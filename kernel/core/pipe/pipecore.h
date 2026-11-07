#ifndef _PIPECORE_H_
#define _PIPECORE_H_

typedef struct Pipe Pipe;

void* pipecorepair(void);
long  pipecoreread(void*, int end, void*, long);
long  pipecorewrite(void*, int end, void*, long);
long  pipecorewritequiet(void*, int end, void*, long);
void  pipecoreclose(void*, int end);

#endif
