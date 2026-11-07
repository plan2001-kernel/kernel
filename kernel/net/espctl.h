/* Internal kernel entry points, not a userspace syscall ABI. */
extern int espctl_init(void);
extern void espctl_bootstrap(Proc*);
extern int espctl_output(Fs *,Block **,int,int);
extern int espctl_plain(Fs *,Block *);
