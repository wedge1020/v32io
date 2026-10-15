# ----------------------------------------------------------------------------
#   v32io C library: the headers (v32io.h, keyboard.h, mouse.h), their demo
#   programs, and the stamping of the library version.
#
#   Used by itself from here, or by the Makefile at the top of the project,
#   which calls it for the headers and the demos (the manual pages,
#   documents and the archive are handled there). The sibling library,
#   v32dev, has its own Makefile in ../v32dev.
#
#       make                   build the demo cartridges (bin/*.v32)
#       make check             compile the headers and the demos only
#       make version           stamp VERSION into the headers (see below)
#       make install           copy the headers next to the Vircon32 headers
#       make uninstall         remove them again
#       make clean
#
#   The Vircon32 DevTools (compile, assemble, packrom) are found on the
#   PATH, in $(DEVTOOLS) if you set it, or where they install by default.
# ----------------------------------------------------------------------------

HEADERS  = v32io.h keyboard.h mouse.h
DEMOS    = v32kbd v32mouse
BUILD    = demos/build
BIN      = demos/bin

# The library version: ../../VERSION is the single source (YYYYMMDD-dev or
# YYYYMMDD-release). `make version` copies it, and today's date, into the
# headers: V32IO_VERSION and V32IO_VERSION_NUMBER in v32io.h, and the
# "File version" date at the top of each header. Portable between GNU and
# BSD/macOS tools: no `sed -i` (BSD sed takes the script as the backup
# suffix), and a header is rewritten only when a line really changes, so
# its mtime says when it was last stamped.
VERSION        ?= $(shell cat ../../VERSION 2>/dev/null)
VERSION_NUMBER  = $(shell printf '%s' "$(VERSION)" | sed 's/^\([0-9]*\).*/\1/')
HDRDATE        ?= $(shell LC_ALL=C date +%Y/%m/%d)

# Where the Vircon32 DevTools are installed: <prefix>/Vircon32/DevTools on
# Linux and macOS, <prefix>/DevTools on Windows (MSYS2/MinGW, where make
# sees OS=Windows_NT). The folder is called DevTools when installed (the
# source tree calls it DevelopmentTools); DEVTOOLS_DIRNAME says which. The
# headers go flat in its include/ folder (no subfolders), which is where
# `compile` looks for the ones it includes -- so a program says
# #include "keyboard.h" and nothing else.
PREFIX  ?= /usr/local
DEVTOOLS_DIRNAME ?= DevTools
ifeq ($(OS),Windows_NT)
  DEVTOOLS_DEFAULT = $(PREFIX)/$(DEVTOOLS_DIRNAME)
else
  DEVTOOLS_DEFAULT = $(PREFIX)/Vircon32/$(DEVTOOLS_DIRNAME)
endif
INCDIR  ?= $(DEVTOOLS_DEFAULT)/include

# the compiler tools: $(DEVTOOLS) if set, else the ones on the PATH, else
# the install location (this is only a default: set DEVTOOLS to override)
DEVTOOLS ?= $(shell p=`command -v compile 2>/dev/null` && dirname "$$p")
ifeq ($(DEVTOOLS),)
  DEVTOOLS = $(DEVTOOLS_DEFAULT)
endif
COMPILE  = $(DEVTOOLS)/compile
ASSEMBLE = $(DEVTOOLS)/assemble
PACKROM  = $(DEVTOOLS)/packrom

.PHONY: all demos check install uninstall version clean

all: demos

# ----------------------------------------------------------------------------
#   Demos. The Vircon32 compiler has no -I option: a program finds its own
#   headers by relative path (demos include "../keyboard.h"), and the
#   Vircon32 ones in the include/ folder of the DevTools.
# ----------------------------------------------------------------------------

demos: $(addprefix $(BIN)/,$(addsuffix .v32,$(DEMOS)))

$(BUILD) $(BIN):
	mkdir -p $@

$(BUILD)/%.asm: demos/%.c $(HEADERS) | $(BUILD)
	$(COMPILE) $< -o $@

$(BUILD)/%.vbin: $(BUILD)/%.asm
	$(ASSEMBLE) $< -o $@

# packrom finds the binary named in the XML relative to the XML itself, so
# the XML is copied beside it
$(BIN)/%.v32: $(BUILD)/%.vbin demos/%.xml | $(BIN)
	cp demos/$*.xml $(BUILD)/$*.xml
	cd $(BUILD) && $(PACKROM) $*.xml -o ../../$@

# only compile (and assemble): no cartridges written
check: $(addprefix $(BUILD)/,$(addsuffix .vbin,$(DEMOS)))
	@echo "headers and demos compile with $(COMPILE)"

# ----------------------------------------------------------------------------
#   Install: the headers to the include/ folder of the DevTools. Mode 0644
#   whatever the umask; DESTDIR is honored (for packaging).
# ----------------------------------------------------------------------------

install:
	umask 022; install -d "$(DESTDIR)$(INCDIR)"
	umask 022; install -m 644 $(HEADERS) "$(DESTDIR)$(INCDIR)/"
	@echo "Installed $(HEADERS) to $(DESTDIR)$(INCDIR)"

uninstall:
	rm -f $(addprefix "$(DESTDIR)$(INCDIR)"/,$(HEADERS))
	@echo "Removed $(HEADERS) from $(DESTDIR)$(INCDIR)"

# ----------------------------------------------------------------------------
#   Version: stamp $(VERSION) into the headers
# ----------------------------------------------------------------------------

# $(call stamp,file,sed script): apply the script, keep the file as it was
# if nothing changed
define stamp
	@sed '$(2)' "$(1)" > "$(1).tmp" || exit 1; \
	if cmp -s "$(1)" "$(1).tmp"; then rm -f "$(1).tmp"; \
	else mv "$(1).tmp" "$(1)"; echo "stamped $(1)"; fi
endef

version:
	@case "$(VERSION)" in \
	    [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-*) ;; \
	    *) echo "bad VERSION '$(VERSION)': expected YYYYMMDD-dev or YYYYMMDD-release" >&2; exit 1 ;; \
	esac
	$(call stamp,v32io.h,s/^\(\#define V32IO_VERSION[[:space:]]*\)"[^"]*"/\1"$(VERSION)"/; s/^\(\#define V32IO_VERSION_NUMBER[[:space:]]*\)[0-9]*/\1$(VERSION_NUMBER)/)
	$(foreach h,$(HEADERS),$(call stamp,$(h),s|\(File version: \)[0-9][0-9][0-9][0-9]/[0-9][0-9]/[0-9][0-9]|\1$(HDRDATE)|)$(newline))
	@echo "v32io $(VERSION) ($(HDRDATE)): $(HEADERS)"

define newline


endef

clean:
	rm -rf $(BUILD)
