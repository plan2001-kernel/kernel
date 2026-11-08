# Linux: genhdr/ (the qstrs, the modules, the root pointers, the version)
# for this configuration, with gcc -E and MicroPython's Python scripts.
# The target has no CPython, so genhdr/ is in the tree: make genhdr after
# a change to mpconfigport.h or a new qstr.  The freestanding runtime is
# built by tools/build-mpy-gcc.sh; mpy-cross has its own GNU Makefile.
include $(CURDIR)/py/mkenv.mk
QSTR_DEFS = qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION = 0
include $(TOP)/py/py.mk
INC = -I. -I$(TOP) -I$(BUILD)
CFLAGS += $(INC) -std=gnu99 -DNDEBUG
SRC_C = main.c p9hal.c modplan9.c modge.c modcbor.c modaesgcm.c p9space.c p9image.c extmod/modjson.c shared/readline/readline.c shared/runtime/pyexec.c shared/runtime/stdout_helpers.c shared/runtime/gchelper_generic.c
SRC_QSTR += main.c modplan9.c modge.c modcbor.c modaesgcm.c p9space.c p9image.c extmod/modjson.c shared/readline/readline.c shared/runtime/pyexec.c
OBJ += $(PY_CORE_O) $(addprefix $(BUILD)/,$(SRC_C:.c=.o))
GENHDR = qstrdefs.generated.h moduledefs.h root_pointers.h mpversion.h
genhdr: $(addprefix $(HEADER_BUILD)/,$(GENHDR))
	mkdir -p genhdr
	cp $^ genhdr/
	# use the pinned MicroPython release from tools/micropython.import
	sed -i 's/VERSION_TAG ".*"/VERSION_TAG "v1.27.0"/' genhdr/mpversion.h
	rm -rf $(BUILD)
.PHONY: genhdr
include $(TOP)/py/mkrules.mk
