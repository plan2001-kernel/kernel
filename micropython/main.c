/*
 * MicroPython: native GE/O boot and the in-memory source path.
 * In O-only boot, -O PARTUUID=... -S init -I restores the persistent
 * init Space, calls run() and hands the console to the native REPL;
 * -N is the same boot without the REPL.  There is no pathname file
 * argument, no built-in boot.py and no fd console fallback.
 *	micropython -c source		run source
 *	micropython -O ... -S init -N	the native noninteractive server boot
 *	micropython -O ... -S init -I	the same, then the native REPL
 *	micropython -O volume ...	use the native O volume (a PARTUUID or
 *					a device) for the Space, its Environment
 *					and its code: no filesystem
 *	micropython -E env ...		import from the Environment env alone
 *	micropython -S space ...	in the Space space: its image restored,
 *					committed per statement and at the
 *					program's end
 *					(see p9space.c; O mode: docs/O-FORMAT.md)
 * A failure exits with status micropython (usage: usage); sys.exit(n)
 * with n.
 */
#include <string.h>
extern int sleep(long);
#include "py/compile.h"
#include "py/builtin.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/cstack.h"
#include "py/objlist.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "p9console.h"
#include "p9space.h"
#ifndef NO_QSTR
#include "../../runtime/abi/obj.h"
#endif

static uintptr_t heap[MICROPY_HEAP_SIZE / sizeof(uintptr_t)];
static uintptr_t pystack[MICROPY_PYSTACK_SIZE / sizeof(uintptr_t)];

#ifdef PLAN2001_SYSCALL_TRACE
/* Measurement-only kernel checkpoint: flushes the dispatch-trace segment. */
extern long syssnapshot(void *);
static void syscallt0checkpoint(void) {
    unsigned long long v[7];
    syssnapshot(v);
}
#else
#define syscallt0checkpoint() ((void)0)
#endif

// the largest area the heap grows by at a time; a failed malloc stops it
size_t gc_get_max_new_split(void) {
    return 256 * 1024 * 1024;
}

// an exception other than SystemExit: no commit at the end
static int uncaught;

// SystemExit's status, or 1 for another exception (printed)
static int failed(mp_obj_t exc) {
    mp_obj_t code;

    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(mp_obj_get_type(exc)), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
        code = mp_obj_exception_get_value(exc);
        if (code == mp_const_none) {
            return 0;
        }
        if (mp_obj_is_small_int(code)) {
            return MP_OBJ_SMALL_INT_VALUE(code) != 0;
        }
        return 1;
    }
    mp_obj_print_exception(&mp_plat_print, exc);
    uncaught = 1;
    return 1;
}

static int execute(mp_lexer_t *lex, qstr name) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fun = mp_compile(&tree, name, false);
        mp_call_function_0(fun);
        nlr_pop();
        return 0;
    }
    return failed(MP_OBJ_FROM_PTR(nlr.ret_val));
}

static int execstr(const char *source, qstr name) {
    return execute(mp_lexer_new_from_str_len(name, source, strlen(source), 0), name);
}

static void repl(void) {
    for (;;) {
        int result = pyexec_mode_kind == PYEXEC_MODE_RAW_REPL
            ? pyexec_raw_repl() : pyexec_friendly_repl();
        if (result != 0) {
            break;
        }
    }
}

// a note: interrupt is KeyboardInterrupt in the running code; alarm
// (plan9.alarm) only cuts the blocked call short, which raises OSError
static int onnote(void *ureg, char *note) {
    (void)ureg;
    if (strcmp(note, "interrupt") == 0) {
        mp_sched_keyboard_interrupt();
        return 1;
    }
    if (strcmp(note, "alarm") == 0) {
        return 1;
    }
    return 0;
}

static void bootfailed(const char *why) {
    p9_dprintf("PLAN2001-O: BOOT FAILED\nPLAN2001-O: %s\n", why);
}

int main(int argc, char **argv) {
    volatile uintptr_t stack_top;
    int fail = 0, nativeboot = 0, nativerepl = 0, autorun = 0, bootfail = 0;
    const char *source = NULL, *ovol = NULL, *env = NULL, *space = NULL;
    const char *booterror = "initialization or run() failed (see error above)";
    char useenv[128];

    // -O VOLUME: the native O volume; then -E or -S as before
    if (argc >= 3 && strcmp(argv[1], "-O") == 0) {
        ovol = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }
    // -E ENV: import from the Environment ENV alone; -S SPACE: in the
    // Space SPACE (p9space.c)
    if (argc >= 3 && strcmp(argv[1], "-E") == 0) {
        env = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    } else if (argc >= 3 && strcmp(argv[1], "-S") == 0) {
        space = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }
    if (argc == 2 && strcmp(argv[1], "-N") == 0) {
        nativeboot = 1;
    } else if (argc == 2 && strcmp(argv[1], "-I") == 0) {
        nativerepl = 1;
    } else if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        source = argv[2];
    } else if (argc != 1) {
        p9_dprintf("usage: micropython [-O volume] [-E env | -S space] [-N | -I | -c source]\n");
        exits("usage");
    }
    p9_space_native_mode(nativeboot || nativerepl);
    // D2a: only an -O run may look for the process-local native stream.
    p9_console_native(ovol != NULL);
    p9_console_probe();
    if (nativeboot && (ovol == NULL || space == NULL)) {
        p9_dprintf("micropython: -N requires -O volume and -S space\n");
        bootfailed("-N requires -O volume and -S space");
        exits("usage");
    }
    if (nativerepl && (ovol == NULL || space == NULL)) {
        p9_dprintf("micropython: -I requires -O volume and -S space\n");
        bootfailed("-I requires -O volume and -S space");
        for (;;) {
            sleep(1000);
        }
    }
    // inf and nan as results, as in C: Plan 9 traps them by default
    setfcr(getfcr() & ~(FPINVAL | FPZDIV | FPOVFL));
    // Plan2001's user stack is 16 MiB (USTKSIZE): half of it for C's
    // recursion (Python's is in the pystack, MICROPY_STACKLESS)
    mp_cstack_init_with_top((void *)&stack_top, 8 * 1024 * 1024);
    gc_init(heap, (char *)heap + sizeof(heap));
    mp_pystack_init(pystack, (char *)pystack + sizeof(pystack));
    mp_init();
    syscallt0checkpoint();
    if (ovol != NULL && p9_space_o(ovol) < 0) {
        if (nativeboot || nativerepl)
            bootfailed("cannot open native O volume (see error above)");
        if (nativerepl) {
            for (;;) {
                sleep(1000);
            }
        }
        mp_deinit();
        p9_console_exit(1);
        return 1;
    }
    if (space != NULL) {
        // the name goes into Python's source: a plain one
        if (strlen(space) >= 64 || space[strcspn(space, "'\\/\n")] != 0) {
            p9_dprintf("micropython: -S %s: not a space name\n", space);
            if (nativeboot || nativerepl)
                bootfailed("invalid Space name");
            if (nativerepl) {
                for (;;) {
                    sleep(1000);
                }
            }
            exits("usage");
        }
        snprint(useenv, sizeof useenv, "__import__('space').open('%s')", space);
        if (execstr(useenv, MP_QSTR__lt_command_gt_) != 0) {
            if (nativeboot || nativerepl)
                bootfailed("cannot restore init Space (see error above)");
            if (nativerepl) {
                for (;;) {
                    sleep(1000);
                }
            }
            mp_deinit();
            p9_console_exit(1);
            return 1;
        }
    } else if (env == NULL) {
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
        mp_obj_list_append(mp_sys_path, mp_obj_new_str("/sys/lib/python", 15));
    } else {
        // the name goes into Python's source: an Environment's is hex
        if (strlen(env) > 64 || env[strspn(env, "0123456789abcdef")] != 0) {
            p9_dprintf("micropython: -E %s: not an Environment\n", env);
            exits("usage");
        }
        snprint(useenv, sizeof useenv, "import space\nspace.useenv('%s')", env);
        if (execstr(useenv, MP_QSTR__lt_command_gt_) != 0) {
            mp_deinit();
            p9_console_exit(1);
            return 1;
        }
    }
    atnotify(onnote, 1);
    if (source != NULL) {
        fail = execstr(source, MP_QSTR__lt_command_gt_);
    } else if (nativeboot || nativerepl) {
        // -O ... -S init -N: native noninteractive server boot.  -I is
        // the same boot, after which the native REPL takes over.  No
        // boot.py, no /dev and no legacy fd console.
        autorun = 1;
        if (p9_space_boot() < 0) {
            bootfail = 1;
            fail = 1;
        }
    } else {
        p9_dprintf("usage: micropython -c source | -O volume -S space [-N|-I]\n");
        exits("usage");
    }
    // the program's end, normal or SystemExit: a commit point
    if (!fail && !uncaught && !bootfail && p9_space_commit(1) < 0) {
        fail = 1;
        booterror = "GE/O commit failed (see error above)";
        if (autorun) {
            bootfail = 1;
            p9_dprintf("init: failed, no commit\n");
        }
    }
    if (autorun) {
        // the commit is durable; report the object identity with the
        // OIDs it now has, then stay alive without a REPL (-N), or let
        // the native REPL take over (-I).  QEMU (or the operator) stops
        // the machine; an init exit would panic.
        if (!uncaught && !bootfail && p9_space_report() < 0) {
            fail = 1;
            booterror = "persistent state report failed (see error above)";
        }
        if (!fail && !uncaught && !bootfail) {
            p9_dprintf("init: done\n");
        }
        syscallt0checkpoint();
        if (nativeboot) {
            if (fail || uncaught || bootfail)
                bootfailed(booterror);
            else
                p9_dprintf("PLAN2001-O: BOOT SUCCESS\n"
                    "PLAN2001-O: GE/O COMMIT SUCCESS\n"
                    "PLAN2001-O: SYSTEM IDLE\n");
        } else if (nativerepl) {
            if (fail || uncaught || bootfail) {
                bootfailed(booterror);
            } else if (p9_console_init_native() < 0) {
                bootfailed("native keyboard initialization failed");
            } else {
                p9_dprintf("PLAN2001-O: BOOT SUCCESS\n"
                    "PLAN2001-O: GE/O COMMIT SUCCESS\n"
                    "PLAN2001-O: REPL READY\n");
                // Ctrl-D ends the friendly REPL but not init: reset to
                // friendly REPL and read the keyboard again.
                for (;;) {
                    pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
                    repl();
                }
            }
        }
        for (;;) {
            sleep(1000);
        }
    }
    mp_deinit();
    p9_console_exit(fail);
    return fail;
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

MP_NORETURN void nlr_jump_fail(void *value) {
    (void)value;
    const char *message = "fatal: uncaught MicroPython exception\n";
    p9_console_write(message, strlen(message));
    p9_console_exit(1);
    for (;;) {}
}

// MicroPython import callback (py/builtinimport.c).  The symbol is part of
// the core port interface, but this kernel has no pathname OPEN: every file
// import fails with the same ENOENT as the former dead open() attempt.
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

// MicroPython import-stat callback (py/builtinimport.c).  The symbol is part
// of the core port interface; without pathname STAT every path is absent.
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}
