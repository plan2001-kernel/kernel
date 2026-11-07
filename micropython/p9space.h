/* The persistent object environment: p9space.c and p9image.c (O-only) */

// a module loaded from the Environment: its context and bytecode tree
typedef struct _p9_space_mod_t {
    qstr name;
    mp_module_context_t *ctx;
    mp_raw_code_t *rc;
} p9_space_mod_t;

extern const mp_obj_type_t mp_type_CommitError;

p9_space_mod_t *p9_space_mods(int *n);
// the module NAME of the Environment, loaded without running it
p9_space_mod_t *p9_space_loadmod(qstr name);

// The OID layout used by the OID_GRAPH stream.  The numeric values are
// part of the stored form and must not change.
enum {
    OID_ROOTS = 1,
    OID_FIRST = 2,
    // V7 (the V7 Environment-epoch metadata): a historical internal record.
    // UINT64_MAX is obj.c's "no object selected" sentinel, so the one below it.
    OID_UPGRADE_META = 0xfffffffffffffffeULL,
    // Stage 4 (docs/O-FORMAT.md): a native O Space's graph and
    // its Environment manifest.  The definitions' code objects start at
    // OID_CODE; the host preparation tool assigns them explicitly.
    OID_GRAPH = 0xfffffffffffffffdULL,
    OID_ENV = 0xfffffffffffffffcULL,
    OID_CODE = 0xffffffff00000000ULL,
};

// the OID_GRAPH byte stream: the objects of *version restored and given
// their OIDs; the header's user part out.  There is no view/file form.
void p9_image_restore_store(const byte *p, size_t n, uint64_t *version, char *user, size_t *nuser);
// a commit's records: the changed objects (new OIDs from *next) and
// the freed, given to write (0 written, -1 failed: all rolled back).
// FORCE: write even when no object changed (an Environment or a
// catalog only).  Returns 1 written, 0 nothing changed, -1 failure
int p9_image_commit(uint64_t *next, int (*write)(vstr_t *rec, void *a), void *a, int force);
// every object of the graph changed: the O commit rewrites one whole stream
void p9_image_all_dirty(void);
// space.oid: p's OID, 0 none (not committed yet)
uint64_t p9_image_oid(const void *p);
int p9_image_fun_code_oid(uint64_t oid);
// REPL function code is a separate native O object, not a graph OID.
uint64_t p9_space_create_fun_code(const byte *p, size_t n);
void p9_space_read_fun_code(uint64_t oid, vstr_t *v);
// the Space's OIDs forgotten (a failed open)
void p9_image_reset(void);

// D2b: a native -N run disables optional legacy getenv instrumentation.
void p9_space_native_mode(int on);
vlong p9_now(void);
// Stage 4: use the native O volume SPEC (a PARTUUID or device) for the
// Space, its Environment and its code; 0 ok, -1 failed (printed)
int p9_space_o(const char *spec);
// Stage 5 (mp -O ... -S NAME -N/-I): the first boot of a fresh O Space
// imports the O-resident demo module, initializes it once and persists
// its run global; later boots restore that global and call it without
// running any module top level.  Failure is printed and nothing is
// committed.  0 ok, -1 failed
int p9_space_boot(void);
// Stage 5: after the commit, the persistent Counter's value, alias and
// OIDs (the same object across boots); failure is printed
int p9_space_report(void);

// a commit point (mp -S; FORCE: the program's end, within space.atomic
// too): 0 done or nothing to do, -1 failed (printed)
int p9_space_commit(int force);
// a rest, before a call blocks: a commit if due; CommitError
void p9_space_rest(void);
// $spacetime: WHAT's time since T0 (nsec()) on fd 2
void p9_spacetime(const char *what, long long t0);
