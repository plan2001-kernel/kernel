/* Host contract: compiler input snapshots, random seeks and real outputs. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int p2001_tcc_open(const char *,int,...);
ssize_t p2001_tcc_read(int,void *,size_t);
off_t p2001_tcc_lseek(int,off_t,int);
int p2001_tcc_close(int);
int p9_project_normalize(const char *path,char out[4096])
{if(strlen(path)>=4096)return -1;strcpy(out,path);return 0;}
int main(void)
{
    char name[]="/tmp/p2001-tcc-input-XXXXXX",buf[8]={0};
    int writer=mkstemp(name);assert(writer>=0);
    assert(write(writer,"abcdef",6)==6);
    int reader=p2001_tcc_open(name,O_RDONLY);assert(reader>=0);
    assert(pwrite(writer,"XYZ",3,0)==3);
    assert(p2001_tcc_lseek(reader,-3,SEEK_END)==3);
    assert(p2001_tcc_read(reader,buf,8)==3 && !memcmp(buf,"def",3));
    assert(p2001_tcc_lseek(reader,0,SEEK_SET)==0);
    assert(p2001_tcc_read(reader,buf,3)==3 && !memcmp(buf,"abc",3));
    assert(p2001_tcc_lseek(reader,-4,SEEK_CUR)<0 && errno==EINVAL);
    assert(p2001_tcc_lseek(reader,INT64_MAX,SEEK_SET)==INT64_MAX);
    assert(p2001_tcc_read(reader,buf,1)==0);
    assert(p2001_tcc_lseek(reader,1,SEEK_CUR)<0 && errno==EINVAL);
    assert(p2001_tcc_close(reader)==0);
    reader=p2001_tcc_open(name,O_RDWR);assert(reader>=0);
    assert(p2001_tcc_read(reader,buf,3)==3 && !memcmp(buf,"XYZ",3));
    assert(p2001_tcc_lseek(reader,0,SEEK_SET)==0);
    assert(write(reader,"out",3)==3);
    assert(p2001_tcc_close(reader)==0);
    assert(pread(writer,buf,3,0)==3 && !memcmp(buf,"out",3));
    close(writer);unlink(name);
    assert(p2001_tcc_open(name,O_RDONLY)<0 && errno==ENOENT);
    puts("PASS: compiler input snapshot/seek boundaries and actual file output");
    return 0;
}
