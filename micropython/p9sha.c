/*
 * SHA-256 from libsec, for p9space.c: in a file of its own, apart from
 * MicroPython's headers (mp.h's and libsec.h's names).
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

void
p9sha256(uchar *p, ulong n, uchar *digest)
{
	sha2_256(p, n, digest, nil);
}

/* Two-span SHA-256 for the O-Envelope domain separator and canonical CBOR. */
int
p9sha256parts(const void *a, usize na, const void *b, usize nb, uchar *digest)
{
 SHA2_256state state;
 memset(&state,0,sizeof state);
 sha2_256((uchar*)a,na,nil,&state);
 sha2_256((uchar*)b,nb,digest,&state);
 memset(&state,0,sizeof state);
 return 0;
}
