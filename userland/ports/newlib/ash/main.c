/* Entry for the genuine upstream ash linked against the native Newlib port. */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../full/plan2001_full.h"
#include "../full/process.h"
#include "../full/project_fs.h"
extern int ash_main(int,char **);
extern int *bb_errno;
const char *applet_name="sh";
void bb_show_usage(void) { fputs("ash: invalid arguments\n",stderr); exit(2); }
int main(int argc,char **argv)
{
    bb_errno=&errno;
    if(p9_project_boot())return 125;
    setenv("HOME","/",0);setenv("PATH","/bin",0);setenv("PS1","ash$ ",0);
    if(argc>0 && !strcmp(argv[0],"unix-child")) {
        P9Plan plan={0}; P9FdImage image;
        if(p9_fd_export(&image,&plan,0)<0) return 125;
        p9_fd_export_undo(&image);
        /* Ash writes interactive prompts to stderr. F5 is one text console,
         * so merge both output descriptors to preserve prompt/output order. */
        if(dup2(STDOUT_FILENO,STDERR_FILENO)<0) return 125;
        char *args[]={"sh","-i",NULL};
        puts("PLAN2001-ASH-READY (persistent ObjectSpace files; builtin commands)");
        fflush(stdout);

        return ash_main(2,args);
    }
    return ash_main(argc,argv);
}
