/* Host ABI front end: native engine headers never reach glibc callers. */
#include <stdio.h>
#include <execinfo.h>
void otar_backtrace(void){void *p[32];int n=backtrace(p,32);backtrace_symbols_fd(p,n,2);}
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
int core_open(int,unsigned long long,int);
int core_get(unsigned long long,void**,unsigned long long*);
int core_owner(unsigned long long,unsigned long long*);
int core_create(unsigned long long,void*,unsigned long long,unsigned long long*);
int core_put(unsigned long long,unsigned long long,void*,unsigned long long);
int core_commit(void);
int main(int argc,char **argv)
{
 if(argc!=4)return 2;
 if(core_open(atoi(argv[1]),strtoull(argv[2],NULL,10),atoi(argv[3])))return 1;
 char line[160];puts("READY");fflush(stdout);
 while(fgets(line,sizeof line,stdin)){
  char op;unsigned long long oid=0,owner=0,n=0;void *buf=NULL;int rc=-1;
  if(sscanf(line,"%c %llu %llu %llu",&op,&oid,&owner,&n)<1)return 2;
  if(op=='Q')return 0;
  if(op=='G'){rc=core_get(oid,&buf,&n);printf("%d %llu\n",rc,rc?0:n);if(!rc && n)fwrite(buf,1,n,stdout);free(buf);}
  else if(op=='O'){rc=core_owner(oid,&owner);printf("%d %llu\n",rc,owner);}
  else if(op=='K'){rc=core_commit();printf("%d 0\n",rc);}
  else if(op=='C' || op=='P'){
   if(n>64u*1024u*1024u)return 2;
   buf=malloc(n?n:1);if(!buf || fread(buf,1,n,stdin)!=n)return 2;
   rc=op=='C'?core_create(owner,buf,n,&oid):core_put(oid,owner,buf,n);
   free(buf);printf("%d %llu\n",rc,oid);
  }else return 2;
  fflush(stdout);if(rc<0)return 1;
 }
 return 0;
}
