#include <u.h>
#include "fns.h"
#include "efi.h"

typedef struct {
	UINT64		Revision;
	void		*Open;
	void		*Close;
	void		*Delete;
	void		*Read;
	void		*Write;
	void		*GetPosition;
	void		*SetPosition;
	void		*GetInfo;
	void		*SetInfo;
	void		*Flush;
	void		*OpenEx;
	void		*ReadEx;
	void		*WriteEx;
	void		*FlushEx;
} EFI_FILE_PROTOCOL;

typedef struct {
	UINT64		Revision;
	void		*OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

static
EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {
	0x0964e5b22, 0x6459, 0x11d2,
	0x8e, 0x39, 0x00, 0xa0,
	0xc9, 0x69, 0x72, 0x3b,
};

static EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
	0x5b1b31a1, 0x9562, 0x11d2,
	0x8e, 0x3f, 0x00, 0xa0,
	0xc9, 0x69, 0x72, 0x3b,
};

static
EFI_FILE_PROTOCOL *fsroot;

static void
towpath(CHAR16 *w, int nw, char *s)
{
	int i;

	for(i=0; *s && i<nw-1; i++){
		*w = *s++;
		if(*w == '/')
			*w = '\\';
		w++;
	}
	*w = 0;
}

static void*
fsopen(char *name)
{
	CHAR16 wname[MAXPATH];
	EFI_FILE_PROTOCOL *fp;

	if(fsroot == nil)
		return nil;

	towpath(wname, MAXPATH, name);

	fp = nil;
	if(eficall(fsroot->Open, fsroot, &fp, wname, (UINT64)1, (UINT64)1))
		return nil;
	return fp;
}

/*
 * 4096 bytes/call (the stock upstream value) makes reading the kernel
 * (several MB) thousands of synchronous EFI File.Read calls; each one costs
 * real wall-clock time on slower real firmware. 64 KiB cuts that by 16x
 * without changing the read()/readn() interface at all - Read() already
 * returns however much it actually read in *size, short or not.
 */
/*
 * The directory the loader was read from ("/EFI/plan2001"), from its
 * LoadedImage's device path: the file path nodes (type 4, subtype 4,
 * UCS-2; firmware may split the path into several), up to the last
 * separator.  "" if there is none: then only the volume's root.
 */
static void
imagedir(EFI_LOADED_IMAGE_PROTOCOL *image, char *dir, int ndir)
{
	uchar *p;
	CHAR16 *w;
	int len, i, n, slash;

	n = 0;
	p = image->FilePath;
	for(i = 0; p != nil && i < 64; i++){
		len = p[2] | p[3]<<8;
		if(p[0] == 0x7f || len < 4)
			break;
		if(p[0] == 4 && p[1] == 4){
			if(n > 0 && dir[n-1] != '/' && n < ndir-1)
				dir[n++] = '/';
			for(w = (CHAR16*)(p+4); (uchar*)w < p+len && *w != 0 && n < ndir-1; w++)
				dir[n++] = *w == '\\' ? '/' : *w;
		}
		p += len;
	}
	dir[n] = 0;
	slash = -1;
	for(i = 0; i < n; i++)
		if(dir[i] == '/')
			slash = i;
	dir[slash > 0 ? slash : 0] = 0;
}

static int
fsread(void *f, void *data, int len)
{
	UINTN size;

	size = len > 64*1024 ? 64*1024 : len;
	if(eficall(((EFI_FILE_PROTOCOL*)f)->Read, f, &size, data))
		return 0;
	return (int)size;
}

static void
fsclose(void *f)
{
	eficall(((EFI_FILE_PROTOCOL*)f)->Close, f);
}

int
fsinit(void **pf)
{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
	EFI_LOADED_IMAGE_PROTOCOL *image;
	EFI_FILE_PROTOCOL *root;
	EFI_HANDLE *Handles;
	void *f;
	UINTN Count;
	int i;
	char path[MAXPATH];

	image = nil;

	/* locate kernel and plan9.ini by deriving a fs protocol
	 * from the device the loader was read from.
	 * if that fails, fall back to old method.
	 */
	if(eficall(ST->BootServices->HandleProtocol, IH,
		&EFI_LOADED_IMAGE_PROTOCOL_GUID, &image) == 0 &&
		eficall(ST->BootServices->HandleProtocol, image->DeviceHandle,
		&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, &fs) == 0 &&
		eficall(fs->OpenVolume, fs, &root) == 0){

		fsroot = root;
		/*
		 * Plan2001: plan9.ini by the loader first, so that it can
		 * live in a directory of another system's ESP
		 * (\EFI\plan2001\, chainloaded by GRUB), then at the root
		 */
		imagedir(image, path, sizeof path - 12);
		f = nil;
		if(path[0] != 0){
			memmove(path+strlen(path), "/plan9.ini", 11);
			f = fsopen(path);
		}
		if(f == nil)
			f = fsopen("/plan9.ini");
		if(f != nil){
			if(pf != nil)
				*pf = f;
			else
				fsclose(f);

			goto gotit;
		}
	}

	Count = 0;
	Handles = nil;
	if(eficall(ST->BootServices->LocateHandleBuffer,
		ByProtocol, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, nil, &Count, &Handles))
		return -1;

	/*
	 * assuming the ESP is the first entry in the handle buffer, so go backwards
	 * to scan for plan9.ini in other (9fat) filesystems first. if nothing is found
	 * we'll be defaulting to the ESP.
	 */
	fsroot = nil;
	for(i=Count-1; i>=0; i--){
		root = nil;
		fs = nil;
		if(eficall(ST->BootServices->HandleProtocol,
			Handles[i], &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, &fs))
			continue;
		if(eficall(fs->OpenVolume, fs, &root))
			continue;
		fsroot = root;
		f = fsopen("/plan9.ini");
		if(f != nil){
			if(pf != nil)
				*pf = f;
			else
				fsclose(f);
			break;
		}
		/*
		 * This volume has no plan9.ini: undo the fsroot assignment
		 * above before trying the next one. Without this, fsroot
		 * stays set to whatever volume was tried last even when
		 * none of them had plan9.ini, and the "if(fsroot == nil)"
		 * check just below never fires - fsinit() then reports
		 * success with *pf never actually set. Confirmed reachable:
		 * booting from an El Torito ISO9660 medium where no scanned
		 * SFS volume has /plan9.ini at its root.
		 */
		fsroot = nil;
	}
	if(fsroot == nil)
		return -1;

gotit:
	read = fsread;
	close = fsclose;
	open = fsopen;
	stop = nil;

	return 0;
}
