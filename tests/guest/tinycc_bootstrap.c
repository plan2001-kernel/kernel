/* Compile and link inside Plan2001, using only installed persistent files. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "project_fs.h"
#include "install.h"
#include "program.h"
extern long objopen(unsigned long long,void *,long,unsigned long long,void *);
extern long objinvoke(int,void *,long);
extern long objclose(int);
static int report;
static char last_output[4096];
extern long spawn(int,unsigned long long,char **,int,void *);
static void emit(const char *s)
{
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpWrite;c.in=(void*)s;c.inlen=strlen(s);
    objinvoke(report,&c,sizeof c);
}
static void check(int ok,const char *what)
{
    if(!ok){char s[256];snprintf(s,sizeof s,"PLAN2001-TINYCC-FAIL: %s errno=%d\n",what,errno);emit(s);_exit(99);}
}
static void source(const char *path,const char *text)
{
    int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY,0600);
    check(fd>=0,"create source inside guest");
    size_t n=strlen(text);check(write(fd,text,n)==(ssize_t)n,"write source inside guest");
    check(close(fd)==0,"close source");
}
static void run(char *const args[],int success)
{
    size_t captured=0;last_output[0]=0;
    int stream[2],status;check(pipe(stream)==0,"compiler output pipe");
    int pid=fork();check(pid>=0,"fork compiler");
    if(!pid){
        close(stream[0]);
        if(dup2(stream[1],1)<0 || dup2(stream[1],2)<0)_exit(121);
        if(stream[1]>2)close(stream[1]);
        char *env[]={"PATH=/usr/bin:/bin","HOME=/home",NULL};
        execve(args[0][0]=='/'?args[0]:"/usr/bin/tcc",args,env);_exit(122);
    }
    close(stream[1]);char buf[1024];int n;
    while((n=read(stream[0],buf,sizeof buf-1))!=0){
        if(n<0 && errno==EINTR)continue;
        check(n>0,"read compiler output");buf[n]=0;emit(buf);
        size_t keep=(size_t)n;if(keep>sizeof last_output-1-captured)keep=sizeof last_output-1-captured;
        memcpy(last_output+captured,buf,keep);captured+=keep;last_output[captured]=0;
    }
    close(stream[0]);int got;
    do{got=waitpid(pid,&status,0);}while(got<0 && errno==EINTR);
    check(got==pid && WIFEXITED(status),"compiler completion");
    check(success?WEXITSTATUS(status)==0:WEXITSTATUS(status)!=0,"compiler exit status");
}
static unsigned long long le(const unsigned char *p,unsigned n)
{
    unsigned long long v=0;while(n--)v=(v<<8)|p[n];return v;
}
static void elf(const char *path,int relocatable)
{
    unsigned char h[64],ph[56];int fd=open(path,O_RDONLY);
    check(fd>=0 && read(fd,h,sizeof h)==sizeof h,"read guest-produced ELF");
    check(!memcmp(h,"\177ELF\2\1",6) && le(h+18,2)==62,"native AMD64 ELF");
    check(le(h+16,2)==(relocatable?1:2),"ELF output type");
    if(!relocatable){
        unsigned loads=0,executable=0;
        unsigned long long entry=le(h+24,8),offset=le(h+32,8);
        check(le(h+54,2)==56 && le(h+56,2)<16,"bounded program header table");
        for(unsigned i=0;i<le(h+56,2);i++){
            check(lseek(fd,offset+i*56,SEEK_SET)>=0 && read(fd,ph,56)==56,"read program header");
            unsigned type=le(ph,4),flags=le(ph+4,4);
            check(type!=2 && type!=3,"no dynamic loader or dynamic segment");
            if(type==1){
                loads++;check((flags&3)!=3,"no writable executable segment");
                check(le(ph+48,8)==4096 && le(ph+16,8)%4096==le(ph+8,8)%4096,"native segment alignment");
                if(flags&2)check(le(ph+16,8)%4096==0,"page-aligned data mapping");
                if(flags&1)check(le(ph+16,8)==0x200000,"native text base");
                if(flags&1){unsigned long long base=le(ph+16,8),size=le(ph+40,8);
                    executable+=entry>=base && entry<base+size;}
            }
        }
        check(loads==2 && executable==1,"native two-segment executable layout");
    }
    close(fd);
}
int main(void)
{
    ObjOpenConsoleReq console={1,0};report=objopen(ObjOpenConsole,&console,sizeof console,0,0);
    check(report>0,"diagnostic console");
    int volume=objopen(ObjOpenVolume,0,0,0,0);check(volume>0,"trusted boot volume");
    ObjProjectOpen project={0};project.volume=volume;project.oid=0x4f45584543000008ULL;
    project.rights=Vread|Vmeta|Vcontrol|Vdelegate|Vwrite|Vcreate|Vcommit|Vexecute;
    int root=objopen(ObjOpenProject,&project,sizeof project,0,0);
    check(root>0 && p9_file_import(root)==0,"F6 persistent project root");
    struct stat st;int cold=stat("/home/hello",&st)==0;
    if(cold){elf("/home/hello",0);emit("PASS: guest-linked ELF survived cold reboot\n");}
    ObjOpenGeOidReq req={volume,0,P2001_TOOLCHAIN_BUNDLE_OID};
    int input=objopen(ObjOpenGeOid,&req,sizeof req,Vread,0);check(input>0,"read authorized bundle");
    ObjCall c={0};c.version=OBJ_ABI_VERSION;c.op=ObjOpRead;
    long n=objinvoke(input,&c,sizeof c);check(n>0 && n<64*1024*1024,"bounded bundle bytes");
    unsigned char *data=malloc(n);check(data!=NULL,"bundle allocation");
    c.out=data;c.outlen=n;check(objinvoke(input,&c,sizeof c)==n,"read bundle bytes");objclose(input);
    unsigned raw,meta;check(uxobject_split(data,n,&raw,&meta)==0,"bundle framing");
    emit("Installing TinyCC and Newlib as durable ObjectSpace files...\n");
    check(p2001_toolchain_install(data,raw,volume)==0,"install exact toolchain files");free(data);
    int executable=objopen(ObjOpenVolume,0,0,Vexecute|Vdelegate,0);
    check(executable>0 && p9_process_bind_executable("/usr/bin/tcc",executable,P2001_TCC_OID)==0,"preauthorized compiler registration");
    objclose(volume);
    check(stat("/usr/bin/tcc",&st)==0 && st.st_size>100000,"compiler is a real file");
    int fd=open("/usr/bin/tcc",O_RDONLY);unsigned char magic[4];
    check(fd>=0 && read(fd,magic,4)==4 && !memcmp(magic,"\177ELF",4),"read installed compiler ELF");close(fd);
    check(open("/usr/bin/tcc",O_WRONLY)<0,"compiler registration does not grant writes");
    emit("PASS: TinyCC, complete Newlib headers and libraries installed and verified\n");
    /* Boot diagnostic consoles are nondelegable; children receive only the
     * fresh output pipe created for each compiler invocation. */
    close(0);close(1);close(2);
    if(cold){char *persisted[]={"/home/hello",NULL};run(persisted,1);
        check(strstr(last_output,"replacement")!=NULL,"registered executable survived cold reboot");
        emit("PASS: admitted snapshot executable survived cold reboot\n");}
    check(mkdir("/home",0700)==0 || errno==EEXIST,"home directory");
    source("/home/hello.c","#include <stdio.h>\n#include <stdint.h>\n#include <math.h>\n#ifdef __linux__\n#error Linux target leaked\n#endif\n#ifndef __plan2001__\n#error missing native target\n#endif\nint main(void){uint64_t n=81;printf(\"native: %.0f\\n\",sqrt((double)n));return 0;}\n");
    char *compile[]={"tcc","-c","/home/hello.c","-o","/home/hello.o",NULL};run(compile,1);elf("/home/hello.o",1);
    emit("PASS: C compilation inside guest used installed Newlib headers\n");
    char *link[]={"tcc","/home/hello.o","-o","/home/hello",NULL};run(link,1);elf("/home/hello",0);
    emit("PASS: static linking inside guest used installed Newlib and native runtime archives\n");
    source("/home/bad.c","#include <missing-plan2001-header.h>\n");
    char *missing[]={"tcc","-c","/home/bad.c","-o","/home/bad.o",NULL};run(missing,0);
    check(stat("/home/bad.o",&st)<0,"failed compilation publishes no object");
    char *jit[]={"tcc","-run","/home/hello.c",NULL};run(jit,0);
    char *env[]={NULL};
    char *unregistered[]={"/home/not-registered",NULL};
    char *fresh[]={"tcc","/home/hello.o","-o","/home/not-registered",NULL};run(fresh,1);
    check(execve(unregistered[0],unregistered,env)<0 && errno==ENOENT,"new ELF gains no implicit execution authority");
    char *admit[]={"tcc","--register","/home/hello",NULL};run(admit,1);
    char *program[]={"/home/hello",NULL};run(program,1);
    check(strstr(last_output,"native: 9")!=NULL,"run guest-compiled Newlib program");
    source("/home/replacement.c","#include <stdio.h>\nint main(void){puts(\"replacement\");return 0;}\n");
    char *replace[]={"tcc","/home/replacement.c","-o","/home/hello",NULL};run(replace,1);
    run(program,1);check(strstr(last_output,"native: 9")!=NULL,"mutable output cannot change admitted snapshot");
    run(admit,1);run(program,1);check(strstr(last_output,"replacement")!=NULL,"explicit readmission replaces snapshot");
    char *bad_admit[]={"tcc","--register","/home/bad.c",NULL};run(bad_admit,0);
    int scoped=p9_program_lookup("/home/hello");check(scoped>0,"resolve scoped executable capability");
    check(spawn(scoped,P2001_TCC_OID,program,1,NULL)<0,"scoped capability rejects OID substitution");
    ObjOpenHandleReq derive={scoped,0};
    int nonexec=objopen(ObjOpenHandle,&derive,sizeof derive,Vread|Vmeta|Vdelegate,NULL);
    check(nonexec>0 && spawn(nonexec,0,program,1,NULL)<0,"native loader rejects non-executable capability");objclose(nonexec);
    int revoked=objopen(ObjOpenHandle,&derive,sizeof derive,Vexecute|Vdelegate,NULL);
    ObjCall revoke={0};revoke.version=OBJ_ABI_VERSION;revoke.op=ObjOpCapRevoke;
    check(revoked>0 && objinvoke(scoped,&revoke,sizeof revoke)>=0 && spawn(revoked,0,program,1,NULL)<0,"native loader rejects revoked scoped capability");
    objclose(revoked);objclose(scoped);
    emit("PASS: scoped execution, snapshot integrity, explicit readmission and native capability denial\n");
    emit("PLAN2001-TINYCC-PASS\n");
    for(;;)sleep(60); /* Keep the boot root alive until the host ends QEMU. */
}
