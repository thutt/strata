# Makefile for dviview, a DVI file viewer for X11.
#
# The targets are all, test, clean and help.
# Nothing is ever written into the source tree: the objects, the program and
# the results of the tests go into the build directory that scripts/setup
# exports (STRATA_BUILD_DIR).  The Makefile of the project passes BUILD and
# PROG itself.

CC      ?= cc
SRCDIR  := src

OUT     := $(STRATA_BUILD_DIR)
ifeq ($(OUT),)
ifneq ($(MAKECMDGOALS),help)
ifneq ($(origin BUILD)$(origin PROG),command linecommand line)
$(error STRATA_BUILD_DIR is not set: source scripts/setup first)
endif
endif
OUT     := /nonexistent
endif

BUILD   ?= $(OUT)/dviview-obj
PROG    ?= $(OUT)/term/dviview
RESULTS ?= $(OUT)/dviview-results

WARN     = -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Werror \
           -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
           -Wcast-qual -Wcast-align=strict -Wpointer-arith \
           -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
           -Wold-style-definition -Wredundant-decls -Wnested-externs \
           -Wformat=2 -Wformat-overflow=2 -Wformat-truncation=2 -Wundef \
           -Wwrite-strings -Wfloat-equal -Wlogical-op -Wduplicated-cond \
           -Wduplicated-branches -Wnull-dereference -Wjump-misses-init \
           -Wswitch-default -Wswitch-enum -Wunused-macros -Wdouble-promotion \
           -Walloca -Wvla -Wbad-function-cast -Wtrampolines \
           -Warith-conversion -Wimplicit-fallthrough=5 -Wstrict-aliasing=2 \
           -Wunused-const-variable=2 -Wcast-function-type \
           -Wmissing-include-dirs -Wformat-signedness -Walloc-zero \
           -Warray-bounds=2 -Wattribute-alias=2 -Wcomment -Wdate-time \
           -Wdisabled-optimization -Winit-self -Winvalid-pch \
           -Wmissing-field-initializers -Wshift-overflow=2 \
           -Wstringop-overflow=4 -Wunused-parameter -Wunused-local-typedefs \
           -Wtype-limits -Wunsafe-loop-optimizations -Wpacked \
           -Wvector-operation-performance -Wmultistatement-macros \
           -Wenum-conversion -Wsizeof-array-argument -Wstack-protector \
           -fstrict-flex-arrays=3 -Wstrict-flex-arrays \
           -Wtrivial-auto-var-init -Wfree-nonheap-object \
           -Wuse-after-free=3 -Wdangling-pointer=2 -Wanalyzer-too-complex

# The build type is also a define that ../../src/build_type.h turns into
# constants.  A standalone build is a debug build.
STRATA_BUILD_TYPE ?= debug
ifeq ($(STRATA_BUILD_TYPE),release)
TYPEDEF = -DSTRATA_RELEASE
else ifeq ($(STRATA_BUILD_TYPE),asan)
TYPEDEF = -DSTRATA_ASAN
else ifeq ($(STRATA_BUILD_TYPE),debug)
TYPEDEF = -DSTRATA_DEBUG
else
$(error STRATA_BUILD_TYPE is $(STRATA_BUILD_TYPE): use debug, release or asan)
endif

# The version comes from the Makefile of the project; a standalone build
# does not know it.
ifneq ($(STRATA_VERSION),)
TYPEDEF += '-DSTRATA_VERSION="$(STRATA_VERSION)"'
endif

# The object that prints the version is made again only when the version
# changes: the stamp file is written only then.
ifneq ($(STRATA_VERSION),)
VERSION_STAMP := $(BUILD)/version
VERSION_MADE := $(shell mkdir -p $(BUILD); \
    if test ! -f $(VERSION_STAMP) || \
       test "$$(cat $(VERSION_STAMP))" != '$(STRATA_VERSION)'; then \
        printf '%s\n' '$(STRATA_VERSION)' > $(VERSION_STAMP); \
    fi)
$(BUILD)/main.o: $(VERSION_STAMP)
endif

CFLAGS  = -O2 $(WARN) $(TYPEDEF) -I/usr/include/freetype2 -iquote ../../src
LDLIBS  = -lfreetype -lX11 -lpng -lm
OBJS    = $(BUILD)/main.o $(BUILD)/dvi.o $(BUILD)/font.o $(BUILD)/tfm.o \
          $(BUILD)/pk.o $(BUILD)/fontmap.o $(BUILD)/util.o \
          $(BUILD)/gfx.o $(BUILD)/gstate.o $(BUILD)/psi.o \
          $(BUILD)/jpeg.o $(BUILD)/image.o $(BUILD)/special.o \
          $(BUILD)/render.o $(BUILD)/log.o \
          $(BUILD)/mkfont.o $(BUILD)/sysprog.o

.PHONY: all test clean help

all: $(PROG)

help:
	@echo "Targets:"
	@echo "  all    Build the program dviview (the default)."
	@echo "  test   Run the Xvfb tests; prints one ok or FAIL line each."
	@echo "  clean  Remove the objects, the program and the results."
	@echo "  help   Show this list."

$(PROG): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# The compiler writes the dependencies of each object (-MMD -MP); the .d
# files that exist are read.  No list of headers is kept by hand.
DEPFLAGS = -MMD -MP

# A change of this Makefile (the flags, say) builds everything again.
$(BUILD)/%.o: $(SRCDIR)/%.c Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# The logging module is shared with the other programs.
$(BUILD)/log.o: ../../src/log.c Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD)/sysprog.o: ../../src/sysprog.c Makefile
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(wildcard $(BUILD)/*.d)

$(BUILD)/shot: tests/shot.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ $< -lX11

test: all $(BUILD)/shot
	DVIEW_PROG=$(abspath $(PROG)) DVIEW_SHOT=$(abspath $(BUILD)/shot) \
	DVIEW_RESULTS=$(abspath $(RESULTS)) DVIEW_TYPE=$(STRATA_BUILD_TYPE) \
	bash tests/run_tests.sh

clean:
	rm -rf $(BUILD) $(PROG) $(RESULTS)
