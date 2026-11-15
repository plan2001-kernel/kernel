/* Entry for the genuine upstream ash linked against the native Newlib port. */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../full/plan2001_full.h"
#include "../full/process.h"
extern int ash_main(int,char **);
extern int *bb_errno;
const char *applet_name="sh";
void bb_show_usage(void) { fputs("ash: invalid arguments\n",stderr); exit(2); }
int main(int argc,char **argv)
{
    static Pvd view;
    static UxService service;
    bb_errno=&errno;
    if(!p9_full_file_service()) {
        if(pvd_bootstrap(&view)) return 125;
        uxservice_init(&service,&view);
        if(p9_full_bind_files(&service)) return 125;
    }
    setenv("HOME","/",0);setenv("PATH","/bin",0);setenv("PS1","ash$ ",0);
    if(argc>0 && !strcmp(argv[0],"unix-child")) {
        P9Plan plan={0}; P9FdImage image;
        if(p9_fd_export(&image,&plan,0)<0) return 125;
        p9_fd_export_undo(&image);
        char *args[]={"sh","-i",NULL};
        puts("PLAN2001-ASH-READY (volatile files; builtin commands)");
        fflush(stdout);
        
        return ash_main(2,args);
    }
    return ash_main(argc,argv);
}
