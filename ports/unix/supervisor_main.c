/* Trusted manifest F5 boot target. The console's original authority stays
 * in this process. Spawned Unix init gets only the explicit proxy grants.
 */
#include "supervisor.h"
extern void exits(char *);
int main(int argc, char **argv)
{
    unsigned long long child=0, manifest=0, project=0x4f45584543000007ULL;
    if ((argc!=2 && argc!=3 && argc!=4) || !argv ||
        uxsupervisor_parse_oid(argv[1],&child)!=0 ||
        (argc==3 && uxsupervisor_parse_oid(argv[2],&manifest)!=0))
        exits("unix-supervisor: invalid trusted manifest args");
    /* Four arguments explicitly select a durable project, with a literal
     * "project" selector instead of the legacy UXFS manifest OID. */
    if (argc==3) project=0;
    if (argc==4 &&
        (!argv[2] || argv[2][0]!='p' || argv[2][1]!='r' ||
         argv[2][2]!='o' || argv[2][3]!='j' || argv[2][4]!='e' ||
         argv[2][5]!='c' || argv[2][6]!='t' || argv[2][7]!=0 ||
         uxsupervisor_parse_oid(argv[3],&project)!=0))
        exits("unix-supervisor: invalid project selector");
    if (uxsupervisor_run(child,manifest,project)<0)
        exits("unix-supervisor: no authorized child or child failed");
    exits("");
    return 1;
}
