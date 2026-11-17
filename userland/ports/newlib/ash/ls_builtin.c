/* Small ash ls builtin for the bounded Unix PVD directory adapter.
 * Listings are one name per line, matching ordinary `ls` with redirected
 * output. Path authority stays in the current process's UxService. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int hidden(const char *name)
{
    return name[0]=='.';
}

static void sort_names(struct dirent **names,unsigned int count)
{
    unsigned int i,j;
    for(i=1;i<count;++i) {
        struct dirent *value=names[i];
        for(j=i;j>0 && strcmp(names[j-1]->d_name,value->d_name)>0;--j)
            names[j]=names[j-1];
        names[j]=value;
    }
}

int p9_ash_ls_builtin(int argc,char **argv)
{
    struct dirent *items[66],*entry;
    struct stat st;
    char output[4608];
    DIR *dir;
    const char *path=".";
    unsigned int count=0,i,used=0;
    int all=0;
    if(argc>1 && strcmp(argv[1],"-a")==0) {
        all=1;
        if(argc>2) path=argv[2];
        if(argc>3) goto usage;
    } else if(argc==2) path=argv[1];
    else if(argc>2) goto usage;

    if(stat(path,&st)==0 && !S_ISDIR(st.st_mode)) {
        size_t n=strlen(path);
        if(n+1>sizeof(output)) return 1;
        memcpy(output,path,n);
        output[n]='\n';
        used=(unsigned int)n+1;
        return write(STDOUT_FILENO,output,used)==(ssize_t)used ? 0 : 1;
    }
    dir=opendir(path);
    if(!dir) {
        fprintf(stderr,"ls: %s: %s\n",path,
                errno==ENOENT ? "no such file or directory" :
                errno==ENOTDIR ? "not a directory" : "cannot open directory");
        return 1;
    }
    while((entry=readdir(dir))!=NULL) {
        if(!all && hidden(entry->d_name)) continue;
        if(count>=sizeof(items)/sizeof(items[0])) {
            closedir(dir);
            fprintf(stderr,"ls: directory listing too large\n");
            return 1;
        }
        items[count++]=entry;
    }
    sort_names(items,count);
    for(i=0;i<count;++i) {
        size_t n=strlen(items[i]->d_name);
        if(n+1>sizeof(output)-used) {
            closedir(dir);
            fprintf(stderr,"ls: directory listing too large\n");
            return 1;
        }
        memcpy(output+used,items[i]->d_name,n);
        used+=(unsigned int)n;
        output[used++]='\n';
    }
    closedir(dir);
    return write(STDOUT_FILENO,output,used)==(ssize_t)used ? 0 : 1;
usage:
    fprintf(stderr,"usage: ls [-a] [directory]\n");
    return 2;
}
