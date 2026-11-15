/* Host-only ABI regression test: cc -std=c11 -Wall -Wextra -Werror
 * -fsanitize=address,undefined runtime/objtail.c tools/host/test-objtail.c
 * -Iruntime -o /tmp/test-objtail && /tmp/test-objtail
 */
#include "objtail.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    unsigned char object[5 + 3 + ObjTailSize] =
        { 'H', 'e', 'l', 'l', 'o', 0xa1, 0x61, 'v' };
    unsigned char invalid[ObjTailSize];
    uint64_t raw = 99, meta = 99;
    assert(objtail_pack(object + 8, 5, 3) == 0);
    assert(memcmp(object, "Hello", 5) == 0);
    assert(objtail_parse(object + 8, sizeof object, &raw, &meta) == 0);
    assert(raw == 5 && meta == 3);
    assert(objtail_parse(object + 8, sizeof object - 1, &raw, &meta) < 0);
    memcpy(invalid, object + 8, sizeof invalid);
    invalid[0] ^= 1;
    assert(objtail_parse(invalid, sizeof object, &raw, &meta) < 0);
    memcpy(invalid, object + 8, sizeof invalid);
    invalid[12] = 1;
    assert(objtail_parse(invalid, sizeof object, &raw, &meta) < 0);
    memcpy(invalid, object + 8, sizeof invalid);
    invalid[11] = 2;
    assert(objtail_parse(invalid, sizeof object, &raw, &meta) < 0);
    memcpy(invalid, object + 8, sizeof invalid);
    invalid[23] = 0xff;
    assert(objtail_parse(invalid, sizeof object, &raw, &meta) < 0);
    assert(objtail_pack(invalid, UINT64_MAX, 1) < 0);
    assert(objtail_pack(invalid, 0, 0) == 0);
    assert(objtail_parse(invalid, ObjTailSize, &raw, &meta) == 0);
    assert(raw == 0 && meta == 0);
    assert(objtail_parse(invalid, ObjTailSize - 1, &raw, &meta) < 0);
    return 0;
}
