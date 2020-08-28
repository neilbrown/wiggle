
# Note on my Mobile Pentium II, -march=pentium2 delivers twice the performance of i386
#OptDbg=-O3
#OptDbg=-O3 -march=pentium2
OptDbg=-ggdb
ifndef CWFLAGS
CWFLAGS=-Wall -Werror -Wstrict-prototypes -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
endif
CFLAGS=$(OptDbg) -I. $(CWFLAGS)

PREFIX  = /usr
# STRIP = -s
INSTALL = /usr/bin/install
DESTDIR =
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1
MAN5DIR = $(MANDIR)/man5
LDLIBS = -lncurses

# Where to place intermediate objects
O=O
# Where to place final objects
BIN=.
DOC=.

LIBOBJ= load.o parse.o split.o extract.o diff.o bestmatch.o merge2.o ccan/hash/hash.o
OBJ=wiggle.o ReadMe.o vpatch.o

BOBJ=$(patsubst %.o,$(O)/%.o,$(OBJ))
BLIBOBJ=$(patsubst %.o,$(O)/%.o,$(LIBOBJ))

all: $(BIN)/wiggle $(DOC)/wiggle.man test
lib : $(O)/libwiggle.a

#
# Pretty print
#
V	      = @
Q	      = $(V:1=)
QUIET_CC      = $(Q:@=@echo    '     CC       '$@;)
QUIET_AR      = $(Q:@=@echo    '     AR       '$@;)
QUIET_LINK    = $(Q:@=@echo    '     LINK     '$@;)
QUIET_MAN     = $(Q:@=@echo    '     MAN      '$@;)
QUIET_CLEAN   = $(Q:@=@echo    '     CLEAN    '$@;)


$(BIN)/wiggle : $(BOBJ) $(O)/libwiggle.a
	$(QUIET_LINK)$(CC) $^ $(LDLIBS) -o $@

$(O)/libwiggle.a : $(BLIBOBJ)
	$(QUIET_AR)ar cr $@ $^

$(BOBJ) :: wiggle.h

$(O)/split.o :: ccan/hash/hash.h config.h

$(BOBJ) $(BLIBOBJ) :: $(O)/%.o : %.c
	@mkdir -p $(dir $@)
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

VERSION = $(shell [ -d .git ] && git 2> /dev/null describe HEAD)
VERS_DATE = $(shell [ -d .git ] && git 2> /dev/null log -n1 --format=format:%cd --date=short)
DVERS = $(if $(VERSION),-DVERSION=\"$(VERSION)\",)
DDATE = $(if $(VERS_DATE),-DVERS_DATE=\"$(VERS_DATE)\",)
CFLAGS += $(DVERS) $(DDATE)

test: wiggle dotest
	./dotest

valgrind: wiggle dotest
	./dotest valgrind

vtest: wiggle dovtest
	./dovtest

$(DOC)/wiggle.man : wiggle.1
	$(QUIET_MAN)nroff -man wiggle.1 > $@

clean: targets artifacts dirs
targets:
	$(QUIET_CLEAN)rm -f $(O)/*.[ao] $(O)/ccan/hash/*.o $(DOC)/*.man $(BIN)/wiggle .version* demo.patch version
artifacts:
	$(QUIET_CLEAN)find . -name core -o -name '*.tmp*' -o -name .tmp -o -name .time | xargs rm -f
dirs : targets artifacts
	$(QUIET_CLEAN)[ -d $(O)/ccan/hash ] && rmdir -p $(O)/ccan/hash || true
	$(QUIET_CLEAN)[ -d $(O) ] && rmdir -p $(O) || true

install : $(BIN)/wiggle wiggle.1
	$(INSTALL) -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MAN1DIR)
	$(INSTALL) $(STRIP) -m 755 $(BIN)/wiggle $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 644 wiggle.1 $(DESTDIR)$(MAN1DIR)

version : ReadMe.c wiggle.1
	@rm -f version
	@sed -n -e 's/.*VERSION "\([0-9.]*\)".*/\1/p' ReadMe.c > .version-readme
	@sed -n -e 's/.*WIGGLE 1 "" v\([0-9.]*\)$$/\1/p' wiggle.1 > .version-man
	@cmp -s .version-readme .version-man && cat .version-man > version || { echo Inconsistant versions.; exit 1;}

dist : test clean version
	mkdir -p DIST
	rm -f DIST/wiggle-`cat version`
	git archive --prefix wiggle-`cat version`/  v`cat version` | gzip -9 > DIST/wiggle-`cat version`.tar.gz

v : version
	cat version

demo.patch: force
	diff -rup demo.orig demo.patched | sed 's/demo.patched/demo/' > demo.patch

force:
