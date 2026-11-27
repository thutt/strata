# Strata is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# Strata is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Strata.  If not, see <https://www.gnu.org/licenses/>.
# Copyright (c) 2026 Logic Magicians Software

# The build directory and build type come from the environment prepared by
#     source <project>/scripts/setup [-b debug|release|asan] /tmp/strata
# which builds in /tmp/strata/<type>.
# Nothing is ever built inside the source tree.
ifeq ($(STRATA_BUILD_DIR),)
$(error STRATA_BUILD_DIR is not set.  Run: \
        source <project>/scripts/setup /tmp/strata)
endif
O        := $(STRATA_BUILD_DIR)
# The source tree is the root of the repository: the Makefile is in it.
SRCROOT  := $(abspath $(CURDIR))
ifneq ($(filter $(SRCROOT)/%,$(abspath $(O))/),)
$(error refusing to build inside the source tree \
        ($(abspath $(O)) is inside $(SRCROOT)); use <project>/scripts/setup)
endif
STRATA_BUILD_TYPE ?= debug

# The version of Strata is the name of the branch that is built (see
# docs/VERSIONING.txt); scripts/strata-version derives it from git.  The
# programs print it with --version and the install script records it.  A
# tree that has no script, or no git repository, is an unsupported build
# configuration.  The version has spaces, so every use of it is quoted.
VERSION  := $(shell if test -x $(SRCROOT)/scripts/strata-version; then \
                $(SRCROOT)/scripts/strata-version $(SRCROOT); \
            else echo UNSUPPORTED BUILD CONFIGURATION; fi)
# The version as one word of a shell command line: each space has a backslash
# before it, and the sub-makes and the recipes pass the word on unchanged.
EMPTY    :=
SPACE    := $(EMPTY) $(EMPTY)
VERSION_Q := $(subst $(SPACE),\ ,$(VERSION))

# The file that holds the version of this build, which the install script
# copies.  It is written when the Makefile is read, and only when the version
# is not in it, so that its time changes only when the version changes: the
# objects that print the version depend on it and are made again only then.
VERSION_FILE := $(abspath $(STRATA_BUILD_DIR))/version
VERSION_MADE := $(shell mkdir -p $(dir $(VERSION_FILE)); \
    if test ! -f $(VERSION_FILE) || \
       test "$$(cat $(VERSION_FILE))" != '$(VERSION)'; then \
        printf '%s\n' '$(VERSION)' > $(VERSION_FILE); \
    fi)

CC      ?= cc
# Quiet by default; "make V=1" (or strata-build V=1) shows the full commands.
ifeq ($(V),1)
Q        =
else
Q        = @
endif

# Strict by default: every warning below is an error.
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
# The build type is also a preprocessor define that src/build_type.h turns
# into the constants strata_debug, strata_release and strata_asan.
ifeq ($(STRATA_BUILD_TYPE),release)
OPT      = -O2 -DSTRATA_RELEASE
else ifeq ($(STRATA_BUILD_TYPE),asan)
# With the sanitizers GCC 15 says "argument 1 value is zero" of an allocation
# whose size is usz(n) after n was tested to be more than zero (the range
# that it follows through the instrumented code is wrong): the warning is
# turned off for this build type only (WARNOFF comes after WARN).
OPT      = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
           -DSTRATA_ASAN
WARNOFF  = -Wno-alloc-zero
else ifeq ($(STRATA_BUILD_TYPE),debug)
OPT      = -O0 -g -DSTRATA_DEBUG
else
$(error STRATA_BUILD_TYPE is $(STRATA_BUILD_TYPE): use debug, release or asan)
endif
CFLAGS   = $(OPT) $(WARN) $(WARNOFF) -DSTRATA_VERSION=\"$(VERSION_Q)\"
SRC      = src
BIN     ?= $(O)/term
DOCSRC  ?= tests/samples

TERMO   ?= term
LDLIBS  ?=
LIBOBJ   = $(BIN)/color.o $(BIN)/msdoc.o $(BIN)/readfile.o $(BIN)/layout.o $(BIN)/marker.o $(BIN)/diag.o \
           $(BIN)/log.o $(BIN)/vars.o $(BIN)/backup.o $(BIN)/calc.o \
           $(BIN)/border.o $(BIN)/sysprog.o
EDOBJ    = $(BIN)/editor.o $(BIN)/view.o $(BIN)/move.o $(BIN)/fileops.o \
           $(BIN)/tplfind.o \
           $(BIN)/markereval.o $(BIN)/note.o $(BIN)/spell.o \
           $(BIN)/thesaurus.o $(BIN)/perr.o $(BIN)/undo.o $(BIN)/search.o

# The default target builds everything: the terminal version and the X11
# version.
.PHONY: all term x11 dviview templates-link help test printtest smoke smoke-quick xtest \
        verify smoke-long xtest-long fuzz clean big install uninstall docversion

all: term x11 dviview

# The terminal programs and the test tools.
term: $(O)/strata.hlp templates-link $(BIN)/stred $(BIN)/str $(BIN)/roundtrip \
        $(BIN)/msdump $(BIN)/tabcheck $(BIN)/edtest $(BIN)/memtest \
        $(BIN)/memprof $(BIN)/strprint $(BIN)/lxtest $(BIN)/pvtest \
        $(BIN)/pvest $(BIN)/pjtest $(BIN)/sptest $(BIN)/bigtest $(BIN)/sectags

# Lists the valid targets.
help:
	@echo "Targets (the default is all):"
	@echo "  all     Build everything: term and x11."
	@echo "  term    Build the terminal programs and the test tools."
	@echo "  x11     Build the X11 programs (needs libx11-dev)."
	@echo "  dviview Build the DVI viewer of the preview (needs libx11-dev"
	@echo "          and libfreetype-dev; skipped when they are missing)."
	@echo "  test    Build and run the unit tests."
	@echo "  docversion  Check that every document of the product has the"
	@echo "          current file format version (tools/docversion.py)."
	@echo "  memprof Print the memory and time of opening FILE=<doc>."
	@echo "  printtest  Print every sample document with strprint (PDF, DVI)."
	@echo "  big     Run the tests on a document of 1000 pages (4 minutes)."
	@echo "  smoke   Run the terminal smoke tests, SMOKE_JOBS at a time"
	@echo "          (default the CPUs, at most 4; SMOKE_JOBS=1 is serial)."
	@echo "  smoke-quick  Every script of smoke for SMOKE_QUICK_SECONDS (60)"
	@echo "          seconds only: about 5 minutes (see docs/TESTING.txt)."
	@echo "  xtest   Run the X11 smoke tests."
	@echo "  verify  Run every suite and check the source tree."
	@echo "  install (strata-build only) Make the install tarball and its"
	@echo "          checksum in the build directory, only when asked: no"
	@echo "          other target makes it.  It installs nothing.  To install"
	@echo "          from a build directory run scripts/strata-install"
	@echo "          --build-dir DIR [OPTIONS]; the default prefix is /usr,"
	@echo "          which needs an administrator, and --prefix \$$HOME/usr"
	@echo "          is for anybody else."
	@echo "  install-tarball (strata-build only) The same as install.  It"
	@echo "          is a fatal error to make it without every manual and"
	@echo "          the PDF file of every manual."
	@echo "  manual  (strata-build only) Make the manuals and their PDF"
	@echo "          files in the build directory; needs LaTeX.  Never part"
	@echo "          of all."
	@echo "  check-tools  (strata-build only) Report which programs, headers"
	@echo "          and libraries that building, testing and running need"
	@echo "          are present; installs nothing."
	@echo "  uninstall Remove what 'install' put under the prefix."
	@echo "  clean   Delete the build products."
	@echo "  help    Show this list."

# Every document of the product (the samples, the templates, the documents of
# the tests and of the manual pictures) carries the file format version in
# its first record.  This target fails when one of them is below the current
# version.  The generated manuals are not listed: the build writes them with
# the current version.  To change the version of the documents after the
# file format changed, run: python3 -I -B tools/docversion.py $(DOCVERSION_DIRS)
DOCVERSION_DIRS = samples templates tests/data tests/samples manual/samples
docversion:
	python3 -I -B tools/docversion.py --check --quiet $(DOCVERSION_DIRS)

# The target install makes the install tarball in the build directory and
# installs nothing, as 'strata-build install' does; it is never a part of
# 'all'.  The script scripts/strata-install installs from a build directory
# when it is run directly, and uninstall runs it with the options of
# INSTALL_ARGS.
install:
	$(SRCROOT)/scripts/strata-install-tarball $(abspath $(O)) \
	    $(STRATA_BUILD_TYPE)

uninstall:
	$(SRCROOT)/scripts/strata-install --uninstall $(INSTALL_ARGS)

# The compiler writes the dependencies of each object (-MMD -MP): the file
# NAME.d next to NAME.o lists the headers that the source includes, and the
# line below reads the files that exist.  No list of headers is kept by hand.
# A program that is compiled and linked in one step (the tests, strprint)
# writes PROGRAM.d the same way.
DEPFLAGS = -MMD -MP

# Only what prints the version is made again when it changes: the objects of
# the Editor and of the Document Manager and the program of the Printer.
$(BIN)/main.o $(BIN)/str.o $(BIN)/strprint: $(O)/version

# A change of this Makefile (the flags, say) builds everything again.
$(BIN)/%.o: $(SRC)/%.c Makefile | $(BIN)
	$(Q)echo "  CC   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(wildcard $(BIN)/*.d)

UIOBJ    = $(BIN)/help.o $(BIN)/widgets.o $(BIN)/ui.o $(BIN)/export.o \
           $(BIN)/config.o $(BIN)/fonts.o $(BIN)/hyphlist.o \
        $(BIN)/markerui.o $(BIN)/filemenu.o $(BIN)/template.o $(BIN)/menu.o \
        $(BIN)/printmenu.o \
        $(BIN)/globalmenu.o $(BIN)/library.o $(BIN)/macro.o \
        $(BIN)/bordermenu.o $(BIN)/recoverui.o \
        $(BIN)/png.o $(BIN)/eqrender.o $(BIN)/pvcore.o $(BIN)/preview.o \
        $(BIN)/printjob.o $(BIN)/printcli.o \
        $(BIN)/latex.o $(BIN)/cond.o $(BIN)/include.o $(BIN)/logodata.o \
        $(BIN)/latexmark.o \
        $(BIN)/spellui.o $(BIN)/thesmenu.o \
        $(BIN)/errlist.o \
        $(BIN)/dictmenu.o $(BIN)/comparemenu.o $(BIN)/compare.o \
        $(BIN)/dictlist.o \
           $(BIN)/main.o \
           $(BIN)/$(TERMO).o \
           $(BIN)/termbuf.o

$(BIN)/stred: $(LIBOBJ) $(EDOBJ) $(UIOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The help file is the topics of help/*.txt, one after the other.  It is
# written into the build directory, next to the programs.
HELPSRC  = $(sort $(wildcard help/*.txt))

# A program that runs in place from a build directory looks for the templates
# in ../share/strata/templates, as an installed one does.  The build
# directory holds a link of that name to the templates of the source tree:
# a link cannot go stale, and it is made again at every build.
# The sample envelopes are in share/strata/samples/envelopes the same way,
# and the sample document of the history of Strata in
# share/strata/samples/story, and the sample of the fonts in
# share/strata/samples/fonts.
templates-link:
	$(Q)mkdir -p $(O)/share/strata
	$(Q)ln -sfn $(CURDIR)/templates $(O)/share/strata/templates
	$(Q)mkdir -p $(O)/share/strata/samples
	$(Q)ln -sfn $(CURDIR)/samples/envelopes $(O)/share/strata/samples/envelopes
	$(Q)ln -sfn $(CURDIR)/samples/story $(O)/share/strata/samples/story
	$(Q)ln -sfn $(CURDIR)/samples/fonts $(O)/share/strata/samples/fonts

$(O)/strata.hlp: $(HELPSRC)
	$(Q)echo "  HELP $@"
	$(Q)mkdir -p $(O)
	$(Q)cat $(HELPSRC) > $@

$(BIN)/helptest: tests/helptest.c $(BIN)/help.o $(BIN)/readfile.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)

$(BIN)/str: $(BIN)/str.o $(BIN)/colorui.o $(BIN)/config.o $(BIN)/fonts.o $(BIN)/widgets.o \
        $(BIN)/template.o $(BIN)/tplfind.o \
        $(BIN)/help.o $(BIN)/printjob.o $(BIN)/pvcore.o $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/logrun.o $(BIN)/compare.o \
        $(BIN)/comparemenu.o $(BIN)/restoremenu.o $(BIN)/spell.o \
        $(BIN)/dictlist.o $(BIN)/hyphlist.o \
        $(BIN)/dictmenu.o $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/roundtrip: tests/roundtrip.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/tabcheck: tests/tabcheck.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/edtest: tests/edtest.c $(LIBOBJ) $(EDOBJ) $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o \
        $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/undotest: tests/undotest.c $(LIBOBJ) $(EDOBJ) $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o \
        $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/listtest: tests/listtest.c $(LIBOBJ) $(EDOBJ) $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o \
        $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/bigtest: tests/bigtest.c $(LIBOBJ) $(EDOBJ) $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o \
        $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/perrtest: tests/perrtest.c $(LIBOBJ) $(EDOBJ) $(BIN)/latex.o \
        $(BIN)/logodata.o \
        $(BIN)/cond.o $(BIN)/include.o $(BIN)/$(TERMO).o $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o \
        $(BIN)/eqrender.o $(BIN)/config.o $(BIN)/fonts.o $(BIN)/hyphlist.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/memtest: tests/memtest.c tests/memtest.expect $(LIBOBJ) $(EDOBJ) \
        $(BIN)/$(TERMO).o $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o \
        $(BIN)/eqrender.o $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/memprof: tests/memprof.c $(LIBOBJ) $(EDOBJ) $(BIN)/$(TERMO).o \
        $(BIN)/termbuf.o $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o \
        $(BIN)/config.o $(BIN)/fonts.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The print program: the document, the markers and their evaluation, and the
# LaTeX writer.  It links nothing of the editor and the user interface.
PRINTOBJ = $(BIN)/markereval.o $(BIN)/note.o $(BIN)/latex.o $(BIN)/cond.o $(BIN)/include.o \
           $(BIN)/logodata.o $(BIN)/config.o $(BIN)/hyphlist.o
$(BIN)/strprint: $(SRC)/strprint.c $(LIBOBJ) $(PRINTOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the numbered backups and of the settings for them.
$(BIN)/bordertest: tests/bordertest.c $(BIN)/border.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)

$(BIN)/recovertest: tests/recovertest.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)

# The check of the text of a LaTeX marker (step (a) of its picture).
$(BIN)/lmtest: tests/lmtest.c $(BIN)/latexmark.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)

$(BIN)/calctest: tests/calctest.c $(BIN)/calc.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)

$(BIN)/backuptest: tests/backuptest.c $(LIBOBJ) $(BIN)/config.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the colors of the screen and of their settings.
$(BIN)/colortest: tests/colortest.c $(LIBOBJ) $(BIN)/config.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/lxtest: tests/lxtest.c $(LIBOBJ) $(PRINTOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# Hostile and damaged pictures for the PNG reader.
$(BIN)/pngtest: tests/pngtest.c $(BIN)/png.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# Damaged and hostile document files for the loader.
$(BIN)/loadtest: tests/loadtest.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The version of the file format: the first record, the writer and the
# special backup (tests/vertest.c).
$(BIN)/vertest: tests/vertest.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The section tags of a document as the Editor draws them (tests/secnum.py).
$(BIN)/sectags: tests/sectags.c $(LIBOBJ) $(EDOBJ) $(BIN)/latex.o \
        $(BIN)/logodata.o $(BIN)/cond.o $(BIN)/include.o $(BIN)/$(TERMO).o $(BIN)/termbuf.o \
        $(BIN)/export.o $(BIN)/png.o $(BIN)/eqrender.o $(BIN)/config.o \
        $(BIN)/fonts.o $(BIN)/hyphlist.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the preview: the progress reader, the estimate, the
# temporary directory and the child process.
$(BIN)/pvtest: tests/pvtest.c $(LIBOBJ) $(PRINTOBJ) $(BIN)/pvcore.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/pvest: tests/pvest.c $(LIBOBJ) $(PRINTOBJ) $(BIN)/pvcore.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the print job: the arguments of lp, the refused values.
$(BIN)/pjtest: tests/pjtest.c $(LIBOBJ) $(BIN)/pvcore.o $(BIN)/printjob.o \
        | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the search for programs: the fixed folders, no PATH.
$(BIN)/sptest: tests/sptest.c $(BIN)/sysprog.o $(BIN)/log.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of conditional text: the variables, the expression of the
# If marker, the filtered copy of a document and the map of its positions.
# The unit tests of the personal word list (Maintain Dictionaries).
$(BIN)/dicttest: tests/dicttest.c $(BIN)/dictlist.o $(BIN)/hyphlist.o $(BIN)/spell.o \
        $(BIN)/config.o $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the matcher of Search and Replace.
$(BIN)/searchtest: tests/searchtest.c $(BIN)/search.o $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the comparison of two documents.
$(BIN)/cmptest: tests/cmptest.c $(BIN)/compare.o $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/condtest: tests/condtest.c $(LIBOBJ) $(PRINTOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the Include marker: the joined document, the loops, the
# depth, the errors and the map of positions.
$(BIN)/incltest: tests/incltest.c $(LIBOBJ) $(PRINTOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
# The unit tests of the log of a run: the socket interface and the collector.
$(BIN)/logtest: tests/logtest.c $(BIN)/log.o $(BIN)/logrun.o | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN)/msdump: tests/msdump.c $(LIBOBJ) | $(BIN)
	$(Q)echo "  LD   $@"
	$(Q)$(CC) $(CFLAGS) $(DEPFLAGS) -MF $@.d -o $@ \
	    $(filter %.c %.o,$^) $(LDLIBS)
$(BIN):
	mkdir -p $(BIN)

# No test touches the settings folder of the person who runs it: the tests of
# this target get a folder of their own and a home of their own, and the last
# step fails if something wrote the settings into that home.
test: export STRATA_HOME = $(O)/testhome/strata
test: export HOME = $(O)/testhome/user
test: $(BIN)/roundtrip $(BIN)/tabcheck $(BIN)/edtest $(BIN)/memtest \
        $(BIN)/lxtest $(BIN)/pvtest $(BIN)/pjtest $(BIN)/sptest $(BIN)/perrtest \
        $(BIN)/bigtest $(BIN)/roundtrip $(BIN)/stred $(BIN)/str \
        $(BIN)/strprint \
        $(BIN)/logtest $(BIN)/condtest $(BIN)/incltest $(BIN)/sectags \
        $(BIN)/helptest \
        $(BIN)/cmptest $(BIN)/colortest $(BIN)/undotest $(BIN)/listtest \
        $(BIN)/backuptest \
        $(BIN)/calctest $(BIN)/bordertest $(BIN)/recovertest \
        $(BIN)/lmtest $(BIN)/dicttest \
        $(BIN)/searchtest $(BIN)/pngtest $(BIN)/loadtest $(BIN)/vertest \
        $(O)/strata.hlp
	rm -rf $(O)/testhome
	mkdir -p $(STRATA_HOME) $(HOME)
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/depcheck.py $(O) \
	$(STRATA_BUILD_TYPE)
	$(BIN)/helptest tests/data/help_test.txt $(O)/strata.hlp
	$(BIN)/undotest
	$(BIN)/listtest
	$(BIN)/backuptest
	$(BIN)/calctest
	$(BIN)/lmtest
	$(BIN)/bordertest
	$(BIN)/recovertest tests/data
	$(BIN)/dicttest
	$(BIN)/searchtest
	$(BIN)/pngtest tests/data
	$(BIN)/loadtest tests/data $(DOCSRC)
	$(BIN)/vertest
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/helpcheck.py
	$(BIN)/lxtest tests/data
	@for f in tests/data/secnum_*.doc; do \
	    $(BIN)/sectags --check $$f | tail -1 | grep -q ' 0 failures' || \
	    { echo "sectags: the numbers of $$f differ"; \
	      $(BIN)/sectags --check $$f | grep FAIL | head -5; exit 1; }; \
	done; echo "sectags: the section numbers and the parts of the documents ok"
	$(BIN)/condtest
	$(BIN)/incltest
	$(BIN)/cmptest
	$(BIN)/colortest $(O)
	$(BIN)/logtest
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/logmsgs.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/logodata.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/nomshelp.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/setupenv.py $(O)
	PYTHONDONTWRITEBYTECODE=1 STRPRINT=$(BIN)/strprint \
	python3 -I -B tests/srctree.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/checkpics.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/picscripts.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/strdev.py
	$(BIN)/perrtest tests/data $(BIN)
	$(BIN)/pvtest tests/data
	$(BIN)/pjtest tests/fakelpstat.sh
	$(BIN)/sptest
	PYTHONDONTWRITEBYTECODE=1 STRATA_BUILD_TYPE=$(STRATA_BUILD_TYPE) \
	python3 -I -B tests/logpolicy.py $(O)
	$(BIN)/roundtrip $(DOCSRC)/*.doc tests/data/*.doc
	$(BIN)/tabcheck $(DOCSRC)/*.doc
	@if [ -f $(DOCSRC)/twain.doc ]; then \
	    $(BIN)/memtest $(DOCSRC)/twain.doc; \
	else echo "memtest: no twain.doc in $(DOCSRC), skipped"; fi
	$(BIN)/edtest $(DOCSRC) $(BIN)/edtest_out.doc tests/data
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/tmpclean.py $(BIN) \
	$(DOCSRC) tests/data
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/samplesguard.py $(BIN)
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/stylerules.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/editorial.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/codestyle.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/version.py $(O)
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/latexname.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/srcwords.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/latexspace.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/installtest.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/gsdoc.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/fileversions.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/menuwidth.py $(BIN)/stred
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/menutext.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/checktools.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/readmemd.py
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/docversion.py
	$(MAKE) docversion
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/big.py quick $(BIN) \
	$(O)/bigwork
	@if [ -e "$(HOME)/.strata" ]; then \
	    echo "test: a test used the settings folder of the home"; exit 1; \
	fi

# Time and memory of opening a large document: make memprof FILE=twain.doc
.PHONY: memprof
memprof: $(BIN)/memprof
	$(BIN)/memprof $(FILE)
# Run strprint on every document of PRINTDOCS by the three routes (needs
# pdflatex, latex, dvipdfmx, dvitype and pdfinfo; PRINTDOCS may list several
# directories separated by colons);
# the PDFs, the DVI files, the .tex files and SUMMARY.txt go to PRINTRES.
PRINTDOCS ?= sample.docs:tests/samples
PRINTPAT  ?= *.doc
PRINTRES  ?= $(O)/printtest
printtest: $(BIN)/strprint
	STRPRINT=$(BIN)/strprint SAMPLES=$(PRINTDOCS) RESULTS=$(PRINTRES) \
	PATTERN='$(PRINTPAT)' tests/printtest.sh all

# The Printer and the Viewer on a synthetic document of more than a
# thousand pages (about four minutes); the Editor part is in "test".
.PHONY: big
big: term
	PYTHONDONTWRITEBYTECODE=1 python3 -I -B tests/big.py full $(BIN) \
	$(O)/bigwork

# The scripts of the smoke target run several at a time (tests/smokerun.py):
# SMOKE_JOBS says how many (default: the CPUs, at most 4; 1 is the serial order
# of the list in that file).  Every script runs, whatever the others do; the
# outputs are shown in the order of the list, with the time of each, and the
# status is not 0 if one failed.  smoke-quick stops every script after
# SMOKE_QUICK_SECONDS seconds (see docs/TESTING.txt for what it leaves out).
SMOKE_JOBS ?=
SMOKE_QUICK_SECONDS ?= 60
SMOKE_ENV = PYTHONDONTWRITEBYTECODE=1 STRED=$(BIN)/stred STR=$(BIN)/str \
            STRPRINT=$(BIN)/strprint RT=$(BIN)/roundtrip \
            SECTAGS=$(BIN)/sectags DOCSRC=$(DOCSRC) \
            STRATA_BUILD_TYPE=$(STRATA_BUILD_TYPE) SMOKE_JOBS=$(SMOKE_JOBS)

smoke: term $(BIN)/sectags
	$(SMOKE_ENV) python3 -I -B tests/smokerun.py

smoke-quick: term $(BIN)/sectags
	$(SMOKE_ENV) python3 -I -B tests/smokerun.py --quick $(SMOKE_QUICK_SECONDS)

# X11 frontend (needs libx11-dev): same programs, a window instead of a terminal
# -> $(O)/x11/
XFT_H := $(wildcard /usr/include/X11/Xft/Xft.h)
ifneq ($(XFT_H),)
XFTFLAGS = -DHAVE_XFT -I/usr/include/freetype2
XFTLIBS  = -lXft -lfontconfig
endif

# The answer of the test for the Xft headers decides how the X11 programs are
# built: the flag -DHAVE_XFT and the libraries.  It is kept in a file of the
# X11 directory, written when the Makefile is read and only when the answer is
# not in it, so that the time of the file changes only when the answer
# changes.  The object of the X11 frontend, the only one that has code for
# Xft, depends on the file: when the headers appear after a build, or go
# away, the next build makes that object and the programs again, and when
# nothing changed nothing is made again.
XFT_ANSWER := $(if $(XFT_H),xft,no-xft)
ifeq ($(BIN),$(O)/x11)
XFT_STAMP      := $(O)/x11/xft-answer
XFT_STAMP_MADE := $(shell mkdir -p $(dir $(XFT_STAMP)); \
    if test ! -f $(XFT_STAMP) || \
       test "$$(cat $(XFT_STAMP))" != '$(XFT_ANSWER)'; then \
        printf '%s\n' '$(XFT_ANSWER)' > $(XFT_STAMP); \
    fi)
$(BIN)/$(TERMO).o: $(XFT_STAMP)
endif
x11:
	$(Q)test -f /usr/include/X11/Xlib.h || { \
	    echo "x11: Xlib.h not found; install libx11-dev."; exit 1; }
	$(Q)test -n "$(XFTFLAGS)" || echo "x11: NOTE: built without Xft (the" \
	    "headers of Xft are not found): the text uses the fonts of the X" \
	    "server.  Install libxft-dev, libfontconfig-dev and" \
	    "libfreetype-dev for the anti-aliased text."
	$(Q)$(MAKE) --no-print-directory $(O)/strata.hlp templates-link
	$(Q)$(MAKE) --no-print-directory BIN=$(O)/x11 TERMO=term_x11 \
	    CFLAGS='$(CFLAGS) $(XFTFLAGS)' \
	LDLIBS='-lX11 $(XFTLIBS)' $(O)/x11/stred $(O)/x11/str \
	    $(O)/x11/strprint

# The DVI viewer of F12, Print, Preview.  Its objects go to the build
# directory; the program is put next to stred in the terminal directory and in
# the X11 directory, where stred looks for it.  Both directories are made
# here, so that the order of the targets and the number of jobs do not matter.
# The viewer needs the headers of X11, FreeType and PNG.  When one of them is
# missing the viewer is not built and the build says so, with the packages
# that are missing and what is lost (the Preview); it is no error, because
# the rest of Strata works without the viewer.  strata-build repeats it in
# its closing lines.
DVIVIEW_HEADERS := X11/Xlib.h:libx11-dev ft2build.h:libfreetype-dev \
                   png.h:libpng-dev
dviview:
	$(Q)missing=""; \
	for spec in $(DVIVIEW_HEADERS); do \
	    h=$${spec%%:*}; \
	    p=$${spec#*:}; \
	    if ! printf '#include <%s>\n' "$$h" | \
	        $(CC) -I/usr/include/freetype2 -E -x c - >/dev/null 2>&1; then \
	        missing="$$missing $$p"; \
	    fi; \
	done; \
	if test -n "$$missing"; then \
	    echo "dviview: NOT BUILT: header files are missing, so the" \
	        "Preview cannot show its pages.  Install$$missing."; \
	    exit 0; \
	fi; \
	mkdir -p $(O)/term $(O)/x11; \
	$(MAKE) --no-print-directory -C tools/dviview \
	    BUILD=$(abspath $(O))/dviview-obj \
	    STRATA_BUILD_TYPE=$(STRATA_BUILD_TYPE) \
	    STRATA_VERSION='$(VERSION)' \
	    PROG=$(abspath $(O))/term/dviview \
	    $(abspath $(O))/term/dviview && \
	cp -f $(O)/term/dviview $(O)/x11/dviview

# The tests must work when the temporary directory has a long path, as it has
# on a machine whose scratch folder is deep: these targets run the smoke tests
# and the X11 tests with a TMPDIR of more than 140 characters.
LONG_TMP_A := a-directory-with-a-very-long-name-for-the-tests-of-a-long-
LONG_TMP_B := temporary-path-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
LONG_TMPDIR := $(O)/$(LONG_TMP_A)$(LONG_TMP_B)

smoke-long:
	mkdir -p $(LONG_TMPDIR)
	TMPDIR=$(LONG_TMPDIR) $(MAKE) smoke

xtest-long:
	mkdir -p $(LONG_TMPDIR)
	TMPDIR=$(LONG_TMPDIR) $(MAKE) xtest

xtest: x11
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 \
	LSAN_OPTIONS=suppressions=$(CURDIR)/tests/lsan.supp \
	python3 -I -B tests/xsmoke.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 python3 -I -B tests/xlogo.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 python3 -I -B tests/xmarkglyph.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 python3 -I -B tests/xkeycap.py
	PYTHONDONTWRITEBYTECODE=1 STRED_X11=$(O)/x11/stred \
	STRATA_HELP=$(O)/strata.hlp python3 -I -B tests/xclose.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 TBIN=$(O)/term \
	STRATA_HELP=$(O)/strata.hlp python3 -I -B tests/xlmpic.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 python3 -I -B tests/xpicscroll.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 python3 -I -B tests/xfallback.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 STRATA_HELP=$(O)/strata.hlp \
	XLAG_RUNS=1 python3 -I -B tests/xlag.py
	PYTHONDONTWRITEBYTECODE=1 XBIN=$(O)/x11 STRATA_HELP=$(O)/strata.hlp \
	XLAG_RUNS=1 XLAG_SLOW_LATEX=3 \
	XLAG_DOCS=$(CURDIR)/manual/doc/GettingStarted.doc \
	python3 -I -B tests/xlag.py

# Run every suite and prove nothing was written into the source tree or the
# original media.
verify: all
	@python3 -I -B tests/tree_guard.py snapshot $(O)/guard.json
	@$(MAKE) --no-print-directory test smoke
	@if [ -f $(O)/x11/stred ]; then $(MAKE) --no-print-directory xtest; fi
	@python3 -I -B tests/tree_guard.py check $(O)/guard.json

# random-key fuzzing in a pty (use the asan build type for a stricter run:
# source <project>/scripts/setup -b asan /tmp/strata)
fuzz: term
	PYTHONDONTWRITEBYTECODE=1 ASAN_OPTIONS=detect_leaks=0 STRED=$(BIN)/stred \
	python3 -I -B tests/fuzz.py 16 120

clean:
	@test -n '$(O)' && rm -rf '$(O)/term' '$(O)/x11'
