/* Stage 6: explicit SPAWN/Oinheritv delegation, not fork/execve.
 * Child handles are assigned in grant order, not at parent fd numbers.
 */
#ifndef PLAN2001_UXSPAWN_H
#define PLAN2001_UXSPAWN_H
typedef unsigned long long UxRights;
typedef struct {
    int handle;                 /* actual process-local parent Handle */
    UxRights rights;            /* strict attenuation, never ambient fd */
} UxGrant;
typedef struct {
    int executable_volume;      /* Vexecute-authorized GE/O volume Handle */
    unsigned long long oid;     /* executable identity, not authority */
    UxGrant grant[8];           /* capability-bound grants, max native Oinheritv */
    unsigned int grant_count;
} UxLaunch;
int uxspawn_start(const UxLaunch *launch, char **argv, int argc);
int uxspawn_wait(int process, long long timeout, int *exited_ok);
#endif
