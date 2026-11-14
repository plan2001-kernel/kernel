/* A bounded single-producer / single-consumer Unix byte stream over TWO
 * explicitly delegated capability regions. No kernel Pipe or FIFO exists.
 * One 4096-byte data region: producer RW, consumer RO.
 * One 4096-byte ACK region: consumer RW, producer RO.
 * Both pages must be formatted before RW ownership is transferred.
 */
#ifndef PLAN2001_UXPIPE_H
#define PLAN2001_UXPIPE_H

#define UXPIPE_PAGE 4096u
#define UXPIPE_CAPACITY 2048u
#define UXPIPE_MAX_IO 65536u
#define UXPIPE_VERSION 2ULL
#define UXPIPE_DATA_MAGIC 0x3154414450505855ULL
#define UXPIPE_ACK_MAGIC  0x314b434150505855ULL
#define UXPIPE_PUB_OFFSET 16ULL
#define UXPIPE_ACK_OFFSET 16ULL
#define UXPIPE_READY_OFFSET 32ULL
#define UXPIPE_WRITER_READY_OFFSET 32ULL
#define UXPIPE_STARTUP_WAIT_MS 5000ULL
#define UXPIPE_WRITER 1
#define UXPIPE_READER 2

typedef struct {
    unsigned long long magic;
    unsigned long long version;
    unsigned long long produced;       /* published with release ordering */
    unsigned long long closed;         /* 1 means producer sent EOF */
    unsigned long long ready;          /* producer mapped data writer */
    unsigned char payload[UXPIPE_CAPACITY];
    unsigned long long users;          /* serialized process endpoint refs */
} UxPipeData;

typedef struct {
    unsigned long long magic;
    unsigned long long version;
    unsigned long long consumed;       /* published with release ordering */
    unsigned long long closed;         /* 1 means consumer disconnected */
    unsigned long long ready;          /* consumer mapped ACK writer */
    unsigned long long users;
} UxPipeAck;

typedef struct {
    unsigned long long data_base;      /* actual mapped region base */
    unsigned long long ack_base;       /* actual mapped region base */
    UxPipeData *data;
    UxPipeAck *ack;
    int role;
    int transient;                    /* reacquired writer mapping leases */
} UxPipe;

int uxpipe_format(UxPipeData *data, UxPipeAck *ack);
int uxpipe_bind(UxPipe *pipe, UxPipeData *data, UxPipeAck *ack,
                unsigned long long data_base,
                unsigned long long ack_base, int role);
long uxpipe_write(UxPipe *pipe, const void *bytes,
                  unsigned int count, long long timeout_ms);
long uxpipe_read(UxPipe *pipe, void *bytes,
                 unsigned int capacity, long long timeout_ms);
int uxpipe_finish(UxPipe *pipe);
#endif
