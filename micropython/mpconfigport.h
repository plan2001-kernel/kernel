/*
 * MicroPython on Plan2001 (amd64; docs/O-FORMAT.md): the GCC-built Plan
 * 9-origin C runtime.  The same file preprocesses the qstrs on Linux
 * (gcc -E, Makefile: make genhdr after a change here).
 */
#include <stdint.h>
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
/*
 * What a model writes expecting CPython: floats (double, libc's math;
 * no complex), big integers, math, json, tracebacks with line numbers
 * and errors that name the object.  No math.inf and nan: they are
 * initialised data, and libc's are functions (float('inf') works)
 */
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_MATH_CONSTANTS (0)
#define MICROPY_PY_MATH_ISCLOSE (1)
#define MICROPY_PY_MATH_FACTORIAL (1)
#define MICROPY_PY_CMATH (0)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_PY_JSON (1)
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_DETAILED)
/* io (StringIO/BytesIO and the stream machinery); pathname open() is gone */
#define MICROPY_PY_IO (1)
/* no Plan 9 pathname open(): the native O API is the only storage interface */
#define MICROPY_PY_BUILTINS_OPEN (0)
#define MICROPY_ENABLE_FINALISER (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
/* repr('ä') is 'ä', as in CPython */
#define MICROPY_PY_STR_REPR_UTF8 (1)
#define MICROPY_PY_FSTRINGS (1)
#define MICROPY_PY_ARRAY_SLICE_ASSIGN (1)
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_EMACS_KEYS (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_AUTO_INDENT_ENTER_ENDS (1)
#define MICROPY_REPL_UTF8 (1)
/* tabs and spaces mixed so that the blocks are ambiguous: an error */
#define MICROPY_LEXER_TAB_CHECK (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
/*
 * the persistent object environment (p9space.c, p9space.c and p9image.c):
 * a module's .mpy made once and loaded from memory; import through
 * p9_import (an Environment's modules, or MicroPython's own)
 */
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#define MICROPY_PERSISTENT_CODE_SAVE (1)
#define MICROPY_PERSISTENT_CODE_SAVE_FUN (1)
#define mp_builtin___import__ p9_import
/* a Space's commit point: a REPL statement's end (mp -S) */
int p9_space_commit(int force);
// the Space's object store (p9image.c, ge/storage/obj.c): an object
// with an OID changed in place, one freed; on only when there are OIDs
extern size_t p9_space_noid;
void p9_space_dirty(const void *p);
void p9_space_freed(const void *p);
#define P9_DIRTY(p) do { if (p9_space_noid) p9_space_dirty(p); } while (0);
#define P9_FREED(p) do { if (p9_space_noid) p9_space_freed(p); } while (0);
#define MICROPY_BOARD_AFTER_PYTHON_EXEC(kind, flags, val, ret) p9_space_commit(0)
/* the types a Space keeps (p9space.c and p9image.c 5.5); graph's links
   are descriptors (5.7) */
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_DESCRIPTORS (1)
/* a Space's graph is wide: a mark stack that overflows rescans the heap */
#define MICROPY_ALLOC_GC_STACK_SIZE (4096)
#define MICROPY_VFS (0)
#define MICROPY_READER_POSIX (0)
#define MICROPY_NLR_SETJMP (1)
#define MICROPY_GCREGS_SETJMP (1)
#define MICROPY_STACK_CHECK (1)
/*
 * no alloca: the Python stack has its own area.  Stackless: a Python
 * call from Python takes its frame there, not on the C stack, so the
 * depth of recursion is the pystack's (calls through C, such as
 * __init__ or a builtin calling back, still take the C stack)
 */
#define MICROPY_ENABLE_PYSTACK (1)
#define MICROPY_STACKLESS (1)
#define MICROPY_NO_ALLOCA (1)
#define MICROPY_USE_INTERNAL_ERRNO (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_MODULES (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_SYS_MAXSIZE (1)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_HW_BOARD_NAME "Plan2001"
#define MICROPY_HW_MCU_NAME "amd64"
#define MICROPY_PY_SYS_PLATFORM "plan9"
/* print's \n as it is, into a pipe or a file too: Plan 9's console needs no \r */
#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn(str, len)
#define MICROPY_HEAP_SIZE (4 * 1024 * 1024)
/* the heap grows from malloc when it runs out (main.c) */
#define MICROPY_GC_SPLIT_HEAP (1)
#define MICROPY_GC_SPLIT_HEAP_AUTO (1)
#define MICROPY_PYSTACK_SIZE (1024 * 1024)
#define MP_SSIZE_MAX INTPTR_MAX
#define MP_ENDIANNESS_LITTLE (1)

#ifdef __PLAN9__
#define MP_NORETURN _Noreturn
#define MP_WEAK
#define MP_NOINLINE
#define MP_ALWAYSINLINE
#define MP_LIKELY(x) (x)
#define MP_UNLIKELY(x) (x)
#define MP_UNREACHABLE for (;;);
#define PACKED
#define MP_BITS(n)
#define MP_OBJ_TYPE_NSLOTS 11
#define MP_ROM_TUPLE_NITEMS 5	/* sys.implementation with _mpy */
#define MP_QSTR_POOL_SIZED (1)
#define MP_NO_NONCONSTEXPR_ASSERT
/* Plan 9 C: NaN compares as equal, -x of +0.0 is +0.0 */
#define MICROPY_FLOAT_CC_FIXES (1)
/* repr: EXACT needs a long double wider than double, which the runtime has not */
#define MICROPY_FLOAT_FORMAT_IMPL (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
#define MICROPY_QSTR_VARG_UINT (1)
/* Plan 9 C bit fields are an int's at most: a float's bits by macros */
#define MP_FLOAT_NO_BITFIELD (1)
/* typecode and free in one int: str and bytearray share the layout */
#define MP_OBJ_ARRAY_FREE_SIZE_BITS 24
#define snprintf snprint
#endif

/*
 * GCC freestanding build (tools/build-mpy-gcc.sh).  The Plan 9 block
 * above still selects the Plan 9 C types and the persistent image
 * semantics; these overrides remove only compiler artefacts the GCC
 * ABI does not need.
 */
#ifdef PLAN2001_GCC_USER
#undef MP_NORETURN
#define MP_NORETURN
#undef MICROPY_FLOAT_CC_FIXES
#define MICROPY_FLOAT_CC_FIXES (0)
#endif

typedef long long mp_off_t;
#define MP_STATE_PORT MP_STATE_VM
#define MICROPY_USE_GCC_MUL_OVERFLOW_INTRINSIC (0)

/* Poll the region session input for a pending Ctrl-C while bytecode runs.
 * The console owner publishes control keys as session input; consuming only
 * an exact Ctrl-C record preserves typed-ahead REPL input. */
#define MICROPY_VM_HOOK_LOOP do { \
    extern int p9_console_poll_interrupt(void); \
    if (p9_console_poll_interrupt()) { \
        mp_sched_keyboard_interrupt(); \
    } \
} while (0);
