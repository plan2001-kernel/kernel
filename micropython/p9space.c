/*
 * MicroPython persistent Space implementation (p9image.c): O-only.
 *
 * The native O path (-O VOLUME -S SPACE) calls the GE/O object interface
 * to load the Environment, immutable code objects and OID_GRAPH from the
 * volume (docs/O-FORMAT.md). The Python object graph is one serialized
 * OID_GRAPH object; logical Python OIDs and aliases are not independently
 * indexed GE/O edges. There is no pathname/file Space backend.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/compile.h"
#include "py/builtin.h"
#include "py/persistentcode.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/objexcept.h"
#include "p9space.h"
#include "p9console.h"

// Stage 4: volume bootstrap followed by OBJ Space handles
extern long ovolopen(char *, int);

/* Shared exact OSPAWN layout, including the optional extension. */
#ifndef NO_QSTR
#include "../runtime/abi/sys.h"
#include "../runtime/abi/vobj.h"
#include "../runtime/abi/obj.h"
#include "../runtime/abi/namespace.h"
#endif

// the runtime an Environment is made for: the .mpy's format
#define RUNTIME "mp-" MICROPY_VERSION_STRING "-mpy" MP_STRINGIFY(MPY_VERSION) "." MP_STRINGIFY(MPY_SUB_VERSION)
MP_DEFINE_STR_OBJ(space_runtime_obj, RUNTIME);

// the Environment of mp -E: (its name, {import name: definition})
MP_REGISTER_ROOT_POINTER(mp_obj_t p9_space_env);

// the modules loaded from the Environment, for the image: their
// context (globals, constants) and their bytecode's tree
MP_REGISTER_ROOT_POINTER(void *p9_space_mods);
static int nmods;

MP_DEFINE_EXCEPTION(CommitError, Exception)

// the open native O Space of mp -S
static struct {
    int isopen;
    char name[64];
    int writer;
    vlong n;		/* the graph version */
    uint64_t next;	/* the next OID */
} sp = { 0, "", 0, 0, 0 };
static vlong lastcommit;	/* p9_now() */

/*
 * Stage 4: the native O volume and the Space handle.  ovol is -1 until
 * mp -O selects one.  The Environment manifest's entries name a code
 * object by OID; odefs is their parsed table, used by loadmpy so that
 * no path or sys.path is ever consulted on this path.
 */
static int ovol = -1;
static int osp = -1;
static int spread = -1;	/* authorized VkindSpace Handle (OBJ read/write/commit) */
static int nativepath;
static int ofresh;	/* Stage 5: the O Space had no graph at open */

typedef struct {
    char def[65];	/* the definition (source SHA-256), its identity */
    char mpy[65];	/* the compiled code's SHA-256 */
    unsigned long long oid;
} odef_t;

static odef_t *odefs;
static int nodefs, aodefs;

static bool ishex(const char *s);

void p9_space_native_mode(int on) {
    nativepath = on;
}

// D2b/T4a: nsec() is the direct historical time syscall; both the native and
// fallback paths use the same pathless call.
vlong p9_now(void) {
    return nsec();
}

// $spacetime: the times of a restore and a commit on fd 2 (ms)
void p9_spacetime(const char *what, vlong t0) {
    static int on = -1;
    char *e;

    if (nativepath)
        return;
    if (on < 0) {
        e = getenv("spacetime");
        on = e != NULL;
        free(e);
    }
    if (on) {
        p9_dprintf("spacetime: %s %lld ms\n", what, (p9_now() - t0) / 1000000);
    }
}

static void addmod(qstr name, mp_module_context_t *ctx, mp_raw_code_t *rc) {
    p9_space_mod_t *m = MP_STATE_VM(p9_space_mods);

    m = m_renew(p9_space_mod_t, m, nmods, nmods + 1);
    MP_STATE_VM(p9_space_mods) = m;
    m[nmods].name = name;
    m[nmods].ctx = ctx;
    m[nmods].rc = rc;
    nmods++;
}

p9_space_mod_t *p9_space_mods(int *n) {
    *n = nmods;
    return MP_STATE_VM(p9_space_mods);
}

static mp_obj_t envitem(int i) {
    return ((mp_obj_tuple_t *)MP_OBJ_TO_PTR(MP_STATE_VM(p9_space_env)))->items[i];
}

void p9sha256(uchar *, ulong, uchar *);

static void raise_err(void) {
    char buf[ERRMAX];

    mp_handle_pending(true);
    rerrstr(buf, sizeof buf);
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), buf);
}

static void hexof(const void *p, size_t n, char *hex) {
    uchar d[32];
    int i;

    p9sha256((uchar *)p, n, d);
    for (i = 0; i < 32; i++) {
        snprint(hex + 2 * i, 3, "%02x", d[i]);
    }
}

/* ---- Stage 4: Environment and code from native O ---- */

enum {
	Obytemax = 1*1024*1024,	/* a whole-object read/write PoC limit */
	Oidexecfirst = 0x4f45584543000001ULL,
	Oidexecbad = 0x4f45584543000002ULL,
	Oidexecmp = 0x4f45584543000003ULL,
};

/* The canonical OBJ path: an authorized VkindSpace Handle from OBJOPEN
 * (ObjOpenSpace) and one OBJOPEN (ObjOpenGeSpace) capability per OID for
 * reads.  T2 stages writes to named OIDs through OBJINVOKE(ObjOpPutOid) and
 * commits them through OBJINVOKE(ObjOpCommit) on the same Space handle. */
static long objgeopen_space(int sh, unsigned long long oid) {
    ObjOpenGeSpaceReq req;

    memset(&req, 0, sizeof req);
    req.space = sh;
    req.oid = oid;
    return obj_open(ObjOpenGeSpace, &req, sizeof req, 0, NULL);
}

/* A whole persistent object through an already-open GE Handle: 0 ok,
 * -1 not there, -2 over max, -3 short/oversized read (a raised error
 * otherwise).  The caller owns and closes H. */
static int oreadge(int h, vstr_t *v, unsigned long long *ver, long max) {
    long n, r;
    unsigned long long x;

    vstr_init(v, 64);
    n = obj_read_version(h, nil, 0, 0, &x);
    if (n < 0) {
        vstr_clear(v);
        return -1;
    }
    if (n > max) {
        vstr_clear(v);
        return -2;
    }
    if (n > 0) {
        vstr_hint_size(v, n + 1);
        r = obj_read_version(h, v->buf, (unsigned long long)n, 0, &x);
        if (r != n) {
            vstr_clear(v);
            return -3;
        }
        v->len = n;
    }
    if (ver != NULL) {
        *ver = x;
    }
    return 0;
}

// a whole O object into v: 0 ok, -1 not there (a raised error otherwise)
static int oreadobj(unsigned long long oid, vstr_t *v, unsigned long long *ver) {
    long h;
    int r;

    if (spread < 0) {
        vstr_init(v, 64);
        vstr_clear(v);
        return -1;
    }
    h = objgeopen_space(spread, oid);
    if (h < 0) {
        vstr_init(v, 64);
        vstr_clear(v);
        return -1;
    }
    r = oreadge(h, v, ver, Obytemax);
    obj_close(h);
    if (r == -2) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space: O: object exceeds the %d-byte OBJ read limit"),
            (int)Obytemax);
    }
    if (r == -3) {
        raise_err();
    }
    return r;
}

/* The T2 write path: the normal MicroPython persistence path stages and
 * commits through the authorized VkindSpace Handle opened for T1 (spread),
 * never through a raw OID alone.  PutOid replaces one explicitly named OID
 * (including OID_GRAPH/Environment/code before they are published) without
 * committing, so all writes of one commit stay in the same Space
 * transaction.  CreatePersist is the existing atomic allocate+payload+sync
 * operation and keeps the 1 MiB direct-object PoC limit.  The old explicit Ohand write wrappers have been retired. */
static void
objputoid_space(unsigned long long oid, void *buf, long n)
{
	if (spread < 0) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no OBJ Space write handle"));
	}
	if (obj_put_oid(spread, oid, buf, (unsigned long long)n) < 0) {
		raise_err();
	}
}

static void
objcommit_space(unsigned long long *ver)
{
	if (spread < 0) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no OBJ Space commit handle"));
	}
	if (obj_commit(spread, ver) < 0) {
		raise_err();
	}
}

static unsigned long long
objcreate_space(void *buf, long n)
{
	unsigned long long oid;

	if (spread < 0) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no OBJ Space create handle"));
	}
	if (obj_create_persist(spread, buf, (unsigned long long)n, &oid) < 0) {
		raise_err();
	}
	return oid;
}

static void odefs_clear(void) {
    free(odefs);
    odefs = NULL;
    nodefs = aodefs = 0;
}

// the definition DEF's code object, its digest checked; 0 ok, -1 no
// object, -2 its code is not the manifest's
static int oreadcode(const odef_t *d, vstr_t *v) {
    char hex[65];

    if (oreadobj(d->oid, v, NULL) < 0) {
        return -1;
    }
    hexof(v->buf, v->len, hex);
    if (strcmp(hex, d->mpy) != 0) {
        vstr_clear(v);
        return -2;
    }
    return 0;
}

static const odef_t *odef(const char *def) {
    int i;

    for (i = 0; i < nodefs; i++) {
        if (strcmp(odefs[i].def, def) == 0) {
            return &odefs[i];
        }
    }
    return NULL;
}

// a field of the manifest line: to the next space or newline
static int ofield(char **pp, char *out, int n) {
    char *p = *pp;
    int k = 0;

    while (*p != 0 && *p != ' ' && *p != '\n') {
        if (k >= n - 1) {
            return -1;
        }
        out[k++] = *p++;
    }
    out[k] = 0;
    *pp = p;
    return k > 0 ? 0 : -1;
}

/*
 * The Environment manifest, an O object (OID_ENV):
 *   runtime <id>
 *   <import name> <definition SHA-256> <mpy SHA-256> <code OID, 16 hex>
 * Its name is the SHA-256 of exactly its bytes (as an Environment's is
 * its text's).  EXPECT, if not nil, must be that name.
 */
static mp_obj_t oreadenv(char *envout, const char *expect) {
    char hex[65], *p, *e, name[128], def[65], mpy[65], oidhex[17];
    mp_obj_t d;
    vstr_t v;
    unsigned long long oid;
    char *q;

    if (oreadobj(OID_ENV, &v, NULL) < 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("space: O: no Environment manifest"));
    }
    hexof(v.buf, v.len, hex);
    if (expect != NULL && strcmp(hex, expect) != 0) {
        vstr_clear(&v);
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space: O: Environment %s, its manifest is %s"), expect, hex);
    }
    if (envout != NULL) {
        snprint(envout, 65, "%s", hex);
    }
    vstr_add_char(&v, 0);
    p = v.buf;
    if (strncmp(p, "runtime ", 8) != 0 || (e = strchr(p, '\n')) == NULL) {
        vstr_clear(&v);
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: O: bad Environment manifest"));
    }
    *e = 0;
    if (strcmp(p + 8, RUNTIME) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space: O: Environment runtime %s, this mp is %s"), p + 8, RUNTIME);
    }
    d = mp_obj_new_dict(0);
    odefs_clear();
    for (p = e + 1; *p != 0;) {
        q = p;
        if (ofield(&q, name, sizeof name) < 0 || *q != ' ') {
            goto bad;
        }
        q++;
        if (ofield(&q, def, sizeof def) < 0 || *q != ' ') {
            goto bad;
        }
        q++;
        if (ofield(&q, mpy, sizeof mpy) < 0 || *q != ' ') {
            goto bad;
        }
        q++;
        if (ofield(&q, oidhex, sizeof oidhex) < 0) {
            goto bad;
        }
        e = strchr(q, '\n');
        if (e == NULL) {
            e = q + strlen(q);
        }
        if (*q == ' ') {
            goto bad;
        }
        if (!ishex(def) || !ishex(mpy) || strlen(oidhex) != 16) {
            goto bad;
        }
        oid = strtoull(oidhex, NULL, 16);
        if (nodefs == aodefs) {
            aodefs = aodefs ? 2 * aodefs : 4;
            odefs = realloc(odefs, aodefs * sizeof odefs[0]);
            if (odefs == NULL) {
                aodefs = nodefs = 0;
                vstr_clear(&v);
                raise_err();
            }
        }
        snprint(odefs[nodefs].def, sizeof odefs[nodefs].def, "%s", def);
        snprint(odefs[nodefs].mpy, sizeof odefs[nodefs].mpy, "%s", mpy);
        odefs[nodefs].oid = oid;
        nodefs++;
        mp_obj_dict_store(d, mp_obj_new_str(name, strlen(name)), mp_obj_new_str(def, 64));
        p = *e == 0 ? e : e + 1;
    }
    vstr_clear(&v);
    return d;

bad:
    vstr_clear(&v);
    odefs_clear();
    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: O: bad Environment manifest line"));
}

// Stage 4's -O: the volume is opened now; the Space follows with -S
int p9_space_o(const char *spec) {
    long h;

    h = ovolopen((char *)spec, strlen(spec));
    if (h < 0) {
        char buf[ERRMAX];

        rerrstr(buf, sizeof buf);
        p9_dprintf("micropython: -O %s: %s\n", spec, buf);
        return -1;
    }
    ovol = (int)h;
    p9_dprintf("space: O volume selected: %s\n", spec);
    return 0;
}

static bool ishex(const char *s) {
    int i;

    for (i = 0; i < 64; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return s[64] == 0;
}

// hash(data) -> the SHA-256 of a str or bytes, hex
static mp_obj_t space_hash(mp_obj_t data) {
    mp_buffer_info_t b;
    char hex[65];

    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    hexof(b.buf, b.len, hex);
    return mp_obj_new_str(hex, 64);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_hash_obj, space_hash);

// useenv(env): import from the O Environment manifest ENV (mp -E)
static mp_obj_t space_useenv(mp_obj_t env_in) {
    char env[65];
    mp_obj_t items[2];

    if (MP_STATE_VM(p9_space_env) != MP_OBJ_NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("an Environment is in use"));
    }
    items[1] = oreadenv(env, mp_obj_str_get_str(env_in));
    items[0] = env_in;
    MP_STATE_VM(p9_space_env) = mp_obj_new_tuple(2, items);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_useenv_obj, space_useenv);

// env() -> the Environment in use (its name), None without one
static mp_obj_t space_env(void) {
    return MP_STATE_VM(p9_space_env) == MP_OBJ_NULL ? mp_const_none : envitem(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_env_obj, space_env);

// a module's bytecode run in its module (builtinimport.c's
// do_execute_proto_fun)
static void execmodule(mp_module_context_t *context, mp_proto_fun_t proto_fun) {
    mp_obj_dict_t *globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;

    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(globals);
    mp_locals_set(globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_call_function_0(mp_make_function_from_proto_fun(proto_fun, context, NULL));
    nlr_pop_jump_callback(true);
}

// module NAME's code object from the O Environment (ENVMAP), loaded
// into MOD, not run
static void loadmpy(mp_obj_t envmap, qstr name, mp_obj_t mod, mp_compiled_module_t *cm) {
    mp_map_elem_t *el;
    const odef_t *d;
    const char *def;
    char oid[24], path[300];
    vstr_t v;
    int r;

    el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(envmap))->map,
        mp_obj_new_str(qstr_str(name), qstr_len(name)), MP_MAP_LOOKUP);
    if (el == NULL) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no module named '%q' in the Environment"), name);
    }
    memset(cm, 0, sizeof *cm);
    cm->context = MP_OBJ_TO_PTR(mod);
    def = mp_obj_str_get_str(el->value);
    if ((d = odef(def)) == NULL) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space: O: %q: definition %s is not in the manifest"), name, def);
    }
    r = oreadcode(d, &v);
    snprint(oid, sizeof oid, "%llux", d->oid);
    if (r == -1) {
        mp_raise_msg_varg(&mp_type_OSError,
            MP_ERROR_TEXT("space: O: %q: no code object %s"), name, oid);
    }
    if (r == -2) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space: O: %q: code digest is not the manifest's"), name);
    }
    mp_raw_code_load_mem((const byte *)v.buf, v.len, cm);
    snprint(path, sizeof path, "<O:%s>", d->def);
    mp_store_attr(mod, MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(path)));
    p9_dprintf("space: O: %s code OID %s, digest %s\n", qstr_str(name), oid, d->mpy);
}

// the Environment's module NAME: a new module (in sys.modules) with
// its O code loaded, not run
static p9_space_mod_t *loadmod(qstr name) {
    mp_compiled_module_t cm;

    loadmpy(envitem(1), name, mp_obj_new_module(name), &cm);
    addmod(name, cm.context, (mp_raw_code_t *)cm.rc);
    return &((p9_space_mod_t *)MP_STATE_VM(p9_space_mods))[nmods - 1];
}

// the Environment's module NAME for a restore: the one in sys.modules,
// else loaded
p9_space_mod_t *p9_space_loadmod(qstr name) {
    p9_space_mod_t *m = MP_STATE_VM(p9_space_mods);
    mp_map_elem_t *el;
    int i;

    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: a module without an Environment"));
    }
    el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP);
    for (i = nmods - 1; el != NULL && i >= 0; i--) {
        if (m[i].name == name && MP_OBJ_FROM_PTR(m[i].ctx) == el->value) {
            return &m[i];
        }
    }
    return loadmod(name);
}

// import (mpconfigport.h: mp_builtin___import__): without an
// Environment MicroPython's; with one the builtin modules, then the
// Environment's, loaded once; a module whose top level fails is not
// left in sys.modules (CPython's way)
mp_obj_t p9_import(size_t n, const mp_obj_t *a) {
    mp_map_t *loaded = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el;
    qstr name;
    p9_space_mod_t *m;
    mp_module_context_t *ctx;
    nlr_buf_t nlr;

    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        return mp_builtin___import___default(n, a);
    }
    name = mp_obj_str_get_qstr(a[0]);
    if ((n > 4 && mp_obj_get_int(a[4]) != 0) || strchr(qstr_str(name), '.') != NULL) {
        mp_raise_msg(&mp_type_ImportError, MP_ERROR_TEXT("packages not supported"));
    }
    if (mp_module_get_builtin(name, false) != MP_OBJ_NULL || mp_module_get_builtin(name, true) != MP_OBJ_NULL) {
        return mp_builtin___import___default(n, a);
    }
    if ((el = mp_map_lookup(loaded, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP)) != NULL) {
        return el->value;
    }
    if (nlr_push(&nlr) == 0) {
        m = loadmod(name);
        ctx = m->ctx;
        execmodule(ctx, m->rc);
        nlr_pop();
    } else {
        mp_map_lookup(loaded, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP_REMOVE_IF_FOUND);
        nlr_jump(nlr.ret_val);
    }
    return MP_OBJ_FROM_PTR(ctx);
}

static void checkname(const char *name) {
    if (*name == 0 || strlen(name) >= sizeof sp.name || strchr(name, '/') != NULL
        || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not a space name: '%s'"), name);
    }
}

static void put64(byte *p, uint64_t u) {
    int i;

    for (i = 7; i >= 0; i--, u >>= 8) {
        p[i] = u;
    }
}

static uint64_t get64(const byte *p) {
    uint64_t u = 0;
    int i;

    for (i = 0; i < 8; i++) {
        u = u << 8 | p[i];
    }
    return u;
}

/*
 * The OID_GRAPH header's user part, byte-compatible with the stored form:
 *   V5/V6  72: env[64] next[8]
 *   V7     88: env[64] next[8] fmt[1] pad[7] pin[8]
 * fmt 2 says the pin is there (0: none).  The native O path reads the
 * Environment and next-OID fields and validates the format tag.
 */
enum {
    UserNext = 64,
    UserLen = 72,		/* V5/V6 */
    UserFmt = 72,
    UserPin = 80,
    UserLen7 = 88,		/* V7 */
    HdrFmt = 2,
};

typedef struct {
    vlong n;		/* the published version */
    vlong k;		/* the oldest kept */
    char env[65];
    uint64_t next;
    uint64_t pin;
    int fmt;
} shdr_t;

// a store's stream's header ('H' N[8] K[8] len[4] user); NULL: read
static const char *storehdr(const byte *p, size_t len, shdr_t *h, size_t *nuser) {
    size_t nu;

    if (len < 21 || p[0] != 'H') {
        return "not a Space's store";
    }
    h->n = get64(p + 1);
    h->k = get64(p + 9);
    nu = get64(p + 17) >> 32;
    if ((nu != UserLen && nu != UserLen7) || len < 21 + nu) {
        return "its header";
    }
    memmove(h->env, p + 21, 64);
    h->env[64] = 0;
    if (!ishex(h->env)) {
        return "its Environment";
    }
    h->next = get64(p + 21 + UserNext);
    if (h->next < OID_FIRST || h->next >= OID_UPGRADE_META) {
        return "its OIDs";
    }
    h->fmt = nu == UserLen7 ? p[21 + UserFmt] : 1;
    h->pin = nu == UserLen7 ? get64(p + 21 + UserPin) : 0;
    if (h->fmt != 1 && h->fmt != HdrFmt) {
        return "another header format";
    }
    *nuser = nu;
    return NULL;
}

// the O Environment in use: its manifest's map, its name verified
static void ouseenv(const char *name, const char *expect) {
    char env[65];
    mp_obj_t map;

    map = oreadenv(env, expect);
    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        mp_obj_t items[2];

        items[0] = mp_obj_new_str(env, 64);
        items[1] = map;
        MP_STATE_VM(p9_space_env) = mp_obj_new_tuple(2, items);
    } else if (strcmp(mp_obj_str_get_str(envitem(0)), env) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("space %s: its Environment is %s, its manifest is %s"),
            name, mp_obj_str_get_str(envitem(0)), env);
    }
}

// Stage 4: a Space on the native O volume: its Environment from the
// manifest, its graph from OID_GRAPH, restored by the V7 machinery
static mp_obj_t ospace_open(size_t n_args, const mp_obj_t *args) {
    const char *name = mp_obj_str_get_str(args[0]);
    const char *why;
    char user[256];
    shdr_t h;
    vlong t0;
    uint64_t n;
    size_t nu;
    vstr_t st;
    nlr_buf_t nlr;

    if (n_args > 1 && args[1] != mp_const_none) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: O: versions are not supported"));
    }
    if ((spread = obj_open(ObjOpenSpace, (void *)name, strlen(name), 0, NULL)) < 0)
        raise_err();
    osp = spread;
    if (nlr_push(&nlr) != 0) {
        obj_close(spread);
        spread = -1;
        osp = -1;
        p9_image_reset();
        nlr_jump(nlr.ret_val);
    }
    t0 = p9_now();
    ofresh = 0;
    if (oreadobj(OID_GRAPH, &st, NULL) == 0) {
        if ((why = storehdr((const byte *)st.buf, st.len, &h, &nu)) != NULL) {
            vstr_clear(&st);
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space: O: %s: %s"), name, why);
        }
        ouseenv(name, h.env);
        nu = sizeof user;
        p9_image_restore_store((const byte *)st.buf, st.len, &n, user, &nu);
        p9_spacetime("restore", t0);
        sp.n = h.n;
        sp.next = get64((byte *)user + UserNext);
        vstr_clear(&st);
        p9_dprintf("space: O: restored %s version %lld, Environment %s\n",
            name, sp.n, h.env);
    } else {
        ofresh = 1;
        ouseenv(name, NULL);
        sp.n = 0;
        sp.next = OID_FIRST;
        p9_dprintf("space: O: %s has no graph, Environment %s\n",
            name, mp_obj_str_get_str(envitem(0)));
    }
    nlr_pop();
    strcpy(sp.name, name);
    sp.writer = getpid();
    sp.isopen = 1;
    lastcommit = p9_now();
    return mp_const_none;
}

/* ---- Stage 5: the O-only init Space's automatic run() ---- */

#define OINIT_MODULE "odemo"

static mp_obj_t globget(mp_obj_t dict, qstr q) {
    mp_map_elem_t *el;

    el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(dict))->map,
        MP_OBJ_NEW_QSTR(q), MP_MAP_LOOKUP);
    if (el == NULL) {
        mp_raise_msg_varg(&mp_type_KeyError, MP_ERROR_TEXT("%q"), q);
    }
    return el->value;
}

/*
 * Stage 5: -O ... -S init -N/-I.  A fresh image imports the demo module
 * from O (its top level runs once), calls its init(), and stores run as
 * a persistent global in the Space's __main__.  Later boots restore that
 * global; the module's top level is not run again.  A failure prints the
 * exception and returns -1, so main.c does not commit.
 */
int p9_space_boot(void) {
    nlr_buf_t nlr;
    qstr modname, qrun, qinit;
    p9_space_mod_t *m;
    mp_obj_t globals, run, res;

    if (ovol < 0 || !sp.isopen) {
        p9_dprintf("init: no O Space is open\n");
        return -1;
    }
    if (nlr_push(&nlr) == 0) {
        modname = qstr_from_str(OINIT_MODULE);
        qrun = qstr_from_str("run");
        qinit = qstr_from_str("init");
        if (ofresh) {
            mp_obj_t mn = mp_obj_new_str(OINIT_MODULE, strlen(OINIT_MODULE));

            p9_import(1, &mn);	/* the one time the top level runs */
            m = p9_space_loadmod(modname);
            globals = MP_OBJ_FROM_PTR(m->ctx->module.globals);
            mp_call_function_0(globget(globals, qinit));
            run = globget(globals, qrun);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main)),
                MP_OBJ_NEW_QSTR(qrun), run);
            p9_dprintf("space: O: init initialized from O code (%s)\n", OINIT_MODULE);
        } else {
            m = p9_space_loadmod(modname);
            globals = MP_OBJ_FROM_PTR(m->ctx->module.globals);
            run = globget(MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main)), qrun);
            p9_dprintf("space: O: init restored (%s), no module initialization\n", OINIT_MODULE);
        }
        res = mp_call_function_0(run);
        p9_dprintf("init: run() -> %d\n", mp_obj_get_int(res));
        nlr_pop();
        return 0;
    }
    mp_printf(&mp_plat_print, "init: run failed: ");
    mp_obj_print_helper(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
    mp_printf(&mp_plat_print, "\ninit: failed, no commit\n");
    return -1;
}

// Stage 5, after the commit: the persistent Counter's value, alias and
// OID (the same instance and alias on every boot)
int p9_space_report(void) {
    nlr_buf_t nlr;
    p9_space_mod_t *m;
    mp_obj_t globals, shared, alias, n;
    uint64_t soid, aoid;

    if (nlr_push(&nlr) == 0) {
        m = p9_space_loadmod(qstr_from_str(OINIT_MODULE));
        globals = MP_OBJ_FROM_PTR(m->ctx->module.globals);
        shared = globget(globals, qstr_from_str("shared"));
        alias = globget(globals, qstr_from_str("alias"));
        n = mp_load_attr(shared, qstr_from_str("n"));
        soid = p9_image_oid(MP_OBJ_TO_PTR(shared));
        aoid = p9_image_oid(MP_OBJ_TO_PTR(alias));
        p9_dprintf("init: shared Counter n %d, alias %s, OID %llux (alias %llux)\n",
            mp_obj_get_int(n), shared == alias ? "same" : "different", soid, aoid);
        nlr_pop();
        return 0;
    }
    mp_printf(&mp_plat_print, "init: report failed: ");
    mp_obj_print_helper(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
    mp_printf(&mp_plat_print, "\n");
    return -1;
}

// open(name[, version]): open the native O Space NAME.  Versions are
// not supported by the O path; no pathname/file Space is opened.
static mp_obj_t space_open(size_t n_args, const mp_obj_t *args) {
    const char *name = mp_obj_str_get_str(args[0]);

    if (sp.isopen) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("a Space is open"));
    }
    checkname(name);
    if (ovol < 0) {
        mp_raise_msg(&mp_type_OSError,
            MP_ERROR_TEXT("space: no native O volume is selected"));
    }
    return ospace_open(n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(space_open_obj, 1, 2, space_open);

// oid(o) -> o's OID in the open Space (kept over versions and boots),
// or None: not committed yet, or no object of the heap
static mp_obj_t space_oid(mp_obj_t o) {
    uint64_t oid;

    if (!mp_obj_is_obj(o) || (oid = p9_image_oid(MP_OBJ_TO_PTR(o))) == 0) {
        return mp_const_none;
    }
    return mp_obj_new_int_from_ull(oid);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_oid_obj, space_oid);

/*
 * Direct native GE/O byte objects (docs/OBJECT-MODEL.md, docs/O-FORMAT.md):
 * oget/oput address one published O object by OID, not a Python logical
 * OID and not the OID_GRAPH payload.  Reads go through the canonical
 * OBJOPEN/OBJINVOKE/OBJCLOSE boundary.  The automatic persistent path uses
 * OBJINVOKE(ObjOpPutOid/ObjOpCommit/ObjOpCreatePersist) on the authorized
 * Space handle (T2).
 * A numeric value may coincide with a Python graph OID; the two namespaces
 * are independent.
 */
static void
obytespace(void)
{
	if (ovol < 0 || osp < 0) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no native O Space is open"));
	}
}

static void
obyteopen(void)
{
	obytespace();
	if (!sp.isopen) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no native O Space is open"));
	}
}

static uint64_t
obyteoid(mp_obj_t o)
{
	mp_uint_t u;

	if (!mp_obj_is_int(o)) {
		mp_raise_TypeError(MP_ERROR_TEXT("space: OID must be an int"));
	}
	u = mp_obj_get_uint(o);	/* negative or too wide: OverflowError */
	return (uint64_t)u;
}

static int
obytereserved(uint64_t oid)
{
	if (oid <= OID_FIRST) {		/* 0, the roots record, the first object */
		return 1;
	}
	if (oid >= OID_CODE) {		/* code OIDs, OID_ENV, OID_GRAPH, metadata */
		return 1;
	}
	return oid >= Oidexecfirst && oid <= Oidexecmp;
}

uint64_t
p9_space_create_fun_code(const byte *p, size_t n)
{
	unsigned long long oid;

	obyteopen();
	if (n > Obytemax) {
		mp_raise_msg_varg(&mp_type_ValueError,
			MP_ERROR_TEXT("space: REPL function code exceeds the %d-byte O limit"),
			(int)Obytemax);
	}
	oid = objcreate_space((void *)p, (long)n);
	if (oid < 64 || oid >= OID_CODE || obytereserved(oid)) {
		mp_raise_msg(&mp_type_ValueError,
			MP_ERROR_TEXT("space: invalid native OID for REPL function code"));
	}
	return oid;
}

void
p9_space_read_fun_code(uint64_t oid, vstr_t *v)
{
	/* Restore runs after the O handle opens but before the Space is published. */
	obytespace();
	if (oid < 64 || oid >= OID_CODE || obytereserved(oid)) {
		mp_raise_msg(&mp_type_ValueError,
			MP_ERROR_TEXT("space: corrupt REPL function code OID"));
	}
	if (oreadobj(oid, v, NULL) < 0) {
		mp_raise_msg(&mp_type_ValueError,
			MP_ERROR_TEXT("space: missing REPL function code object"));
	}
}

static mp_obj_t
space_oput(mp_obj_t oidobj, mp_obj_t dataobj)
{
	mp_buffer_info_t b;
	unsigned long long ver;
	uint64_t oid;

	obyteopen();
	oid = obyteoid(oidobj);
	if (obytereserved(oid) || p9_image_fun_code_oid(oid)) {
		mp_raise_msg(&mp_type_ValueError,
			MP_ERROR_TEXT("space.oput: OID is reserved or is REPL function code"));
	}
	if (mp_obj_is_str(dataobj) || !mp_get_buffer(dataobj, &b, MP_BUFFER_READ)) {
		mp_raise_TypeError(MP_ERROR_TEXT("space.oput: data must be bytes-like"));
	}
	if (b.len > Obytemax) {
		mp_raise_msg_varg(&mp_type_ValueError,
			MP_ERROR_TEXT("space.oput: %d bytes over the %d-byte PoC limit"),
			(int)b.len, (int)Obytemax);
	}
	objputoid_space(oid, b.buf, (long)b.len);
	/* publish and sync now: no graph dirty flag or later commit waits */
	objcommit_space(&ver);
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(space_oput_obj, space_oput);

static mp_obj_t
space_oget(mp_obj_t oidobj)
{
	unsigned long long ver;
	uint64_t oid;
	long h, n, r;
	byte *buf;
	mp_obj_t o;
	int i;

	obyteopen();
	if (spread < 0) {
		mp_raise_msg(&mp_type_OSError,
			MP_ERROR_TEXT("space: no OBJ Space read handle"));
	}
	oid = obyteoid(oidobj);
	/* a bounded size/read retry: a concurrent commit may change the size */
	for (i = 0; i < 3; i++) {
		h = objgeopen_space(spread, oid);
		if (h < 0) {
			mp_raise_msg(&mp_type_OSError,
				MP_ERROR_TEXT("space.oget: no such O object"));
		}
		n = obj_read_version(h, nil, 0, 0, &ver);
		if (n < 0) {
			obj_close(h);
			mp_raise_msg(&mp_type_OSError,
				MP_ERROR_TEXT("space.oget: no such O object"));
		}
		if (n > Obytemax) {
			obj_close(h);
			mp_raise_msg_varg(&mp_type_ValueError,
				MP_ERROR_TEXT("space.oget: %d bytes over the %d-byte PoC limit"),
				(int)n, (int)Obytemax);
		}
		if (n == 0) {
			obj_close(h);
			return mp_const_empty_bytes;
		}
		buf = m_new(byte, n);
		r = obj_read_version(h, buf, (unsigned long long)n, 0, &ver);
		obj_close(h);
		if (r == n) {
			o = mp_obj_new_bytes(buf, n);
			m_del(byte, buf, n);
			return o;
		}
		m_del(byte, buf, n);
	}
	mp_raise_msg(&mp_type_OSError,
		MP_ERROR_TEXT("space.oget: object changed while reading"));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_oget_obj, space_oget);

/*
 * oregister([data]) -> a fresh local native OID.  Allocation and the
 * initial payload are one native Space transaction; the native layer
 * commits and syncs before this returns.  It is a local numeric OID,
 * not a Python graph OID and not a globally unique identifier.
 */
static mp_obj_t
space_oregister(size_t n_args, const mp_obj_t *args)
{
	mp_buffer_info_t b;
	unsigned long long oid;

	obyteopen();
	b.buf = nil;
	b.len = 0;
	if (n_args > 0) {
		if (mp_obj_is_str(args[0]) ||
		    !mp_get_buffer(args[0], &b, MP_BUFFER_READ)) {
			mp_raise_TypeError(MP_ERROR_TEXT("space.oregister: data must be bytes-like"));
		}
		if (b.len > Obytemax) {
			mp_raise_msg_varg(&mp_type_ValueError,
				MP_ERROR_TEXT("space.oregister: %d bytes over the %d-byte PoC limit"),
				(int)b.len, (int)Obytemax);
		}
	}
	oid = objcreate_space(b.buf, (long)b.len);
	return mp_obj_new_int_from_ull(oid);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(space_oregister_obj, 0, 1, space_oregister);

// info() -> (name, version, Environment) of the open Space, or None
static mp_obj_t space_info(void) {
    mp_obj_t t[3];

    if (!sp.isopen) {
        return mp_const_none;
    }
    t[0] = mp_obj_new_str(sp.name, strlen(sp.name));
    t[1] = mp_obj_new_int_from_ll(sp.n);
    t[2] = envitem(0);
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_info_obj, space_info);

/*
 * spawn(oid, cpu=None, argv=None, membudget=0) -> VkindProcess Handle.
 * The Handle carries Vobserve|Vcontrol|Vmeta|Vdelegate and is the process
 * authority; a numeric PID is only an identifier obtainable by observation.
 * The Space must be open; oid selects its published executable bytes.
 * cpu is an exact logical kernel CPU, or None/-1 for unpinned.  argv
 * is an iterable of str, at most 32 entries.  membudget is bytes; 0
 * inherits the parent's budget (0 there means unlimited).  A syscall
 * rejection raises OSError with the kernel's error string.
 */
static mp_obj_t space_spawn(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_oid, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_cpu, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_argv, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_membudget, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_handle, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_view, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_roles, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_pipes, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_env, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_stdout, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_vhandles, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    struct {
        mp_arg_val_t oid, cpu, argv, membudget, handle, view, roles, pipes, env, stdout, vhandles;
    } a;
    struct { struct Ospawn base; struct Oinherit extra; } ext;
    struct Oinheritv *hv;
    int hasv;
    char *argv[33];
    const char *s;
    mp_obj_t it, item;
    unsigned long long oid, mb;
    size_t total;
    long pid;
    int argc, i;

    mp_arg_parse_all(n_args, args, kw_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, (mp_arg_val_t *)&a);

    if (!mp_obj_is_int(a.oid.u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("space.spawn: oid must be an int"));
    }
    oid = (unsigned long long)mp_obj_get_uint(a.oid.u_obj);

    memset(&ext, 0, sizeof ext);
    if(a.handle.u_obj == mp_const_none) obyteopen();
    ext.base.cpu = ~0ULL;
    if (a.cpu.u_obj != mp_const_none) {
        int cpu = mp_obj_get_int(a.cpu.u_obj);
        if (cpu == -1) {
            /* unpinned */
        } else if (cpu < 0) {
            mp_raise_msg(&mp_type_ValueError,
                MP_ERROR_TEXT("space.spawn: cpu must be >= 0, -1 or None"));
        } else {
            ext.base.cpu = (unsigned long long)cpu;
        }
    }

    argc = 0;
    total = 0;
    if (a.argv.u_obj != mp_const_none) {
        it = mp_getiter(a.argv.u_obj, NULL);
        while ((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            if (argc >= 32) {
                mp_raise_msg(&mp_type_ValueError,
                    MP_ERROR_TEXT("space.spawn: at most 32 argv entries"));
            }
            if (!mp_obj_is_str(item)) {
                mp_raise_TypeError(
                    MP_ERROR_TEXT("space.spawn: argv must contain str"));
            }
            s = mp_obj_str_get_str(item);
            total += strlen(s) + 1;
            if (total > 16 * 1024) {
                mp_raise_msg(&mp_type_ValueError,
                    MP_ERROR_TEXT("space.spawn: argv is too long"));
            }
            argv[argc++] = (char *)s;
        }
    }
    argv[argc] = NULL;

    if (a.membudget.u_obj == mp_const_none) {
        mb = 0;
    } else {
        /* negative values raise OverflowError in mp_obj_get_uint */
        mb = (unsigned long long)mp_obj_get_uint(a.membudget.u_obj);
    }
    ext.base.membudget = mb;
    hasv = a.vhandles.u_obj != mp_const_none;
    hv = (struct Oinheritv *)&ext.extra;
    if (hasv) {
        if (a.view.u_int != 0
        || a.stdout.u_obj != mp_const_none || a.roles.u_obj != mp_const_none
        || a.pipes.u_obj != mp_const_none || a.env.u_obj != mp_const_none) {
            mp_raise_msg(&mp_type_ValueError,
                MP_ERROR_TEXT("space.spawn: vhandles cannot be combined with other inheritance"));
        }
        memset(hv, 0, sizeof *hv);
        it = mp_getiter(a.vhandles.u_obj, NULL);
        while ((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            mp_obj_t handle_item = item;
            unsigned long long selected_rights = 0;
            if (!mp_obj_is_int(item)) {
                size_t pairlen; mp_obj_t *pair;
                mp_obj_get_array(item, &pairlen, &pair);
                if(pairlen != 2) mp_raise_ValueError(MP_ERROR_TEXT("vhandles: expected handle or (handle, rights)"));
                handle_item = pair[0];
                selected_rights = (unsigned long long)mp_obj_get_int(pair[1]);
            }
            if (hv->nhandles >= 8) {
                mp_raise_msg(&mp_type_ValueError,
                    MP_ERROR_TEXT("space.spawn: at most 8 vhandles"));
            }
            hv->h[hv->nhandles].handle = (unsigned long long)mp_obj_get_int(handle_item);
            hv->h[hv->nhandles].rights = selected_rights;
            hv->nhandles++;
        }
    }
    ext.base.flags = hasv ? 2 : 1;
    if (!hasv) {
    ext.extra.view = a.view.u_int;
    if(a.stdout.u_obj != mp_const_none) ext.extra.stdoutpipe = mp_obj_get_uint(a.stdout.u_obj);
    if(a.roles.u_obj != mp_const_none){
        it = mp_getiter(a.roles.u_obj, NULL);
        while((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION){
            s = mp_obj_str_get_str(item);
            if(ext.extra.nroles == 32 || strlen(s) >= 32)
                mp_raise_ValueError(MP_ERROR_TEXT("spawn role limit"));
            strcpy(ext.extra.roles[ext.extra.nroles++], s);
        }
    }
    if(a.pipes.u_obj != mp_const_none){
        it = mp_getiter(a.pipes.u_obj, NULL);
        while((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION){
            size_t count; mp_obj_t *pair;
            mp_obj_get_array(item, &count, &pair);
            if(count != 2 || ext.extra.npipes == 16)
                mp_raise_ValueError(MP_ERROR_TEXT("spawn pipe limit"));
            ext.extra.pipes[ext.extra.npipes][0] = mp_obj_get_int(pair[0]);
            ext.extra.pipes[ext.extra.npipes++][1] = mp_obj_get_int(pair[1]);
        }
    }
    if(a.env.u_obj != mp_const_none){
        it = mp_getiter(a.env.u_obj, NULL);
        while((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION){
            size_t count; mp_obj_t *pair;
            mp_obj_get_array(item, &count, &pair);
            if(count != 2 || ext.extra.nenv == 8)
                mp_raise_ValueError(MP_ERROR_TEXT("spawn env limit"));
            const char *key = mp_obj_str_get_str(pair[0]);
            const char *val = mp_obj_str_get_str(pair[1]);
            if(strlen(key) >= 32 || strlen(val) >= 128)
                mp_raise_ValueError(MP_ERROR_TEXT("spawn env size"));
            strcpy(ext.extra.env[ext.extra.nenv].name, key);
            strcpy(ext.extra.env[ext.extra.nenv++].value, val);
        }
    }
    }

    {
        int spaceh = a.handle.u_obj == mp_const_none
            ? spread : mp_obj_get_int(a.handle.u_obj);
        if (spaceh < 0) {
            mp_raise_msg(&mp_type_OSError,
                MP_ERROR_TEXT("space.spawn: no open O Space"));
        }
        pid = obj_ospawn(spaceh, oid, argv, argc, &ext.base);
    }
    if (pid < 0) {
        raise_err();
    }
    return mp_obj_new_int(pid);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(space_spawn_obj, 1, space_spawn);

// wait() -> (handle, status): status '' if the process exited cleanly
static mp_obj_t space_wait(void) {
    Waitmsg *w;
    mp_obj_t t[2];

    p9_space_rest();
    if ((w = wait()) == nil) {
        raise_err();
    }
    t[0] = mp_obj_new_int(w->pid);
    t[1] = mp_obj_new_str(w->msg, strlen(w->msg));
    free(w);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_wait_obj, space_wait);

// terminate(handle): authorized termination through a Vcontrol Handle
static mp_obj_t space_terminate(mp_obj_t handle) {
    if (!mp_obj_is_int(handle)) {
        mp_raise_TypeError(MP_ERROR_TEXT("space.terminate: handle must be an int"));
    }
    if (obj_process_terminate(mp_obj_get_int(handle)) < 0) {
        raise_err();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_terminate_obj, space_terminate);

// commit points 3 (a rest) and space.atomic (the Space commit-point semantics)
static mp_float_t interval = 1.0;
static int atomicdepth, atomicpending;

static int iswriter(void) {
    return sp.isopen && getpid() == sp.writer;
}

/*
 * Stage 4: a commit to O.  p9_image_commit's changed-object records are
 * turned into the OID_GRAPH byte stream and
 * written as one O object, OID_GRAPH; the Environment manifest and the
 * immutable code objects are rewritten in the same transaction so that
 * the O engine's history sweep can never drop them.  This is a PoC
 * simplification: the graph is rewritten whole, not per object.
 */
typedef struct {
    vstr_t objs;	/* 'o' records of the new stream */
} ocommit_t;

static int ocommit_rec(vstr_t *rec, void *a) {
    ocommit_t *c = a;
    byte *p = (byte *)rec->buf, *e = p + rec->len;
    byte b[8];
    vlong len;
    uint64_t oid;

    while (p < e) {
        switch (*p++) {
        case 'p':
            if (e - p < 16) {
                return -1;
            }
            oid = get64(p);
            len = (vlong)get64(p + 8);
            p += 16;
            if (len < 0 || len > e - p) {
                return -1;
            }
            vstr_add_byte(&c->objs, 'o');
            put64(b, oid);
            vstr_add_strn(&c->objs, (char *)b, 8);
            put64(b, sp.n + 1);
            vstr_add_strn(&c->objs, (char *)b, 8);
            put64(b, len);
            vstr_add_strn(&c->objs, (char *)b, 8);
            vstr_add_strn(&c->objs, (char *)p, len);
            p += len;
            break;
        case 'd':
            if (e - p < 8) {
                return -1;
            }
            p += 8;	/* the old graph is not carried over */
            break;
        default:
            return -1;
        }
    }
    return 0;
}

static void ocommit(void) {
    ocommit_t c;
    vstr_t out, env, code;
    char user[UserLen];
    byte b[8];
    unsigned long long nver;
    vlong v;
    int i, r;

    v = sp.n + 1;
    vstr_init(&c.objs, 4096);
    p9_image_all_dirty();
    r = p9_image_commit(&sp.next, ocommit_rec, &c, 1);
    if (r < 0) {
        vstr_clear(&c.objs);
        raise_err();
    }
    if (r == 0) {
        vstr_clear(&c.objs);
        return;
    }
    vstr_init(&out, 21 + UserLen + c.objs.len);
    vstr_add_byte(&out, 'H');
    put64(b, v);
    vstr_add_strn(&out, (char *)b, 8);
    put64(b, v >= 3 ? v - 2 : 1);
    vstr_add_strn(&out, (char *)b, 8);
    vstr_add_strn(&out, "\0\0\0\x48", 4);	/* 72 */
    memset(user, 0, sizeof user);
    memmove(user, mp_obj_str_get_str(envitem(0)), 64);
    put64((byte *)user + UserNext, sp.next);
    vstr_add_strn(&out, user, sizeof user);
    vstr_add_strn(&out, c.objs.buf, c.objs.len);
    vstr_clear(&c.objs);

    objputoid_space(OID_GRAPH, out.buf, out.len);
    vstr_clear(&out);

    /* the Environment and the code, current at every commit */
    if (oreadobj(OID_ENV, &env, NULL) == 0) {
        objputoid_space(OID_ENV, env.buf, env.len);
        vstr_clear(&env);
    }
    for (i = 0; i < nodefs; i++) {
        if (oreadcode(&odefs[i], &code) == 0) {
            objputoid_space(odefs[i].oid, code.buf, code.len);
            vstr_clear(&code);
        }
    }
    nver = 0;
    objcommit_space(&nver);
    sp.n = v;
    p9_dprintf("space: O: committed %s version %lld (O version %llud)\n",
        sp.name, sp.n, nver);
}

// a native O commit: OID_GRAPH and the Environment/code objects
static void commit(void) {
    ocommit();
    lastcommit = p9_now();
}

// a commit point 1 or 2 (FORCE: the program's end, atomic or not); in
// the writer only, not an rfork'd child.  A failure is printed
int p9_space_commit(int force) {
    nlr_buf_t nlr;

    if (!iswriter()) {
        return 0;
    }
    if (atomicdepth > 0 && !force) {
        atomicpending = 1;
        return 0;
    }
    if (nlr_push(&nlr) == 0) {
        commit();
        nlr_pop();
        return 0;
    }
    mp_printf(&mp_plat_print, "space: commit failed: ");
    mp_obj_print_helper(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
    mp_printf(&mp_plat_print, "\n");
    return -1;
}

// a rest (modplan9.c: before a call blocks): a commit if space.interval
// has passed since the last; a failure raises CommitError instead of
// blocking
void p9_space_rest(void) {
    if (!iswriter() || p9_now() - lastcommit < (vlong)(interval * 1e9)) {
        return;
    }
    if (atomicdepth > 0) {
        atomicpending = 1;
        return;
    }
    commit();
}

// with space.atomic(): the commit points within it deferred to the
// outermost's end
typedef struct {
    mp_obj_base_t base;
} atomic_t;
static const mp_obj_type_t atomic_type;
static const atomic_t atomic_obj = { { &atomic_type } };

static mp_obj_t atomic_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    return MP_OBJ_FROM_PTR(&atomic_obj);
}

static mp_obj_t atomic_enter(mp_obj_t self) {
    atomicdepth++;
    return self;
}
static MP_DEFINE_CONST_FUN_OBJ_1(atomic_enter_obj, atomic_enter);

static mp_obj_t atomic_exit(size_t n, const mp_obj_t *a) {
    (void)n;
    (void)a;
    if (atomicdepth > 0 && --atomicdepth == 0 && atomicpending) {
        atomicpending = 0;
        if (iswriter()) {
            commit();
        }
    }
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(atomic_exit_obj, 4, 4, atomic_exit);

static const mp_rom_map_elem_t atomic_locals[] = {
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&atomic_enter_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&atomic_exit_obj) },
};
static MP_DEFINE_CONST_DICT(atomic_locals_dict, atomic_locals);

static MP_DEFINE_CONST_OBJ_TYPE(
    atomic_type,
    MP_QSTR_atomic,
    MP_TYPE_FLAG_NONE,
    make_new, atomic_make_new,
    locals_dict, &atomic_locals_dict
    );

/* GE/O operations use the object ABI. */
extern long oexecute(int, unsigned long long, char**, int);
static void hcheck(long r) { if(r < 0) raise_err(); }
static mp_obj_t space_hclose(mp_obj_t h) {
    hcheck(obj_close(mp_obj_get_int(h))); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_hclose_obj,space_hclose);
static mp_obj_t space_hget(mp_obj_t h, mp_obj_t oid) {
    unsigned long long ver;
    vstr_t v;
    long r, size, gh;

    /* The explicit legacy Space Handle is converted through OBJOPEN
     * (ObjOpenGeSpace), which applies the same Ohand authority. */
    gh = objgeopen_space(mp_obj_get_int(h), obyteoid(oid));
    hcheck(gh);
    size = obj_read_version(gh, nil, 0, 0, &ver);
    if(size < 0){ obj_close(gh); hcheck(size); }
    if(size > Obytemax){
        obj_close(gh);
        mp_raise_ValueError(MP_ERROR_TEXT("object exceeds binding limit"));
    }
    if(size == 0){
        obj_close(gh);
        return mp_const_empty_bytes;
    }
    vstr_init_len(&v,size);
    r = obj_read_version(gh, v.buf, (unsigned long long)size, 0, &ver);
    obj_close(gh);
    if(r<0){ vstr_clear(&v); raise_err(); }
    v.len=r;
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(space_hget_obj,space_hget);
/* hexecute(handle, oid[, argv]): replace this process image in place.
 * argv is an optional iterable of str, bounded like space.spawn.  The
 * kernel copies the strings before the old image is replaced. */
static mp_obj_t space_hexecute(size_t n_args, const mp_obj_t *args) {
    char *argv[33];
    const char *s;
    mp_obj_t it, item;
    size_t total;
    int argc;

    argc = 0;
    total = 0;
    if (n_args >= 3 && args[2] != mp_const_none) {
        it = mp_getiter(args[2], NULL);
        while ((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            if (argc >= 32) {
                mp_raise_msg(&mp_type_ValueError,
                    MP_ERROR_TEXT("hexecute: at most 32 argv entries"));
            }
            if (!mp_obj_is_str(item)) {
                mp_raise_TypeError(
                    MP_ERROR_TEXT("hexecute: argv must contain str"));
            }
            s = mp_obj_str_get_str(item);
            total += strlen(s) + 1;
            if (total > 16 * 1024) {
                mp_raise_msg(&mp_type_ValueError,
                    MP_ERROR_TEXT("hexecute: argv is too long"));
            }
            argv[argc++] = (char *)s;
        }
    }
    argv[argc] = NULL;
    hcheck(oexecute(mp_obj_get_int(args[0]), obyteoid(args[1]),
                    argc ? argv : nil, argc));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(space_hexecute_obj, 2, 3,
                                           space_hexecute);

/* Native sealed provider and union operations. */
static long namespace_call(ObjCall *call) {
 long control=obj_open(ObjOpenNamespaceCurrent,NULL,0,Vcontrol,NULL);
 if(control<0)return control;
 long result=obj_invoke(control,call,sizeof *call);
 obj_close(control);
 return result;
}
static ObjCall namespace_request(unsigned op) {
 ObjCall call={0};call.version=OBJ_ABI_VERSION;call.op=op;return call;
}
static mp_obj_t space_view(mp_obj_t roles) {
 size_t n;mp_obj_t *a;char names[32][32];
 mp_obj_get_array(roles,&n,&a);
 if(!n||n>32)mp_raise_ValueError(MP_ERROR_TEXT("view role count"));
 memset(names,0,sizeof names);
 for(size_t i=0;i<n;i++){
  const char *role=mp_obj_str_get_str(a[i]);size_t len=strlen(role);
  if(len>=32)mp_raise_ValueError(MP_ERROR_TEXT("role length"));
  memcpy(names[i],role,len);
 }
 ObjCall call=namespace_request(ObjOpNamespaceCreate);
 call.in=names;call.inlen=n*32;
 long h=namespace_call(&call);hcheck(h);return mp_obj_new_int(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_view_obj,space_view);
static mp_obj_t space_mount(mp_obj_t h,mp_obj_t mode) {
 ObjCall call=namespace_request(ObjOpNamespaceAttach);
 call.arg0=mp_obj_get_int(h);call.arg1=mp_obj_get_int(mode);
 long id=namespace_call(&call);hcheck(id);return mp_obj_new_int(id);
}
static MP_DEFINE_CONST_FUN_OBJ_2(space_mount_obj,space_mount);
static mp_obj_t space_unmount(mp_obj_t id) {
 ObjCall call=namespace_request(ObjOpNamespaceDetach);
 call.arg0=mp_obj_get_uint(id);
 hcheck(namespace_call(&call));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_unmount_obj,space_unmount);
static mp_obj_t space_visible(void) {
 NsVisible records[32];ObjCall call=namespace_request(ObjOpNamespaceVisible);
 call.out=records;call.outlen=sizeof records;
 long count=namespace_call(&call);hcheck(count);
 mp_obj_t result=mp_obj_new_list(0,NULL);
 for(long i=0;i<count;i++){
  mp_obj_t fields[4]={mp_obj_new_bytes(records[i].object.volume,16),
   mp_obj_new_int_from_ull(records[i].object.space),
   mp_obj_new_int_from_ull(records[i].object.oid),
   mp_obj_new_int(records[i].rights)};
  mp_obj_list_append(result,mp_obj_new_tuple(4,fields));
 }
 return result;
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_visible_obj,space_visible);
static mp_obj_t space_open_handle(mp_obj_t name) {
 const char *s=mp_obj_str_get_str(name);long h=obj_open(ObjOpenSpace,(void*)s,strlen(s),0,NULL);
 hcheck(h);return mp_obj_new_int(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_open_handle_obj,space_open_handle);

/* Open a persistent GE/O object as a generic capability object through the
 * canonical OBJOPEN/OBJINVOKE/OBJCLOSE boundary.  Reads/writes resolve
 * through the existing O Space handle and GE/O provider.  Identity is the
 * persistent OID, not a new volatile OID. */
static mp_obj_t space_openobj(mp_obj_t oid_obj) {
    ObjOpenGeSpaceReq req;
    long h;
    if (!mp_obj_is_int(oid_obj))
        mp_raise_TypeError(MP_ERROR_TEXT("space.openobj: oid must be an int"));
    if (osp < 0)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("space.openobj: no O Space"));
    memset(&req, 0, sizeof req);
    req.space = osp;
    req.oid = (unsigned long long)mp_obj_get_uint(oid_obj);
    h = obj_open(ObjOpenGeSpace, &req, sizeof req, 0, NULL);
    if (h < 0) raise_err();
    return mp_obj_new_int(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_openobj_obj,space_openobj);

static const mp_rom_map_elem_t space_globals[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_space) },
    { MP_ROM_QSTR(MP_QSTR_view), MP_ROM_PTR(&space_view_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&space_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_unmount), MP_ROM_PTR(&space_unmount_obj) },
    { MP_ROM_QSTR(MP_QSTR_visible), MP_ROM_PTR(&space_visible_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_handle), MP_ROM_PTR(&space_open_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_openobj), MP_ROM_PTR(&space_openobj_obj) },
    { MP_ROM_QSTR(MP_QSTR_MREPL), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_MBEFORE), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_MAFTER), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_hclose), MP_ROM_PTR(&space_hclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_hget), MP_ROM_PTR(&space_hget_obj) },
    { MP_ROM_QSTR(MP_QSTR_hexecute), MP_ROM_PTR(&space_hexecute_obj) },
    { MP_ROM_QSTR(MP_QSTR_OREAD), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_OWRITE), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_OCREATE), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_OCOMMIT), MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_OEXECUTE), MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_ODELEGATE), MP_ROM_INT(32) },
    { MP_ROM_QSTR(MP_QSTR_runtime), MP_ROM_PTR(&space_runtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash), MP_ROM_PTR(&space_hash_obj) },
    { MP_ROM_QSTR(MP_QSTR_useenv), MP_ROM_PTR(&space_useenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_env), MP_ROM_PTR(&space_env_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&space_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&space_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_oid), MP_ROM_PTR(&space_oid_obj) },
    { MP_ROM_QSTR(MP_QSTR_oget), MP_ROM_PTR(&space_oget_obj) },
    { MP_ROM_QSTR(MP_QSTR_oput), MP_ROM_PTR(&space_oput_obj) },
    { MP_ROM_QSTR(MP_QSTR_oregister), MP_ROM_PTR(&space_oregister_obj) },
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&space_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&space_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_terminate), MP_ROM_PTR(&space_terminate_obj) },
    { MP_ROM_QSTR(MP_QSTR_CommitError), MP_ROM_PTR(&mp_type_CommitError) },
    { MP_ROM_QSTR(MP_QSTR_atomic), MP_ROM_PTR(&atomic_type) },
};
static MP_DEFINE_CONST_DICT(space_globals_dict, space_globals);

const mp_obj_module_t mp_module_space = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&space_globals_dict,
};

MP_REGISTER_MODULE(MP_QSTR_space, mp_module_space);

// space.interval: the seconds between commits at rests (>= 0)
void space_attr(mp_obj_t self, qstr attr, mp_obj_t *dest) {
    mp_float_t v;

    (void)self;
    if (attr != MP_QSTR_interval) {
        return;
    }
    if (dest[0] == MP_OBJ_NULL) {
        dest[0] = mp_obj_new_float(interval);
    } else if (dest[1] != MP_OBJ_NULL) {
        v = mp_obj_get_float(dest[1]);
        if (!(v >= 0)) {
            mp_raise_ValueError(MP_ERROR_TEXT("space.interval: not >= 0"));
        }
        interval = v;
        dest[0] = MP_OBJ_NULL;
    }
}
MP_REGISTER_MODULE_DELEGATION(mp_module_space, space_attr);
