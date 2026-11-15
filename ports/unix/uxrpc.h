/* A bounded, single-client Unix file-service RPC over TWO UXPIPE channels.
 * The trusted owner holds UxService + the GE/O volume capability. Clients
 * receive only the four attenuated region grants; raw OIDs and paths are
 * never kernel authority. All messages are fixed-size, versioned AMD64 v1.
 */
#ifndef PLAN2001_UXRPC_H
#define PLAN2001_UXRPC_H
#include "uxfd.h"
#include "uxpipe.h"
#define UXRPC_MAGIC 0x3143505258465558ULL
#define UXRPC_VERSION 1u
enum {
    UXRPC_OPEN=1, UXRPC_READ=2, UXRPC_WRITE=3, UXRPC_CLOSE=4,
    UXRPC_SEEK=5, UXRPC_STAT=6, UXRPC_UNLINK=7, UXRPC_EMIT=8,
    UXRPC_QUIT=9
};
typedef struct {
    unsigned long long magic;
    unsigned int version, seq, op, flags, fd, count;
    long long offset;
    int whence;
    char path[PVD_PATH_MAX];
    unsigned char data[UXFS_DATA_MAX];
} UxRpcRequest;
typedef struct {
    unsigned long long magic;
    unsigned int version, seq;
    long long result;
    unsigned int kind, length;
    unsigned char data[UXFS_DATA_MAX];
} UxRpcReply;
typedef struct {
    UxService *service;
    UxFdTable fds;
    UxPipe request, response;
    char prefix[PVD_PATH_MAX];
    unsigned int next_seq;
    unsigned char output[UXFS_DATA_MAX];
    unsigned int output_size;
} UxRpcServer;
typedef struct {
    UxPipe request, response;
    unsigned int next_seq;
} UxRpcClient;
int uxrpc_server_init(UxRpcServer *s, UxService *service,
                      const char *prefix, const UxPipe *request,
                      const UxPipe *response);
/* 0: handled one request; 1: received explicit QUIT; -1: protocol failure */
int uxrpc_server_once(UxRpcServer *s);
int uxrpc_client_init(UxRpcClient *c, const UxPipe *request,
                      const UxPipe *response);
int uxrpc_client_call(UxRpcClient *c, UxRpcRequest *request,
                      UxRpcReply *reply);
int uxrpc_open(UxRpcClient *c, const char *path, unsigned int rights);
long uxrpc_read(UxRpcClient *c, int fd, void *buf, unsigned int n);
long uxrpc_write(UxRpcClient *c, int fd, const void *buf, unsigned int n);
int uxrpc_close(UxRpcClient *c, int fd);
long long uxrpc_seek(UxRpcClient *c, int fd, long long off, int whence);
int uxrpc_stat(UxRpcClient *c, const char *path, unsigned int *kind,
               unsigned int *length);
int uxrpc_unlink(UxRpcClient *c, const char *path);
int uxrpc_emit(UxRpcClient *c, const void *buf, unsigned int n);
int uxrpc_quit(UxRpcClient *c);
#endif
