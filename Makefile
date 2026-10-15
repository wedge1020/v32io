# ----------------------------------------------------------------------------
#   v32io: Vircon32 peripherals (keyboard, mouse) read through gamepad ports.
#   Project Makefile, laid out after the v32c++ one. CMakeLists.txt does the
#   same installs (`cmake`, `cmake --install`, `cpack`); the two never share
#   build products.
#
#       make                 build the demo cartridges (lib/demos/bin)
#       make test            checks that need nothing but this tree
#       make realcheck       the end-to-end tests (needs the Vircon32 sources)
#       make version         show the version and stamp it (see below)
#       make sysinstall      headers, manual pages, documents (may need sudo)
#       make sysuninstall    remove them again
#       make archive         v32io-project.zip
#       make tarball         v32io-<version>.tar.gz
#       make clean
#
#   PREFIX (default /usr/local) and DESTDIR (for packaging) work as usual.
#   The firmware (firmware/RP2350-USB-A) and the DesktopEmulator patches
#   (emulator/) are built on their own: see their README files.
# ----------------------------------------------------------------------------

LIBDIR   = lib
MANDIR   = man
TOOLS    = tools

# The version: the VERSION file is the single source (YYYYMMDD-dev or
# YYYYMMDD-release). `make version` shows it and stamps it, with today's
# date, into the headers (lib/Makefile does that part) and the .TH line of
# every manual page. `make version VERSION=20261101-release` sets a new
# one first. Portable between GNU and BSD/macOS tools: no `sed -i` (BSD sed
# takes the script as the backup suffix), no `date +%-d`.
VERSION ?= $(shell cat VERSION 2>/dev/null)
MANDATE := $(shell LC_ALL=C date +"%B %d, %Y" | sed 's/ 0\([1-9]\),/ \1,/')
HDRDATE := $(shell LC_ALL=C date +%Y/%m/%d)

# The manual pages: man/man3, in section 3v32 (like the Vircon32 C library
# ones). A page that is only `.so <other page>` is an alias and has no .TH
# line of its own.
MAN3    := $(wildcard $(MANDIR)/man3/*.3v32)

PREFIX  ?= /usr/local
MANROOT ?= $(PREFIX)/share/man

# Where the Vircon32 DevTools live, and the shared folder for community
# tools next to them: <prefix>/Vircon32/DevTools and <prefix>/Vircon32/v32tools
# on Linux and macOS; <prefix>/DevTools and <prefix>/v32tools on Windows
# (MSYS2/MinGW, where make sees OS=Windows_NT). The headers go in the
# DevTools' include/ folder, as `compile` looks for the ones it includes
# there; the documents in v32tools/docs/v32io.
ifeq ($(OS),Windows_NT)
  VIRCON32 = $(PREFIX)
else
  VIRCON32 = $(PREFIX)/Vircon32
endif
INCDIR  ?= $(VIRCON32)/DevTools/include
DOCDIR  ?= $(VIRCON32)/v32tools/docs/v32io

ARCHIVE_EXCLUDE = 'v32io-tests/build/*' 'lib/demos/build/*' 'build/*' '*.zip' '*.tar.gz' '*.tmp' '._*' '*/.DS_Store'

.PHONY: all demos check test realcheck version install uninstall sysinstall sysuninstall \
        manpages archive tarball clean

all: demos

demos:
	$(MAKE) -C $(LIBDIR) demos

# ----------------------------------------------------------------------------
#   Tests
# ----------------------------------------------------------------------------

# Everything that needs only this tree: the version stamps agree, the
# manual pages are well formed (and, if the Vircon32 compiler is around, their
# examples compile), and the headers and demos compile with it. The compiler
# comes from DEVTOOLS or the PATH; without it those two steps are skipped.
check: test
test:
	@sh $(TOOLS)/check-version.sh
	@python3 $(TOOLS)/lint-man.py
	@dt="$(DEVTOOLS)"; [ -n "$$dt" ] || { p=`command -v compile 2>/dev/null` && dt=`dirname "$$p"`; }; \
	if [ -n "$$dt" ] && [ -x "$$dt/compile" ]; then \
	    python3 $(TOOLS)/lint-man.py --compile "$$dt" || exit 1; \
	    $(MAKE) -C $(LIBDIR) check DEVTOOLS="$$dt" || exit 1; \
	else echo "skipping the compile checks: Vircon32 DevTools not found (set DEVTOOLS)"; fi

# The end-to-end tests of v32io-tests: the firmware logic and the library
# running on the console of the Vircon32 emulator. Needs its sources and
# TinyUSB: see the top of v32io-tests/run.sh for the variables to set
# (VIRCON, DEVTOOLS, TINYUSB; and EMU, SDL2INC for the emulator device test).
realcheck:
	sh v32io-tests/run.sh

# ----------------------------------------------------------------------------
#   Version
# ----------------------------------------------------------------------------

# Show the version and stamp it, with today's date. In every manual page, a
# .TH line whose fourth field is "v32io <anything>" becomes
#     .TH <title> <section> "<Month day, year>" "v32io <VERSION>" ...
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
	@$(MAKE) --no-print-directory -C $(LIBDIR) version VERSION=$(VERSION) HDRDATE=$(HDRDATE)
	@for f in $(MAN3); do \
	    grep -q '^\.TH .* "v32io [^"]*"' "$$f" || continue; \
	    sed 's/^\(\.TH [^ ]* [^ ]*\) "[^"]*" "v32io [^"]*"/\1 "$(MANDATE)" "v32io $(VERSION)"/' \
	        "$$f" > "$$f.tmp" || exit 1; \
	    if cmp -s "$$f" "$$f.tmp"; then rm -f "$$f.tmp"; \
	    else mv "$$f.tmp" "$$f"; echo "stamped $$f"; fi; \
	done
	@echo "v32io $(VERSION) ($(MANDATE))"
	@grep -h '^\.TH' $(MAN3) | sed 's/^\.TH \([^ ]*\) \([^ ]*\) \("[^"]*" "[^"]*"\).*/  \1(\2) \3/'

# ----------------------------------------------------------------------------
#   Install
# ----------------------------------------------------------------------------

# System-wide install: the C headers to the Vircon32 DevTools' include/
# folder, so a program says #include "keyboard.h" with no options; the manual
# pages to $(MANROOT)/man3; the documents and the demo programs to
# v32tools/docs/v32io. The same layout `cmake --install` produces (see
# CMakeLists.txt). May need sudo. `make sysuninstall` removes it again.
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
DOCFILES = README.md LICENSE docs/PROTOCOLS.md docs/COMPOSE.md
INCPARENTS = $(patsubst %/,%,$(dir $(INCDIR))) \
             $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(INCDIR)))))
# docs/, v32tools/ and the Vircon32 folder above the documents (written out
# by name: the folder itself is gone by the time they are pruned)
DOCPARENTS = $(patsubst %/,%,$(dir $(DOCDIR))) \
             $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(DOCDIR))))) \
             $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(DOCDIR)))))))

install: sysinstall
uninstall: sysuninstall

sysinstall: demos
	$(MAKE) -C $(LIBDIR) install PREFIX=$(PREFIX) INCDIR=$(INCDIR)
	umask 022; install -d "$(DESTDIR)$(MANROOT)/man3" "$(DESTDIR)$(DOCDIR)/demos"
	@umask 022; for f in $(MAN3); do \
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
	umask 022; install -m 644 $(DOCFILES) "$(DESTDIR)$(DOCDIR)/"
	umask 022; install -m 644 $(LIBDIR)/README.md "$(DESTDIR)$(DOCDIR)/LIBRARY.md"
	@umask 022; for d in v32kbd v32mouse; do \
	    sed 's|"\.\./\([a-z0-9]*\.h\)"|"\1"|' $(LIBDIR)/demos/$$d.c > "$(DESTDIR)$(DOCDIR)/demos/$$d.c" || exit 1; \
	    chmod 0644 "$(DESTDIR)$(DOCDIR)/demos/$$d.c"; \
	    install -m 644 $(LIBDIR)/demos/$$d.xml "$(DESTDIR)$(DOCDIR)/demos/"; \
	    install -m 644 $(LIBDIR)/demos/bin/$$d.v32 "$(DESTDIR)$(DOCDIR)/demos/"; \
	done
	chmod -R go+rX "$(DESTDIR)$(DOCDIR)"
	chmod go+rx $(addprefix "$(DESTDIR)",$(INCPARENTS) $(DOCPARENTS) $(MANROOT) $(MANROOT)/man3) 2>/dev/null; true
	-@command -v mandb >/dev/null 2>&1 && [ -z "$(DESTDIR)" ] && mandb -q 2>/dev/null; true
	@echo "Installed the headers to $(DESTDIR)$(INCDIR),"
	@echo "the manual pages (gzipped) to $(DESTDIR)$(MANROOT)/man3"
	@echo "and the documents and demos to $(DESTDIR)$(DOCDIR)"

# Removes exactly what sysinstall put down, then each directory that leaves
# empty: v32tools/docs, v32tools and Vircon32 only if nothing else (the
# DevTools, v32c++, v32opt...) is in them.
sysuninstall:
	$(MAKE) -C $(LIBDIR) uninstall PREFIX=$(PREFIX) INCDIR=$(INCDIR)
	@for f in $(MAN3); do \
	    rm -f "$(DESTDIR)$(MANROOT)/man3/$${f##*/}" "$(DESTDIR)$(MANROOT)/man3/$${f##*/}.gz"; \
	done
	rm -rf "$(DESTDIR)$(DOCDIR)"
	-@for d in $(addprefix "$(DESTDIR)",$(DOCPARENTS)) $(addprefix "$(DESTDIR)",$(INCDIR) $(INCPARENTS)); do \
	    rmdir "$$d" 2>/dev/null && echo "removed empty $$d"; \
	done; true
	-@command -v mandb >/dev/null 2>&1 && [ -z "$(DESTDIR)" ] && mandb -q 2>/dev/null; true

# ----------------------------------------------------------------------------
#   Archives
# ----------------------------------------------------------------------------

# COPYFILE_DISABLE and zip -X / tar's excludes keep macOS from adding its
# AppleDouble ._* files and extended attributes to the archive. -r matters:
# without it a directory stores only its own entry and the archive silently
# ships empty folders.
archive: clean
	COPYFILE_DISABLE=1 zip -r -X v32io-project.zip * -x $(ARCHIVE_EXCLUDE)

tarball: clean
	COPYFILE_DISABLE=1 tar --exclude='v32io-tests/build' --exclude='lib/demos/build' \
	    --exclude='*.zip' --exclude='*.tar.gz' --exclude='._*' --exclude='.DS_Store' \
	    -czf ../v32io-$(VERSION).tar.gz -C .. "$(notdir $(CURDIR))"
	mv ../v32io-$(VERSION).tar.gz .

clean:
	$(MAKE) -C $(LIBDIR) clean
	rm -rf v32io-tests/build build
	rm -f *.zip *.tar.gz
# The cartridges in lib/demos/bin are part of the source tree (so they can be
# used without the DevTools); `make` rebuilds them.
