/*
 * Minimal self-contained memory sourced process used by the manifest.
 * Its image is embedded in the kernel, not fetched from any volume.
 * Deliberately no recovery-console claim: this is a process isolation
 * acceptance fixture.  The executable invokes only the native EXITS ABI.
 */
#include <u.h>
extern void exits(char*);

void
_main(void *arg)
{
	(void)arg;
	exits("memory-selftest-done");
	for(;;)
		;
}
