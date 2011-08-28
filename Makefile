
# Note on my Mobile Pentium II, -march=pentium2 delivers twice the performance of i386
#OptDbg=-O3
#OptDbg=-O3 -march=pentium2
OptDbg=-ggdb
CFLAGS=$(OptDbg) -Wall -Werror -Wstrict-prototypes -Wextra -Wno-unused-parameter

# STRIP = -s
INSTALL = /usr/bin/install
DESTDIR = 
BINDIR  = /usr/bin
MANDIR  = /usr/share/man
MAN1DIR = $(MANDIR)/man1
MAN5DIR = $(MANDIR)/man5
LDLIBS = -lncurses

all: wiggle wiggle.man test

wiggle : wiggle.o load.o split.o extract.o diff.o bestmatch.o ReadMe.o \
              merge2.o vpatch.o
wiggle.o load.o split.o extract.o diff.o bestmatch.o ReadMe.o \
               merge2.o vpatch.o : wiggle.h

test: wiggle dotest
	./dotest

wiggle.man : wiggle.1
	nroff -man wiggle.1 > wiggle.man

clean:
	rm -f *.o *.man wiggle .version* demo.patch
	find . -name core -o -name '*.tmp*' -o -name .tmp | xargs rm -f

install : wiggle wiggle.1
	$(INSTALL) -D $(STRIP) -m 755 wiggle $(DESTDIR)$(BINDIR)/wiggle
	$(INSTALL) -D -m 644 wiggle.1 $(DESTDIR)$(MAN1DIR)/wiggle.1

version : ReadMe.c wiggle.1
	@rm -f version
	@sed -n -e 's/.*wiggle - v\([0-9.]*\) - .*/\1/p' ReadMe.c > .version-readme
	@sed -n -e 's/.*WIGGLE 1 "" v\([0-9.]*\)$$/\1/p' wiggle.1 > .version-man
	@cmp -s .version-readme .version-man && cat .version-man > version || { echo Inconsistant versions.; exit 1;}

dist : test clean version
	mkdir -p DIST
	rm -f DIST/wiggle-`cat version`
	git archive --prefix wiggle-`cat version`/  v`cat version` | gzip -9 > DIST/wiggle-`cat version`.tar.gz

v : version
	cat version

demo.patch: force
	diff -ru demo.orig demo.patched | sed 's/demo.patched/demo/' > demo.patch

force:
