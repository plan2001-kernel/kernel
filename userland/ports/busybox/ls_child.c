/* Stage 6: native external ls process.
 * Reads a ONE-SHOT directory snapshot from an explicit delegated RO
 * region. Its stdout is another, separately delegated RW region consumed
 * by the launcher via compat.c. This does not open a path, volume or OID.
 * A snapshot conveys names only, not ObjectSpace object capabilities.
 */
#include "libbb.h"
#include "obj.h"
#include "vobj.h"
extern long obj_list(void *records, unsigned long long count);
extern long obj_region_map(int handle, unsigned long long addr);
extern long obj_region_unmap(unsigned long long addr);

#define LS_IMAGE_BYTES 4096u
#define LS_DATA_BYTES (LS_IMAGE_BYTES - 8u)

int p2001_ls_main(int argc, char **argv)
{
    VobjListRec handles[16];
    unsigned long long base = 0, length = 0;
    unsigned int i, j;
    int reader = -1;
    long count, addr;
    const unsigned char *image;
    if (argc > 2 || argc < 1 || !argv) return 64;
    if (argc == 2 && (!argv[1] || argv[1][0] != '/' ||
                      argv[1][1] != 0))
        return 64; /* the stage-6 external fixture provides ONLY root */
    count = obj_list(handles, 16);
    if (count < 0 || count > 16) return 1;
    for (i = 0; i < (unsigned int)count; ++i) {
        if (handles[i].kind == VkindRegion &&
            (handles[i].rights & Vread) &&
            !(handles[i].rights & Vwrite)) {
            if (reader >= 0) return 1;
            reader = (int)handles[i].handle;
        }
    }
    if (reader < 0) return 1;
    addr = obj_region_map(reader, 0);
    if (addr < 0) return 1;
    base = (unsigned long long)addr;
    image = (const unsigned char *)base;
    length = __atomic_load_n((const unsigned long long *)image,
                              __ATOMIC_ACQUIRE);
    if (length > LS_DATA_BYTES) {
        (void)obj_region_unmap(base);
        return 1;
    }
    /* Validate names before they reach an output capability. Reject control
     * bytes, slash, multiple consecutive line terminators and missing LF.
     */
    for (j = 0; j < (unsigned int)length; ++j) {
        unsigned char c = image[8+j];
        if ((c < 32 && c != '\n') || c >= 127 || c == '/') {
            (void)obj_region_unmap(base);
            return 1;
        }
        if (c == '\n' && (j == 0 || image[8+j-1] == '\n')) {
            (void)obj_region_unmap(base);
            return 1;
        }
    }
    if (length && (image[8+length-1] != '\n' ||
        full_write(1, image+8, (size_t)length) != (ssize_t)length)) {
        (void)obj_region_unmap(base);
        return 1;
    }
    (void)obj_region_unmap(base);
    return 0;
}
