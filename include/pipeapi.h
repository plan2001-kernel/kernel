#ifndef _PIPEAPI_H_
#define _PIPEAPI_H_

/*
 * Both handles are full-duplex endpoints of one pair.  The names are
 * conventional: rd is endpoint 0 (reads q0, writes q1) and wr is endpoint 1
 * (reads q1, writes q0).
 *
 * Pipe is reached only through the five-operation OBJ ABI:
 * OBJCREATE(ObjKindPipe) -> end 0, OBJINVOKE(ObjOpPipePeer) -> end 1,
 * OBJINVOKE(Read/Write) -> bytes, OBJCLOSE -> release.  The historical
 * PIPENEW/PIPEREAD/PIPEWRITE/PIPECLOSE numbers are reserved and dispatch to
 * nothing.
 */
typedef struct Pipepair Pipepair;
struct Pipepair {
	int	rd;
	int	wr;
};

#endif
