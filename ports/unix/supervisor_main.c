/* Trusted manifest F5 boot target. The console's original authority stays
 * in this process. Spawned Unix init gets only the explicit proxy grants.
 */
#include "supervisor.h"
extern void exits(char *);
int main(int argc, char **argv)
{
    unsigned long long child=0, manifest=0;
    if ((argc!=2 && argc!=3) || !argv ||
        uxsupervisor_parse_oid(argv[1],&child)!=0 ||
        (argc==3 && uxsupervisor_parse_oid(argv[2],&manifest)!=0))
        exits("unix-supervisor: invalid trusted manifest args");
    if (uxsupervisor_run(child,manifest)<0)
        exits("unix-supervisor: no authorized child or child failed");
    exits("");
    return 1;
}
