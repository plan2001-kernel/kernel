/* Native exec integration driver; every command runs in a fresh guest process.
 * The output pipe tests descriptor inheritance and upstream dispatch, never
 * calls applet_main() or ash builtins in the test driver.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "process.h"
#include "plan2001_full.h"
#include "project_fs.h"
#include "obj.h"
#include "vobj.h"
#include "sysctl.h"

#include "expected_applets.h"
extern long objopen(unsigned long long, void *, long, unsigned long long, void *);
extern long objinvoke(int, void *, long);
extern long objclose(int);
extern long errstr(char *,unsigned long);
static int report;
static unsigned char covered[sizeof(expected_names)/sizeof(expected_names[0])];
static void emit(const char *s)
{
    ObjCall call = {0};
    call.version = OBJ_ABI_VERSION; call.op = ObjOpWrite;
    call.in = (void *)s; call.inlen = strlen(s);
    (void)objinvoke(report, &call, sizeof call);
}
static void check(int ok, const char *what)
{
    if (!ok) {
        emit("PLAN2001-MULTICALL-FAIL: "); emit(what); emit("\n");
        char diagnostic[160];snprintf(diagnostic,sizeof diagnostic,"errno=%d\n",errno);emit(diagnostic);
        memset(diagnostic,0,sizeof diagnostic);errstr(diagnostic,sizeof diagnostic);emit(diagnostic);emit("\n");
        _exit(99);
    }
}
static void command_input(char *const args[], int code, const char *expected,const char *input)
{
    int stream[2],feed[2]={-1,-1},child,status,got;
    char output[4096];
    unsigned used = 0;
    if(input) check(pipe(feed)==0,"create command input pipe");
    check(pipe(stream) == 0, "create output pipe");
    child = fork();
    check(child >= 0, "fork");
    if (!child) {
        char *environment[] = {"PATH=/bin:/usr/bin", "HOME=/", NULL};
        (void)close(stream[0]);
        if(input) {
            (void)close(feed[1]);
            if(dup2(feed[0],0)<0) _exit(120);
            if(feed[0]!=0) (void)close(feed[0]);
        }
        if (dup2(stream[1], 1) < 0 || dup2(stream[1], 2) < 0) _exit(121);
        if (stream[1] != 1 && stream[1] != 2) (void)close(stream[1]);
        execve(strchr(args[0], '/') ? args[0] : "/bin/busybox", args, environment);
        { char error[100]; int n=snprintf(error,sizeof error,"exec failed: errno=%d\n",errno); (void)write(2,error,n); }
        _exit(122);
    }
    check(close(stream[1]) == 0, "drop parent writer");
    if(input) {
        size_t used=0,len=strlen(input);
        (void)close(feed[0]);
        while(used<len) {
            int n=write(feed[1],input+used,len-used);
            if(n<0 && errno==EINTR) continue;
            check(n>0,"feed command stdin"); used+=(unsigned)n;
        }
        check(close(feed[1])==0,"publish input EOF");
    }
    while ((got = read(stream[0], output + used, sizeof output - 1 - used)) != 0) {
        if (got < 0 && errno == EINTR) continue;
        check(got > 0, "read command output");
        used += (unsigned)got;
        check(used < sizeof output - 1, "bounded command output");
    }
    output[used] = 0;
    check(close(stream[0]) == 0, "close reader");
    do { got = waitpid(child, &status, 0); } while (got < 0 && errno == EINTR);
    if (got != child || !WIFEXITED(status) || WEXITSTATUS(status) != code ||
        (expected && strcmp(output, expected))) {
        emit("command: ");
        for (unsigned i = 0; args[i]; ++i) { emit(args[i]); emit(" "); }
        emit("\noutput: "); emit(output); emit("\n");
        char detail[80];
        snprintf(detail, sizeof detail, "status=%d expected=%d\n", status, code);
        emit(detail);
        check(0, "command status/output mismatch");
    }
    const char *name = strrchr(args[0], '/');
    name = name ? name+1 : args[0];
    if (!strcmp(name,"busybox") && args[1]) name=args[1];
    for(unsigned i=0;i<sizeof(covered);++i)
        if(!strcmp(name,expected_names[i])) covered[i]=1;
    emit("PASS: ");
    for (unsigned i = 0; args[i]; ++i) { emit(args[i]); emit(" "); }
    emit("\n");
}
/* A child can attenuate/revoke only capabilities explicitly delegated to it.
 * Failed exec must leave its image alive and publish no replacement process. */
static void denied_execution(int revoke)
{
    int child=fork(), status, got;
    check(child>=0,"fork capability-denial test");
    if(!child) {
        VobjListRec records[P9_GRANTS];
        SysctlReq query={0};
        int source=0, derived;
        query.version=SYSCTL_ABI_VERSION;
        query.data=records; query.datalen=sizeof records;
        long count=sysctl(SysctlScopeOwnProcess,SysctlHandleList,&query,sizeof query);
        if(count<0 || count>P9_GRANTS) _exit(111);
        for(long i=0;i<count;++i)
            if(records[i].kind==VkindGeVolume) source=(int)records[i].handle;
        if(!source) _exit(112);
        ObjOpenHandleReq request={source,0};
        derived=(int)objopen(ObjOpenHandle,&request,sizeof request,
                            revoke ? Vexecute|Vdelegate : Vdelegate,NULL);
        if(derived<=0 || p9_process_bind_executable("/bin/busybox",derived,
                              UINT64_C(0x4f45584543000005))) _exit(113);
        if(revoke) {
            ObjCall call={0};
            call.version=OBJ_ABI_VERSION; call.op=ObjOpCapRevoke;
            if(objinvoke(source,&call,sizeof call)<0) _exit(114);
        }
        char *args[]={"echo","MUST-NOT-EXECUTE",NULL};
        errno=0;
        if(execve("/bin/echo",args,NULL)!=-1 || errno!=ENOEXEC) _exit(115);
        _exit(0);
    }
    do { got=waitpid(child,&status,0); } while(got<0 && errno==EINTR);
    check(got==child && WIFEXITED(status) && WEXITSTATUS(status)==0,
          revoke ? "revoked executable alias denied" : "non-executable grant denied");
}
#define RUN(code, output, ...) do { char *args[] = {__VA_ARGS__, NULL}; \
    command_input(args, code, output, NULL); } while (0)
#define INPUT(input, code, output, ...) do { char *args[] = {__VA_ARGS__, NULL}; \
    command_input(args, code, output, input); } while (0)
static void fixture(const char *path,const char *data)
{
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
    unsigned n=(unsigned)strlen(data);
    check(fd>=0,"create fixture");
    check(write(fd,data,n)==(int)n && close(fd)==0,"write fixture");
}
static void content(const char *path,const char *data)
{
    char buf[1024];
    int fd=open(path,O_RDONLY),n;
    check(fd>=0,"open expected file");
    n=read(fd,buf,sizeof buf);
    check(n==(int)strlen(data) && !memcmp(buf,data,(unsigned)n),"file content");
    check(read(fd,buf,1)==0 && close(fd)==0,"file EOF and close");
}
/* Build a native composition, then exercise it through ordinary Unix paths. */
static void view(const char *label,const char *mode,int writer)
{
    PjObject root,left,right,space;
    check(p9_project_resolve("/tmp",&root)==0,"composition parent");
    UxMeta m;uxmeta_init(&m,label,1,0);
    unsigned char *blob;size_t n;check(pj_empty(&m,&blob,&n)==0,"composition framing");
    unsigned raw,old;check(uxobject_split(blob,(unsigned)n,&raw,&old)==0,"composition split");
    unsigned char metadata[128],*out=malloc(raw+UXMETA_ARENA+32);unsigned pos=0,meta;
    check(out!=NULL,"composition allocation");
    metadata[pos++]=0xa2;metadata[pos++]=0x6c;memcpy(metadata+pos,"project_view",12);pos+=12;
    metadata[pos++]=(unsigned char)(0x60+strlen(mode));memcpy(metadata+pos,mode,strlen(mode));pos+=(unsigned)strlen(mode);
    metadata[pos++]=0x6c;memcpy(metadata+pos,"write_source",12);pos+=12;metadata[pos++]=writer<0?0xf6:0;
    memcpy(out,blob,raw);free(blob);
    check(uxobject_metadata(&m,metadata,pos,out+raw,UXMETA_ARENA,&meta)==0,"composition metadata");
    check(uxobject_footer(out+raw+meta,raw,meta)==0,"composition footer");
    ObjProjectReq req={0};req.operation=ProjectCreate;req.kind=VkindObjectSpace;req.rights=root.rights;
    strcpy(req.name,label);req.data=out;req.length=raw+meta+32;
    check(pj_call(root.handle,&req,NULL,0)>=0,"publish composition object");free(out);
    check(pj_resolve(&root,label,&space)==0,"resolve composition object");
    check(p9_project_resolve("/tmp/view-left",&left)==0 && p9_project_resolve("/tmp/view-right",&right)==0,"composition sources");
    /* Native bindings carry the enforced source ceilings. */
    for(int i=0;i<2;i++){
        PjObject *source=i?&right:&left;ObjProjectReq r={0};r.operation=ProjectLink;
        r.other=source->handle;r.rights=source->rights;if(i!=writer)r.rights&=Vread|Vmeta|Vdelegate|Vexecute;
        strcpy(r.name,i?"1":"0");check(pj_call(space.handle,&r,NULL,0)>=0,"bind live composition source");
    }
    pj_close(&left);pj_close(&right);pj_close(&space);pj_close(&root);
}
static void compositions(void)
{
    check(mkdir("/tmp/view-left",0700)==0 && mkdir("/tmp/view-right",0700)==0,"create composition sources");
    fixture("/tmp/view-left/shared","same");check(link("/tmp/view-left/shared","/tmp/view-right/shared")==0,"shared hard link");
    fixture("/tmp/view-left/conflict","left");fixture("/tmp/view-right/conflict","right");
    view("union","union",-1);view("intersection","intersection",-1);view("writable","union",0);
    content("/tmp/union/shared","same");content("/tmp/intersection/shared","same");
    struct stat st;check(stat("/tmp/union/conflict",&st)<0,"reject ambiguous union");
    DIR *d=opendir("/tmp/intersection");check(d!=NULL,"enumerate intersection despite conflicting labels");
    struct dirent *e;unsigned count=0;while((e=readdir(d)))if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")){check(!strcmp(e->d_name,"shared"),"intersection contains identical pairs only");count++;}
    check(closedir(d)==0 && count==1,"intersection deduplicates common pair");
    check(open("/tmp/union/denied",O_WRONLY|O_CREAT,0600)<0 && errno==EROFS,"composition defaults read only");
    fixture("/tmp/writable/created","chosen");content("/tmp/view-left/created","chosen");
    check(stat("/tmp/view-right/created",&st)<0 && errno==ENOENT,"no copy up or alternate write source");
    check(stat("/tmp/view-left/shared",&st)==0 && st.st_nlink==2,"real hard link count");
    emit("PASS: Unix live union/intersection, ambiguity and explicit write source\n");
}
int main(void)
{
    ObjOpenConsoleReq req = {1, 0};
    int volume;
    report = (int)objopen(ObjOpenConsole, &req, sizeof req, 0, NULL);
    check(report > 0, "authorized diagnostic console");
    volume = (int)objopen(ObjOpenVolume, NULL, 0, Vexecute | Vdelegate, NULL);
    check(volume > 0, "authorized executable volume");

    ObjProjectOpen project={0};project.volume=(int)objopen(ObjOpenVolume,NULL,0,0,NULL);project.oid=UINT64_C(0x4f45584543000007);
    project.rights=Vread|Vmeta|Vcontrol|Vdelegate|Vwrite|Vcreate|Vcommit|Vexecute;
    int root=(int)objopen(ObjOpenProject,&project,sizeof project,0,NULL);
    check(root>0 && p9_file_import(root)==0,"open durable project root");
    objclose(project.volume);
    const char *directories[]={"/bin","/dev","/etc","/home","/tmp","/usr","/usr/bin","/var"};
    for(unsigned i=0;i<sizeof directories/sizeof directories[0];i++)check(mkdir(directories[i],0700)==0,"create native directory");
    {
        struct stat st, target;
        unsigned count=0;
        const char *path;
        check(stat("/bin/echo",&st)<0 && errno==ENOENT,
              "generated name without registration confers no authority");
        check(p9_process_bind_executable("/bin/busybox", volume,
              UINT64_C(0x4f45584543000005)) == 0, "bind one executable");
        check(stat("/bin/busybox",&target)==0 && S_ISREG(target.st_mode), "executable stat");
        for(unsigned i=0;(path=p9_multicall_path(i))!=NULL;++i) {
            check(stat(path,&st)==0 && lstat(path,&st)==0 &&
                  st.st_ino==target.st_ino && access(path,X_OK)==0,
                  "aliases share executable metadata");
            check(open(path,O_WRONLY|O_CREAT,0600)<0 && errno==EACCES,
                  "immutable executable cannot be replaced");
            ++count;
        }
        check(count>P9_EXECUTABLES, "more aliases than executable registry slots");
        count=0;
        DIR *dir=opendir("/bin");
        check(dir!=NULL,"enumerate executable projection");
        struct dirent *entry;
        while((entry=readdir(dir))!=NULL) if(entry->d_type==DT_REG) ++count;
        check(count>=6 && closedir(dir)==0,"directory contains virtual applets");
        check(stat("/bin/not-configured",&st)<0 && errno==ENOENT,"unknown alias absent");
        emit("PASS: authorized immutable aliases, stat/lstat/access and directory enumeration\n");
    }
    compositions();
    /* Diagnostic kernel consoles cannot be delegated by fork. */
    (void)close(0); (void)close(1); (void)close(2);
    check(mkdir("/tmp/cwd-source",0700)==0 && chdir("/tmp/cwd-source")==0,"open cwd view");
    fixture("here","cwd");
    check(rename("/tmp/cwd-source","/tmp/cwd-renamed")==0,"rename live cwd");
    content("here","cwd");
    DIR *cwd_dir=opendir(".");check(cwd_dir!=NULL && closedir(cwd_dir)==0,"enumerate renamed cwd");
    RUN(0,"cwd","/bin/cat","here");
    check(chdir("..")==0,"cwd parent follows retained caller route");
    struct stat cwd_stat;check(stat("cwd-renamed/here",&cwd_stat)==0,"cwd returns to retained parent");
    check(chdir("/../../")==0,"parent traversal stays within delegated root");
    emit("PASS: cwd capabilities survive rename and exec; caller-relative parent traversal\n");
    /* Commands inherit pipe descriptors. */
    /* Leave fourteen unrelated pipe descriptors open across real exec.
     * This needs more than sixteen native grants, not just the old eight. */
    {
        int extra[7][2];
        for(unsigned i=0;i<7;++i) check(pipe(extra[i])==0,"open inherited pipe descriptors");
        RUN(0,"many-grants\n","/bin/echo","many-grants");
        for(unsigned i=0;i<7;++i) {
            check(close(extra[i][0])==0 && close(extra[i][1])==0,"close inherited pipe descriptors");
        }
        emit("PASS: real fork/exec with more than sixteen native grants\n");
    }
    {
        char block[4096],path[64];int fd;struct stat st;
        memset(block,'a',sizeof block);block[sizeof(block)-1]='\n';
        fd=open("/tmp/large-file",O_CREAT|O_WRONLY|O_TRUNC,0600);
        check(fd>=0,"create large file");
        for(unsigned i=0;i<64;++i)check(write(fd,block,sizeof block)==sizeof block,"write beyond 1 KiB");
        check(close(fd)==0 && stat("/tmp/large-file",&st)==0 && st.st_size==262144,"256 KiB file size");
        for(unsigned i=0;i<100;i++){
            snprintf(path,sizeof path,"/tmp/dir%u",i);check(mkdir(path,0700)==0,"more than 64 directories");
            snprintf(path,sizeof path,"/tmp/file%u",i);fixture(path,"node\n");
        }
        RUN(0,"64\n","/usr/bin/awk","END {print NR}","/tmp/large-file");
        RUN(0,"node\n","/bin/cat","/tmp/file99");
        RUN(0,"100\n","/bin/sh","-c","ls /tmp | grep '^dir' | awk 'END {print NR}'");
        RUN(0,"", "/bin/rm","-r","/tmp/dir99");
        for(unsigned i=0;i<100;i++){
            snprintf(path,sizeof path,"/tmp/file%u",i);check(unlink(path)==0,"remove many files");
            if(i!=99){snprintf(path,sizeof path,"/tmp/dir%u",i);check(rmdir(path)==0,"remove many directories");}
        }
        check(unlink("/tmp/large-file")==0,"unlink large persistent file");
        emit("PASS: 256 KiB file and 200 nodes across real fork/exec and pipelines\n");
    }
    {
        UxMeta before,after;struct stat st1,st2;
        fixture("/tmp/object-original","raw-file\n");
        PjObject object;check(p9_project_resolve("/tmp/object-original",&object)==0 && pj_metadata(&object,&before)==0,"read native Unix metadata");pj_close(&object);
        check(!strcmp(before.name,"object-original") && (before.known&UXMETA_BIRTH),"object-owned name and real creation instant");
        check(stat("/tmp/object-original",&st1)==0 && st1.st_size==9 && st1.st_ino!=0,"raw-view stat and local inode");
        check(rename("/tmp/object-original","/tmp/unrelated-binding")==0,"rename namespace binding");
        check(p9_project_resolve("/tmp/unrelated-binding",&object)==0 && pj_metadata(&object,&after)==0 && !strcmp(after.name,before.name) && after.birth_ns==before.birth_ns,"rename preserves object name and creation instant");
        pj_close(&object);
        check(stat("/tmp/unrelated-binding",&st2)==0 && st2.st_ino==st1.st_ino && st2.st_size==9,"rename preserves Unix identity and raw size");
        check(unlink("/tmp/unrelated-binding")==0,"remove metadata fixture");
        emit("PASS: one object-owned name, independent Unix binding, creation time and raw stat projection\n");
    }
    RUN(0, EXPECTED_APPLETS, "busybox", "--list");
    RUN(0, EXPECTED_APPLETS_FULL, "busybox", "--list-full");
    RUN(0, "hello\n", "busybox", "echo", "hello");
    RUN(0, "alias\n", "/bin/echo", "alias");
    RUN(0, "", "busybox", "true");
    RUN(1, "", "busybox", "false");
    RUN(127, NULL, "busybox", "missing-applet");
    RUN(1, NULL, "busybox", "--install");
    RUN(1, NULL, "busybox", "busybox", "--install");
    RUN(0, "file\n", "busybox", "basename", "/tmp/file");
    RUN(0, "/tmp\n", "busybox", "dirname", "/tmp/file/");
    RUN(0, "/\n", "busybox", "dirname", "/file");
    RUN(0, ".\n", "busybox", "dirname", "file");
    RUN(0, "/\n", "busybox", "pwd");
    RUN(0, "guest:17\n", "busybox", "printf", "%s:%d\n", "guest", "17");
    RUN(0, "", "busybox", "test", "1", "-eq", "1");
    RUN(1, "", "busybox", "test", "1", "-eq", "2");
    RUN(0, "", "[", "yes", "=", "yes", "]");
    RUN(0, "ash-ok\n", "busybox", "ash", "-c", "echo ash-ok");
    RUN(0, "sh-ok\n", "busybox", "sh", "-c", "echo sh-ok");
    RUN(0, "path\nabsolute\n", "/bin/sh", "-c",
        "basename /tmp/path; /bin/busybox echo absolute");
    RUN(0, "external\n", "/bin/sh", "-c", "/bin/echo external");
    RUN(0, "relative\n", "/bin/sh", "-c", "cd /bin; ./echo relative");
    RUN(0, "/tmp\n", "/usr/bin/dirname", "/tmp/file");
    RUN(1, "", "/bin/false");
    RUN(0, "", "/bin/true");
    RUN(0, "file-data\n", "/bin/sh", "-c",
        "/bin/echo file-data > /tmp/native; /bin/cat /tmp/native");
    RUN(0, "match\n", "/bin/sh", "-c", "/bin/echo match | /bin/grep '^ma'");
    RUN(0, "file-data\n", "/bin/grep", "^file-[a-z]*$", "/tmp/native");
    RUN(1, "", "/bin/grep", "absent", "/tmp/native");
    RUN(2, NULL, "/bin/grep", "[", "/tmp/native");
    RUN(0, "file-data\n", "/bin/egrep", "file-(data|other)", "/tmp/native");
    RUN(0, "file-data\n", "/bin/fgrep", "file-data", "/tmp/native");
    RUN(0, "native\n", "/bin/ls", "-1", "/tmp");
    RUN(1, NULL, "/bin/ls", "/missing");
    RUN(0, "", "/bin/sh", "-c", "/bin/ls /bin > /tmp/list; test -s /tmp/list");
    RUN(0, "file-data\n", "/bin/cat", "/tmp/native");
    RUN(1, NULL, "/bin/cat", "/missing");
    fixture("/tmp/lines","a\nb\nc\n");
    RUN(0,"a\nb\n","busybox","head","-n","2","/tmp/lines");
    RUN(0,"c\n","busybox","tail","-n","1","/tmp/lines");
    INPUT("a\nb\nc\n",0,"3\n","busybox","wc","-l");
    INPUT("abc def\n",0,"8\n","busybox","wc","-c");
    INPUT("one\n",0,"one\n","busybox","tee","/tmp/tee");
    INPUT("two\n",0,"two\n","busybox","tee","-a","/tmp/tee");
    content("/tmp/tee","one\ntwo\n");
    INPUT("a:1\nb:2\n",0,"1\n2\n","busybox","cut","-d:","-f2");
    INPUT("hello\n",0,"HELLO\n","busybox","tr","a-z","A-Z");
    INPUT("b\na\nb\n",0,"a\nb\nb\n","busybox","sort");
    INPUT("a\na\nb\n",0,"a\nb\n","busybox","uniq");
    fixture("/tmp/identical","a\nb\nc\n");
    RUN(0,"","busybox","cmp","/tmp/lines","/tmp/identical");
    RUN(1,NULL,"busybox","cmp","/tmp/lines","/tmp/tee");
    RUN(2,NULL,"busybox","cmp","/tmp/lines","/missing");
    RUN(0,"","/bin/mkdir","/tmp/tree");
    RUN(1,NULL,"/bin/mkdir","/tmp/tree");
    RUN(1,NULL,"/bin/mkdir","/missing/child");
    fixture("/tmp/tree/item","remove-me");
    RUN(1,NULL,"/bin/rm","/tmp/tree");
    RUN(0,"","/bin/rm","-r","/tmp/tree");
    struct stat removed;
    check(stat("/tmp/tree",&removed)<0 && errno==ENOENT,"recursive directory removal");
    RUN(0,"","/bin/mkdir","/tmp/tree"); /* deleted PVD slots are reusable */
    RUN(0,"","/bin/rm","-r","/tmp/tree");
    RUN(1,NULL,"/bin/rm","/missing");
    RUN(0,"","/bin/rm","-f","/missing");
    fixture("/tmp/from","moved");
    RUN(0,"","/bin/mv","/tmp/from","/tmp/to");
    content("/tmp/to","moved");
    check(stat("/tmp/from",&removed)<0 && errno==ENOENT,"move removes old name");
    RUN(1,NULL,"/bin/mv","/bin/busybox","/tmp/stolen");
    RUN(1,NULL,"/bin/rm","/bin/echo");
    RUN(0,"a\nB\nc\n","/bin/sed","s/b/B/","/tmp/lines");
    INPUT("one 2\ntwo 3\n",0,"5\n","busybox","awk","{s+=$2} END {print s}");
    RUN(0,"9\n32\n","busybox","awk","BEGIN {print sqrt(81); print 2^5}");
    RUN(0,"7\n","busybox","awk","BEGIN {print system(\"exit 7\")}");
    RUN(0,"child\n","busybox","awk","BEGIN {\"echo child\" | getline x; close(\"echo child\"); print x}");
    RUN(0,"0\n","busybox","awk","BEGIN {print \"data\" | \"cat > /tmp/awkpipe\"; print close(\"cat > /tmp/awkpipe\")}");
    content("/tmp/awkpipe","data\n");
    RUN(1,NULL,"busybox","awk","BEGIN {");
    RUN(1,NULL,"/bin/sed","[");
    RUN(1,NULL,"/bin/mkdir","-p","/tmp/unsupported/child");
    check(stat("/tmp/unsupported",&removed)<0 && errno==ENOENT,"unsupported mkdir mask fails before mutation");
    RUN(1,NULL,"/bin/sed","-i","s/a/A/","/tmp/lines");
    content("/tmp/lines","a\nb\nc\n");
    int held=open("/tmp/to",O_RDONLY);
    check(held>=0,"hold source across rename");
    RUN(0,"","/bin/mv","/tmp/to","/tmp/renamed");
    char held_data[8];
    check(read(held,held_data,5)==5 && !memcmp(held_data,"moved",5),"rename preserves open source description");
    fixture("/tmp/destination","keep");
    int destination=open("/tmp/destination",O_RDONLY);
    check(destination>=0,"hold rename destination");
    RUN(0,"","/bin/mv","/tmp/renamed","/tmp/destination");
    content("/tmp/destination","moved");
    check(read(destination,held_data,4)==4 && !memcmp(held_data,"keep",4),"replacement preserves open destination inode");
    check(unlink("/tmp/destination")==0,"unlink open renamed file");
    check(lseek(held,0,SEEK_SET)==0 && read(held,held_data,5)==5 && !memcmp(held_data,"moved",5),"unlink preserves open inode");
    check(close(held)==0 && close(destination)==0,"close held descriptions");
    errno=0;
    check(rmdir("/")==-1 && errno==EBUSY,"view root cannot be removed");
    FILE *first=popen("echo first","re"),*second;
    check(first!=NULL && fcntl(fileno(first),F_GETFD)==FD_CLOEXEC,"popen cloexec");
    check(fgets(held_data,sizeof held_data,first)!=NULL && !strcmp(held_data,"first\n"),"popen first command");
    second=popen("echo second","r");
    check(second!=NULL,"second popen command");
    int shell_status=system("exit 3");
    check(WIFEXITED(shell_status) && WEXITSTATUS(shell_status)==3,"system waits for its own child");
    check(fgets(held_data,sizeof held_data,second)!=NULL && !strcmp(held_data,"second\n"),"popen second command");
    check(pclose(second)==0 && pclose(first)==0,"pclose reaps exact children");
    first=popen("exit 7","r");
    check(first!=NULL,"popen nonzero status");
    shell_status=pclose(first);
    check(WIFEXITED(shell_status) && WEXITSTATUS(shell_status)==7,"pclose encoded exit status");
    errno=0;
    check(popen("true","invalid")==NULL && errno==EINVAL,"popen rejects invalid mode");
    for(unsigned i=0;i<sizeof(covered);++i) {
        if(!covered[i]) { emit(expected_names[i]); check(0,"enabled applet lacks guest command test"); }
    }
    denied_execution(0);
    denied_execution(1);
    emit("PASS: attenuated and revoked executable grants denied by native loader\n");
    RUN(0,"parent-still-authorized\n","/bin/echo","parent-still-authorized");
    emit("PLAN2001-MULTICALL-PASS\n");
    for (;;) sleep(60);
}
