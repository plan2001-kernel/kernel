/* Explicit admission catalog in the delegated file root. Catalog write
 * authority is admission authority. Targets are fresh read/execute snapshots,
 * never mutable aliases of compiler output. No volume capability is opened. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "project_fs.h"
#include "program.h"
static const char *stage="program registration";
const char *p9_program_diagnostic(void){return stage;}
static int catalog(const char *path,char out[4096])
{
    char normal[4096];
    if(p9_project_normalize(path,normal))return -1;
    if(!strcmp(normal,"/") || !strncmp(normal,"/.programs",10)){
        errno=EINVAL;return -1;
    }
    if(strlen(normal)>=sizeof(((P9Executable*)0)->path) || strlen(normal)+11>=4096){errno=ENAMETOOLONG;return -1;}
    strcpy(out,"/.programs");strcat(out,normal);return 0;
}
int p9_program_lookup(const char *path)
{
    char target[4096];PjObject o={0};
    if(catalog(path,target) || p9_project_resolve(target,&o))return -1;
    unsigned need=Vread|Vmeta|Vexecute|Vdelegate;
    if(o.kind!=VkindGe || (o.rights&need)!=need || (o.rights&(Vwrite|Vcontrol|Vcommit))){
        pj_close(&o);errno=EACCES;return -1;
    }
    unsigned char *blob;size_t size,raw,meta;
    if(pj_blob(&o,&blob,&size,&raw,&meta)){pj_close(&o);return -1;}
    int rc=p9_program_validate(blob,raw);free(blob);
    if(rc){pj_close(&o);return -1;}
    return o.handle;
}
int p9_program_register(const char *path)
{
    stage="select program";
    char target[4096];PjObject source={0},parent={0},snapshot={0};
    unsigned char *blob=NULL;size_t size,raw,meta;
    if(catalog(path,target) || p9_project_resolve(path,&source))return -1;
    unsigned need=Vread|Vmeta|Vexecute|Vdelegate;
    if(source.kind!=VkindGe || (source.rights&need)!=need){pj_close(&source);errno=EACCES;return -1;}
    stage="read program";
    int rc=pj_blob(&source,&blob,&size,&raw,&meta);pj_close(&source);
    if(rc)return -1;
    stage="validate native ELF";
    if(p9_program_validate(blob,raw)){free(blob);return -1;}
    stage="create admission catalog";
    char *last=strrchr(target,'/');
    for(char *p=target+1;p<last;p++)if(*p=='/'){
        *p=0;
        PjObject dir={0};
        if(p9_project_resolve(target,&dir)){
            if(errno!=ENOENT || mkdir(target,0700)<0){*p='/';free(blob);return -1;}
        }else pj_close(&dir);
        *p='/';
    }
    *last=0;
    if(p9_project_resolve(target,&parent)){
        if(errno!=ENOENT || mkdir(target,0700)<0 || p9_project_resolve(target,&parent)){free(blob);return -1;}
    }
    const char *label=last+1;
    char temporary[64];static unsigned serial;
    stage="create read-only snapshot";
    for(unsigned attempt=0;;attempt++){
        snprintf(temporary,sizeof temporary,"stage-%d-%u",(int)getpid(),++serial);
        rc=pj_create_attenuated(&parent,temporary,VkindGe,blob,raw,need,&snapshot);
        if(!rc || errno!=EEXIST || attempt==63)break;
    }
    free(blob);pj_close(&snapshot);
    if(!rc){
        stage="publish program snapshot";
        rc=pj_rename(&parent,temporary,&parent,label,1);
        if(rc){int saved=errno;pj_remove(&parent,temporary,0);errno=saved;}
    }
    pj_close(&parent);return rc;
}
