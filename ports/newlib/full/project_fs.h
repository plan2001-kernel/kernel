#ifndef PLAN2001_PROJECT_FS_H
#define PLAN2001_PROJECT_FS_H
#include "process.h"
#include "../../project/native.h"
int p9_project_boot(void);
int p9_project_normalize(const char *,char [4096]);
int p9_project_root(void);
int p9_project_resolve(const char *,PjObject *);
int p9_project_list(const char *,int (*)(void*,const char*,unsigned,unsigned),void*);
int p9_file_export_fd(int,P9Plan*,int*,int*);
int p9_file_import_fd(int,int,int,int);
#endif
