# ----------------------------------------------------------------------------
#   v32io and v32dev: Vircon32 peripherals and external I/O.
#
#     v32io    keyboard and mouse, read through gamepad ports
#     v32dev   files, a terminal and more, through the memory card
#
#   Project Makefile, laid out after the v32c++ one. CMakeLists.txt does the
#   same installs (`cmake`, `cmake --install`, `cpack`); the two never share
#   build products.
#
#       make                 build the demo cartridges (lib/*/demos/bin)
#       make test            checks that need nothing but this tree
#       make realcheck       the end-to-end tests (needs the Vircon32 sources)
#       make version         show the version and stamp it (see below)
#       make sysinstall      headers, manual pages, documents (may need sudo)
#       make sysuninstall    remove them again
#       make archive         v32io-project.zip
#       make tarball         v32io-<version>.tar.gz
#       make clean
#
#   WHAT IS INSTALLED. There are three install targets, chosen with COMPONENTS
#   (or by naming the target). A component's manual pages, documents and demos
#   are installed only together with its headers:
#
#       make sysinstall                      v32io only (the default)
#       make sysinstall COMPONENTS=v32dev    v32dev only
#       make sysinstall COMPONENTS=both      both
#
#   The same, as targets: sysinstall-v32io, sysinstall-v32dev,
#   sysinstall-both, and sysuninstall-v32io, sysuninstall-v32dev,
#   sysuninstall-both. `make sysuninstall` removes what COMPONENTS names.
#
#   PREFIX (default /usr/local) and DESTDIR (for packaging) work as usual.
#   The firmware (firmware/RP2350-USB-A, -USB-C, -USB-CM) and the DesktopEmulator patches
#   (emulator/v32io, emulator/v32dev) are built on their own: see their
#   README files.
# ----------------------------------------------------------------------------

LIBDIR   = lib
MANDIR   = man
TOOLS    = tools

# What `make sysinstall` and its relatives work on: v32io, v32dev or both.
COMPONENTS ?= v32io
ALL_COMPONENTS = v32io v32dev
ifeq ($(COMPONENTS),both)
  COMPONENT_LIST = $(ALL_COMPONENTS)
else ifneq ($(filter $(COMPONENTS),$(ALL_COMPONENTS)),)
  COMPONENT_LIST = $(COMPONENTS)
else
  $(error COMPONENTS must be v32io, v32dev or both (it is '$(COMPONENTS)'))
endif

# The version: the VERSION file is the single source (YYYYMMDD-dev or
# YYYYMMDD-release), for both components. `make version` shows it and
# stamps it, with today's date, into the headers (lib/*/Makefile do that
# part) and the .TH line of every manual page. `make version
# VERSION=20261101-release` sets a new one first. Portable between GNU and
# BSD/macOS tools: no `sed -i` (BSD sed takes the script as the backup
# suffix), no `date +%-d`.
VERSION ?= $(shell cat VERSION 2>/dev/null)
MANDATE := $(shell LC_ALL=C date +"%B %d, %Y" | sed 's/ 0\([1-9]\),/ \1,/')
HDRDATE := $(shell LC_ALL=C date +%Y/%m/%d)

# The manual pages: man/<component>/man3, in section 3v32 (like the Vircon32
# C library ones). A page that is only `.so <other page>` is an alias and has
# no .TH line of its own.
MAN3_v32io  := $(wildcard $(MANDIR)/v32io/man3/*.3v32)
MAN3_v32dev := $(wildcard $(MANDIR)/v32dev/man3/*.3v32)

PREFIX  ?= /usr/local
MANROOT ?= $(PREFIX)/share/man

# Where the Vircon32 DevTools live, and the shared folder for community
# tools next to them: <prefix>/Vircon32/DevTools and <prefix>/Vircon32/v32tools
# on Linux and macOS; <prefix>/DevTools and <prefix>/v32tools on Windows
# (MSYS2/MinGW, where make sees OS=Windows_NT). The headers of both
# components go flat in the DevTools' include/ folder (no subfolders), as
# `compile` looks for the ones it includes there; the documents in
# v32tools/docs/<component>.
#
# The folder is called DevTools once installed (its source tree in the
# ComputerSoftware repository is called DevelopmentTools): set
# DEVTOOLS_DIRNAME if yours is named differently.
DEVTOOLS_DIRNAME ?= DevTools
ifeq ($(OS),Windows_NT)
  VIRCON32 = $(PREFIX)
else
  VIRCON32 = $(PREFIX)/Vircon32
endif
INCDIR  ?= $(VIRCON32)/$(DEVTOOLS_DIRNAME)/include
DOCROOT ?= $(VIRCON32)/v32tools/docs
DOCDIR   = $(DOCROOT)/$(C)

ARCHIVE_EXCLUDE = 'tests/v32io/build/*' 'lib/*/demos/build/*' 'build/*' '*.zip' '*.tar.gz' '*.tmp' '._*' '*/.DS_Store'

.PHONY: all demos check test realcheck realcheck-v32io realcheck-v32dev version \
        install uninstall sysinstall sysuninstall \
        sysinstall-v32io sysinstall-v32dev sysinstall-both \
        sysuninstall-v32io sysuninstall-v32dev sysuninstall-both \
        install-component uninstall-component manpages archive tarball clean

all: demos

# Builds the demo cartridges of both components when the Vircon32 compiler is
# around (DEVTOOLS or the PATH); without it, the cartridges that ship in
# lib/*/demos/bin are used as they are.
demos:
	@dt="$(DEVTOOLS)"; [ -n "$$dt" ] || { p=`command -v compile 2>/dev/null` && dt=`dirname "$$p"`; }; \
	if [ -n "$$dt" ] && [ -x "$$dt/compile" ]; then \
	    for c in $(ALL_COMPONENTS); do $(MAKE) -C $(LIBDIR)/$$c demos DEVTOOLS="$$dt" || exit 1; done; \
	else echo "Vircon32 DevTools not found (set DEVTOOLS): using the demo cartridges in lib/*/demos/bin"; fi

# ----------------------------------------------------------------------------
#   Tests
# ----------------------------------------------------------------------------

# Everything that needs only this tree, for both components: the version
# stamps agree, the manual pages are well formed (and, if the Vircon32
# compiler is around, their examples compile), and the headers and demos
# compile with it. The compiler comes from DEVTOOLS or the PATH; without it
# those two steps are skipped.
check: test
test:
	@sh $(TOOLS)/check-version.sh
	@python3 $(TOOLS)/lint-man.py all
	@dt="$(DEVTOOLS)"; [ -n "$$dt" ] || { p=`command -v compile 2>/dev/null` && dt=`dirname "$$p"`; }; \
	if [ -n "$$dt" ] && [ -x "$$dt/compile" ]; then \
	    python3 $(TOOLS)/lint-man.py --compile "$$dt" all || exit 1; \
	    for c in $(ALL_COMPONENTS); do $(MAKE) -C $(LIBDIR)/$$c check DEVTOOLS="$$dt" || exit 1; done; \
	else echo "skipping the compile checks: Vircon32 DevTools not found (set DEVTOOLS)"; fi

# The end-to-end tests of tests/: the library running on the console of the
# Vircon32 emulator, for the component(s) that COMPONENTS names.
#
#   v32io:  the firmware logic and the library. Needs the Vircon32 sources
#           and TinyUSB: see the top of tests/v32io/run.sh for the variables
#           to set (VIRCON, DEVTOOLS, TINYUSB; and EMU, SDL2INC for the
#           emulator device test).
#   v32dev: the patched console and the host tool. Needs the Vircon32 sources
#           with emulator/v32dev/v32dev.patch applied: set
#           COMPUTERSOFTWARE=<that folder> (see tests/v32dev/run.sh).
realcheck: $(addprefix realcheck-,$(COMPONENT_LIST))

realcheck-v32io:
	sh tests/v32io/run.sh

realcheck-v32dev:
	@[ -n "$(COMPUTERSOFTWARE)" ] || { echo "set COMPUTERSOFTWARE=<ComputerSoftware folder with emulator/v32dev/v32dev.patch applied>" >&2; exit 2; }
	bash tests/v32dev/run.sh "$(COMPUTERSOFTWARE)"

# ----------------------------------------------------------------------------
#   Version
# ----------------------------------------------------------------------------

# Show the version and stamp it, with today's date. In every manual page of
# a component, a .TH line whose fourth field is "<component> <anything>"
# becomes
#     .TH <title> <section> "<Month day, year>" "<component> <VERSION>" ...
# (title, section and the fifth field are kept). Alias pages (.so) are left
# alone. A page is rewritten only when its line actually changes, so its
# mtime says when it was last stamped. Run after changing VERSION, before a
# release.
version:
	@case "$(VERSION)" in \
	    [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-*) ;; \
	    *) echo "bad VERSION '$(VERSION)': expected YYYYMMDD-dev or YYYYMMDD-release" >&2; exit 1 ;; \
	esac
	@if [ "`cat VERSION 2>/dev/null`" != "$(VERSION)" ]; then echo "$(VERSION)" > VERSION; echo "stamped VERSION"; fi
	@for c in $(ALL_COMPONENTS); do \
	    $(MAKE) --no-print-directory -C $(LIBDIR)/$$c version VERSION=$(VERSION) HDRDATE=$(HDRDATE) || exit 1; \
	    for f in $(MANDIR)/$$c/man3/*.3v32; do \
	        grep -q "^\.TH .* \"$$c [^\"]*\"" "$$f" || continue; \
	        sed "s/^\(\.TH [^ ]* [^ ]*\) \"[^\"]*\" \"$$c [^\"]*\"/\1 \"$(MANDATE)\" \"$$c $(VERSION)\"/" \
	            "$$f" > "$$f.tmp" || exit 1; \
	        if cmp -s "$$f" "$$f.tmp"; then rm -f "$$f.tmp"; \
	        else mv "$$f.tmp" "$$f"; echo "stamped $$f"; fi; \
	    done; \
	done
	@echo "v32io and v32dev $(VERSION) ($(MANDATE))"
	@for c in $(ALL_COMPONENTS); do grep -h '^\.TH' $(MANDIR)/$$c/man3/*.3v32 \
	    | sed 's/^\.TH \([^ ]*\) \([^ ]*\) \("[^"]*" "[^"]*"\).*/  \1(\2) \3/'; done

# ----------------------------------------------------------------------------
#   Install
# ----------------------------------------------------------------------------

# System-wide install: the C headers of the component(s) to the Vircon32
# DevTools' include/ folder, flat (v32io.h, keyboard.h, mouse.h and/or
# v32dev.h, storage.h, serial.h), so a program says #include "keyboard.h"
# with no options; the manual pages to $(MANROOT)/man3; the documents and the
# demo programs to v32tools/docs/<component>. The same layout `cmake
# --install` produces (see CMakeLists.txt). May need sudo. `make sysuninstall`
# removes it again. Nothing of a component that is not named is installed,
# manual pages included.
#
# Manual pages are installed gzip -9'd (page.3v32.gz), mode 0644. An alias
# page (a one-line `.so man3/<page>`) becomes a symbolic link to its
# target's .gz instead -- what Debian's dh_installman does -- since a .so
# inside a compressed page is not followed by every man(1). Permissions are
# set explicitly (umask 022, chmod), never left to the umask sudo runs with.
#
# The demo programs include "../keyboard.h" (they live beside the headers in
# the source tree); installed beside the documents the include is rewritten
# to "keyboard.h", which the compiler finds in its own include/ folder.
#
# What the documents folder of a component holds:
#     README.md     the README of the project
#     LICENSE
#     LIBRARY.md    the README of the component's library (lib/<c>/README.md)
#     PROTOCOL(S).md, COMPOSE.md    the component's protocol documents
#     demos/        the demo programs, their descriptions and cartridges
DOCFILES_v32io  = README.md LICENSE docs/v32io/PROTOCOLS.md docs/v32io/COMPOSE.md
DOCFILES_v32dev = README.md LICENSE docs/v32dev/PROTOCOL.md
DEMOS_v32io     = v32kbd v32mouse
DEMOS_v32dev    = storage

INCPARENTS = $(patsubst %/,%,$(dir $(INCDIR))) \
             $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(INCDIR)))))
# docs/, v32tools/ and the Vircon32 folder above the documents (written out
# by name: the folder itself is gone by the time they are pruned)
DOCPARENTS = $(DOCROOT) \
             $(patsubst %/,%,$(dir $(DOCROOT))) \
             $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(DOCROOT)))))

install: sysinstall
uninstall: sysuninstall

sysinstall:
	@for c in $(COMPONENT_LIST); do $(MAKE) --no-print-directory install-component C=$$c || exit 1; done

sysinstall-v32io:
	@$(MAKE) --no-print-directory sysinstall COMPONENTS=v32io
sysinstall-v32dev:
	@$(MAKE) --no-print-directory sysinstall COMPONENTS=v32dev
sysinstall-both:
	@$(MAKE) --no-print-directory sysinstall COMPONENTS=both

sysuninstall:
	@for c in $(COMPONENT_LIST); do $(MAKE) --no-print-directory uninstall-component C=$$c || exit 1; done
	-@command -v mandb >/dev/null 2>&1 && [ -z "$(DESTDIR)" ] && mandb -q 2>/dev/null; true

sysuninstall-v32io:
	@$(MAKE) --no-print-directory sysuninstall COMPONENTS=v32io
sysuninstall-v32dev:
	@$(MAKE) --no-print-directory sysuninstall COMPONENTS=v32dev
sysuninstall-both:
	@$(MAKE) --no-print-directory sysuninstall COMPONENTS=both

# The work for one component, C (v32io or v32dev); run by sysinstall.
install-component: demos
	@case "$(C)" in v32io|v32dev) ;; *) echo "install-component: C must be v32io or v32dev" >&2; exit 2 ;; esac
	$(MAKE) -C $(LIBDIR)/$(C) install PREFIX=$(PREFIX) INCDIR=$(INCDIR)
	umask 022; install -d "$(DESTDIR)$(MANROOT)/man3" "$(DESTDIR)$(DOCDIR)/demos"
	@umask 022; for f in $(MAN3_$(C)); do \
	    dest="$(DESTDIR)$(MANROOT)/man3/$${f##*/}"; \
	    rm -f "$$dest" "$$dest.gz"; \
	    if head -n 1 "$$f" | grep -q '^\.so '; then \
	        target=`sed -n '1s|^\.so [^/]*/||p' "$$f"`; \
	        ln -s "$$target.gz" "$$dest.gz" || exit 1; \
	    else \
	        gzip -9 -n -c "$$f" > "$$dest.gz" || exit 1; \
	        chmod 0644 "$$dest.gz" || exit 1; \
	    fi; \
	done
	umask 022; install -m 644 $(DOCFILES_$(C)) "$(DESTDIR)$(DOCDIR)/"
	umask 022; install -m 644 $(LIBDIR)/$(C)/README.md "$(DESTDIR)$(DOCDIR)/LIBRARY.md"
	@umask 022; for d in $(DEMOS_$(C)); do \
	    sed 's|"\.\./\([a-z0-9]*\.h\)"|"\1"|' $(LIBDIR)/$(C)/demos/$$d.c > "$(DESTDIR)$(DOCDIR)/demos/$$d.c" || exit 1; \
	    chmod 0644 "$(DESTDIR)$(DOCDIR)/demos/$$d.c"; \
	    install -m 644 $(LIBDIR)/$(C)/demos/$$d.xml "$(DESTDIR)$(DOCDIR)/demos/"; \
	    install -m 644 $(LIBDIR)/$(C)/demos/bin/$$d.v32 "$(DESTDIR)$(DOCDIR)/demos/"; \
	done
	chmod -R go+rX "$(DESTDIR)$(DOCDIR)"
	chmod go+rx $(addprefix "$(DESTDIR)",$(INCPARENTS) $(DOCPARENTS) $(MANROOT) $(MANROOT)/man3) 2>/dev/null; true
	-@command -v mandb >/dev/null 2>&1 && [ -z "$(DESTDIR)" ] && mandb -q 2>/dev/null; true
	@echo "Installed $(C): the headers to $(DESTDIR)$(INCDIR),"
	@echo "the manual pages (gzipped) to $(DESTDIR)$(MANROOT)/man3"
	@echo "and the documents and demos to $(DESTDIR)$(DOCDIR)"

# Removes exactly what install-component put down for C, then each directory
# that leaves empty: the include folder (and the DevTools folder above it)
# only if nothing else (the other component, the compiler's own headers...)
# is in it; v32tools/docs, v32tools and Vircon32 only if nothing else (the
# other component, v32c++, v32opt...) is in them.
uninstall-component:
	@case "$(C)" in v32io|v32dev) ;; *) echo "uninstall-component: C must be v32io or v32dev" >&2; exit 2 ;; esac
	$(MAKE) -C $(LIBDIR)/$(C) uninstall PREFIX=$(PREFIX) INCDIR=$(INCDIR)
	@for f in $(MAN3_$(C)); do \
	    rm -f "$(DESTDIR)$(MANROOT)/man3/$${f##*/}" "$(DESTDIR)$(MANROOT)/man3/$${f##*/}.gz"; \
	done
	rm -rf "$(DESTDIR)$(DOCDIR)"
	-@for d in $(addprefix "$(DESTDIR)",$(DOCPARENTS)) $(addprefix "$(DESTDIR)",$(INCDIR) $(INCPARENTS)); do \
	    rmdir "$$d" 2>/dev/null && echo "removed empty $$d"; \
	done; true

# ----------------------------------------------------------------------------
#   Archives
# ----------------------------------------------------------------------------

# COPYFILE_DISABLE and zip -X / tar's excludes keep macOS from adding its
# AppleDouble ._* files and extended attributes to the archive. -r matters:
# without it a directory stores only its own entry and the archive silently
# ships empty folders.
archive: clean
	COPYFILE_DISABLE=1 zip -r -X ../v32io-project.zip * -x $(ARCHIVE_EXCLUDE)

tarball: clean
	COPYFILE_DISABLE=1 tar --exclude='tests/v32io/build' --exclude='lib/*/demos/build' \
	    --exclude='*.zip' --exclude='*.tar.gz' --exclude='._*' --exclude='.DS_Store' \
	    -czf ../v32io-$(VERSION).tar.gz -C .. "$(notdir $(CURDIR))"
	mv ../v32io-$(VERSION).tar.gz .

clean:
	for c in $(ALL_COMPONENTS); do $(MAKE) -C $(LIBDIR)/$$c clean; done
	rm -rf tests/v32io/build build
	rm -f *.zip *.tar.gz ../v32io-project.zip
# The cartridges in lib/*/demos/bin are part of the source tree (so they can
# be used without the DevTools); `make` rebuilds them.
