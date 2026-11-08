#include <bootinfo.h>

enum {
	MAXPATH = 128,
};

extern char hex[];

void usleep(int t);
/* the ISA side of the loader: archx64.c for bootx64.efi, archaa64.c for bootaa64.efi */
void archconf(BootInfo *bi);
uvlong archentry(uvlong entry);
ulong archdataround(void);
int archblobok(uvlong pa, uvlong len);
int archentryok(uvlong entry);
void archcacheclean(uvlong pa, uvlong len);
void archcachesync(void);
char *archcheck(void);
void archjump(void *entry, void *bootinfo);

int fsinit(void **pf);

void* (*open)(char *name);
int (*read)(void *f, void *data, int len);
void (*close)(void *f);
void (*stop)(void);

int readn(void *f, void *data, int len);
int bootmapinit(void);
int bootexit(void);
void *bootinfofinish(void);
int conflen(void);
int efialloc(uvlong pa, uvlong len);
void efifree(uvlong pa, uvlong len);
void fbmark(int stage);

extern char *confaddr;
extern char *logbuf;
extern int logcap, logused;

int getc(void);
void putc(int c);

void memset(void *p, int v, int n);
void memmove(void *dst, void *src, int n);
int memcmp(void *src, void *dst, int n);
int strlen(char *s);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
void print(char *s);

char *configure(void *f, char *path);
char *bootkern(void *f);
char *findconf(char*);

char *hexfmt(char *s, int i, uvlong a);
void tracehex(char *label, uvlong value);
char *decfmt(char *s, int i, ulong a);

#ifdef PLAN2001_EFI_GCC
uintptr eficall_n(void *proc, int n, ...);
#define EFI_NARG(...) EFI_NARG_(__VA_ARGS__, 12,11,10,9,8,7,6,5,4,3,2,1,0)
#define EFI_NARG_(...) EFI_NARG__(__VA_ARGS__)
#define EFI_NARG__(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,N,...) N
#define eficall(proc, ...) eficall_n((proc), EFI_NARG(__VA_ARGS__), __VA_ARGS__)
#else
uintptr eficall(void *proc, ...);
#endif
void eficonfig(void);
