/* Trusted manifest Unix supervisor: no kernel grants are escalated.
 * It holds the kernel's NON-delegable console and creates separate,
 * explicitly delegable region streams for one unprivileged F5 child.
 */
#ifndef PLAN2001_UNIX_SUPERVISOR_H
#define PLAN2001_UNIX_SUPERVISOR_H
#include "uxpipe.h"
typedef struct {
    int handles[6];          /* data+ACK per stream: stdin,stdout,stderr */
    unsigned long long maps[6];
    UxPipe streams[3];       /* producer(stdin), consumer(stdout,stderr) */
    unsigned char pending[128];
    unsigned int pending_len, pending_pos;
    unsigned int pending_eof;
    unsigned char edit[128];
    unsigned int edit_len;
} UxSupervisor;

int uxsupervisor_parse_oid(const char *arg, unsigned long long *oid);
int uxsupervisor_prepare(UxSupervisor *supervisor);
int uxsupervisor_pump(UxSupervisor *supervisor);
void uxsupervisor_release(UxSupervisor *supervisor);
int uxsupervisor_run(unsigned long long child_oid,
                     unsigned long long manifest_oid,
                     unsigned long long project_oid);
#endif
