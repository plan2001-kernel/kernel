#ifndef PLAN2001_UXRPC_CHILD_H
#define PLAN2001_UXRPC_CHILD_H
/* Dedicated, unprivileged native child with four UXPIPE region grants and
 * NO volume grant. The parent separately holds Vexecute for SPAWN.
 */
int p2001_uxrpc_child_main(int argc, char **argv);
#endif
