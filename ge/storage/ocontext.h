/* Experimental N13 native object view; kernel pointers never cross the ABI. */
enum {
	Oread=1, Owrite=2, Ocreate=4, Ocommit=8, Oexecute=16, Odelegate=32,
	Oall=63, Obindmax=32, Onamemax=32,
};
typedef struct Ohand Ohand;
struct Ohand {
	Ref ref;
 Ohand *origin; /* shared revocation authority, pinned by derived caps */
	QLock gate;             /* serializes revoke with admitted operations */
	void *sp;
 void *provider; /* sealed flattened view; never an object grant */
	uvlong sid, oid;
	int rights, bootstrap, revoked, caps;
	char space[256];
};
void ohandput(Ohand*);
void* octxnew(void);
void* octxcopy(void*);
void octxput(void*);
int octxmember(void*, Ohand*);
void octxunbind(void*, char*);
int octxsame(void*, void*);
int octxshareable(void*);

int octxmatches(void*, void*);

int octxsnapshot(void*, Ohand**, int);
void octxcompose(void*, void*, int, uvlong);
void octxunmount(void*, uvlong);

uvlong octxepoch(void*);

/* Every role owns one authorized object Handle, including GE/O roles. */
void octxbindobject(void*, char*, void*, int);
void* octxobject(void*, char*);
Ohand* vobjbindingcap(void*);
Ohand* vobjbindinggecap(void*);
int vobjbindingdelegable(void*);
void vobjbindinghold(void*);
void vobjbindingput(void*);

void octxcopybinding(void*, void*, char*);
