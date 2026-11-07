#include "u.h"
#include "../../kernel/core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../../kernel/core/error.h"
#include "ocontext.h"

typedef struct Obinding Obinding;
typedef struct OContext OContext;
struct Obinding { char name[Onamemax]; Ohand *grant; void *object; uvlong attachment; };
struct OContext {
	Ref ref;
	Lock lk;
 uvlong epoch;
	Obinding b[Obindmax];
};

static int used(Obinding *b) {return b->grant!=nil || b->object!=nil;}
static void retain(Obinding *b) {
 if(b->grant) incref(&b->grant->ref);
 if(b->object) vobjbindinghold(b->object);
}
static void release(Obinding *b) {
 ohandput(b->grant);vobjbindingput(b->object);
}

void*
octxnew(void)
{
	OContext *c;
	c = mallocz(sizeof *c, 1);
	if(c == nil) return nil;
	c->ref.ref = 1;
	return c;
}

void
octxput(void *v)
{
	OContext *c = v;
	int i;
	if(c == nil || decref(&c->ref))
		return;
	for(i=0; i<Obindmax; i++)
		release(&c->b[i]);
	free(c);
}

void*
octxcopy(void *v)
{
	OContext *c = v, *n;
	int i;
	n = octxnew();
	if(n == nil) error(Enomem);
	if(c != nil){
		lock(&c->lk);
		memmove(n->b, c->b, sizeof n->b);
  n->epoch=c->epoch;
		for(i=0; i<Obindmax; i++)
			retain(&n->b[i]);
		unlock(&c->lk);
	}
	return n;
}

/* Only the first binding for a role is effective, including revoked entries. */
static int effective(OContext *c, int i)
{
 int j;
 if(!used(&c->b[i])) return 0;
 for(j=0;j<i;j++) if(used(&c->b[j]) && strcmp(c->b[j].name,c->b[i].name)==0) return 0;
 return 1;
}
int
octxmember(void *v, Ohand *h)
{
 OContext *c=v; int i, found=0;
 if(c == nil) return 0;
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(effective(c,i) && c->b[i].grant==h) found=1;
 unlock(&c->lk);
 return found;
}
/* Pins a consistent ordered snapshot; callers release every returned grant. */
int
octxsnapshot(void *v, Ohand **out, int all)
{
 OContext *c=v; int i,n=0;
 if(c == nil) return 0;
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(c->b[i].grant && (all || effective(c,i))){
  out[n]=c->b[i].grant; incref(&out[n]->ref); n++;
 }
 unlock(&c->lk); return n;
}
Ohand*
octxlookup(void *v, char *name)
{
	OContext *c = v;
	Ohand *h = nil;
	int i;
	if(c == nil)
		error(Eperm);
	lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i]) && strcmp(c->b[i].name,name)==0){
  h=c->b[i].grant;if(h) incref(&h->ref);break;
 }
	unlock(&c->lk);
	if(h == nil)
		error("O: no binding");
	return h;
}

/* Compose private flattened providers atomically. No provider/context links,
 * hence neither reference cycles nor recursive resolution are possible. */
void
octxcompose(void *v, void *provider, int mode, uvlong attachment)
{
 OContext *c=v, *p=provider;
 Obinding next[Obindmax], old[Obindmax];
 int i,j,n=0,remove;
 if(c==nil || p==nil || c==p || mode<0 || mode>2) error(Ebadarg);
 memset(next,0,sizeof next);
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i])){
  remove=0;
  if(mode==0) for(j=0;j<Obindmax;j++)
   if(used(&p->b[j]) && strcmp(c->b[i].name,p->b[j].name)==0) remove=1;
  if(!remove) next[n++]=c->b[i];
 }
 for(i=0;i<Obindmax;i++) if(used(&p->b[i])){
  if(n==Obindmax){unlock(&c->lk);error("O: view full");}
  if(mode==2) next[n]=p->b[i];
  else { memmove(next+1,next,n*sizeof next[0]);next[0]=p->b[i]; }
  if(mode==2) next[n].attachment=attachment;
  else next[0].attachment=attachment;
  n++;
 }
 /* Reverse the prepended block so provider precedence remains unchanged. */
 if(mode!=2){
  int count=0; Obinding tmp;
  for(i=0;i<Obindmax;i++) if(used(&p->b[i])) count++;
  for(i=0;i<count/2;i++){tmp=next[i];next[i]=next[count-1-i];next[count-1-i]=tmp;}
 }
 memmove(old,c->b,sizeof old);
 for(i=0;i<n;i++) retain(&next[i]);
 memmove(c->b,next,sizeof next);c->epoch++;
 unlock(&c->lk);
 for(i=0;i<Obindmax;i++) release(&old[i]);
}
void
octxbindmode(void *v, char *name, Ohand *h, int mode)
{
 OContext provider;
 memset(&provider,0,sizeof provider);
 strcpy(provider.b[0].name,name);provider.b[0].grant=h;
 octxcompose(v,&provider,mode,0);
}
void
octxbind(void *v, char *name, Ohand *h)
{
 octxbindmode(v,name,h,0);
}
static void
removebindings(void *v, char *name, uvlong attachment)
{
 OContext *c=v; Obinding next[Obindmax]; Obinding old[Obindmax];
 int i,n=0,k=0;
 if(c==nil) error(Eperm);
 memset(next,0,sizeof next);
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i])){
  if(name ? strcmp(c->b[i].name,name)==0 : c->b[i].attachment==attachment)
   old[k++]=c->b[i];
  else next[n++]=c->b[i];
 }
 memmove(c->b,next,sizeof next);c->epoch++;unlock(&c->lk);
 if(!k) error("O: no binding or attachment");
 for(i=0;i<k;i++) release(&old[i]);
}
void octxunbind(void *v, char *name) {removebindings(v,name,0);}
void octxunmount(void *v, uvlong attachment) {
 if(!attachment) error(Ebadarg);
 removebindings(v,nil,attachment);
}
/* Both maps are private unpublished snapshots here. */
int
octxsame(void *a, void *b)
{
	OContext *x = a, *y = b;
	int i, j, found;
	for(i = 0; i < Obindmax; i++){
		if(!effective(x,i)) continue;
		found = 0;
		for(j = 0; j < Obindmax; j++)
			if(y->b[j].grant == x->b[i].grant && y->b[j].object == x->b[i].object && strcmp(y->b[j].name, x->b[i].name) == 0)
				found = 1;
		if(!found) return 0;
	}
	return 1;
}

/* Linearization point for explicitly sharing a mutable view. */
int
octxmatches(void *live, void *snapshot)
{
	OContext *c = live;
	int same;
	lock(&c->lk);
 same=1;
 for(int i=0;i<Obindmax;i++){
  OContext *p=snapshot;
  if(c->b[i].grant!=p->b[i].grant || c->b[i].object!=p->b[i].object || c->b[i].attachment!=p->b[i].attachment ||
     strcmp(c->b[i].name,p->b[i].name)!=0) same=0;
 }
	unlock(&c->lk);
	return same;
}

uvlong octxepoch(void *v) {
 OContext *c=v;uvlong e;
 if(!c) return 0;
 lock(&c->lk);e=c->epoch;unlock(&c->lk);return e;
}

/* Commit both representations at one spinlocked linearization point. All
 * fallible preparation happens before this call. Destructors run after it.
 * Generic COPY/SHARE replace the role; EXCLUSIVE rejects an existing role.
 * Native union composition remains available through the native interface. */
void
octxbindobject(void *v, char *name, Ohand *grant, void *object, int mode, int remove)
{
 OContext *c=v;
 Obinding next[Obindmax],old[Obindmax];
 int i,n=0,exists=0;
 if(c==nil) error(Eperm);
 memset(next,0,sizeof next);
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i])){
  if(strcmp(name,c->b[i].name)==0) exists=1;
  else next[n++]=c->b[i];
 }
 if(!remove && ((exists && mode==1) || n==Obindmax)){
  unlock(&c->lk);error(exists?"object: binding exists":"object: binding limit");
 }
 if(!remove){
  strcpy(next[n].name,name);next[n].grant=grant;next[n].object=object;n++;
 }
 for(i=0;i<n;i++) retain(&next[i]);
 memmove(old,c->b,sizeof old);memmove(c->b,next,sizeof next);c->epoch++;
 unlock(&c->lk);
 for(i=0;i<Obindmax;i++) release(&old[i]);
}
void*
octxobject(void *v, char *name)
{
 OContext *c=v;void *o=nil;int i;
 if(c==nil) error(Eperm);
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i]) && strcmp(name,c->b[i].name)==0){
  o=c->b[i].object;if(o) vobjbindinghold(o);break;
 }
 unlock(&c->lk);
 if(o==nil) error("object: no binding");
 return o;
}

void
octxcopybinding(void *dst, void *src, char *name)
{
 OContext *c=src;Obinding b;int i;
 memset(&b,0,sizeof b);
 lock(&c->lk);
 for(i=0;i<Obindmax;i++) if(used(&c->b[i]) && strcmp(name,c->b[i].name)==0){b=c->b[i];retain(&b);break;}
 unlock(&c->lk);
 if(!used(&b)) error("object: no binding");
 if(waserror()){release(&b);nexterror();}
 octxbindobject(dst,name,b.grant,b.object,0,0);
 poperror();release(&b);
}
