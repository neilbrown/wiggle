# Note on my Mobile Pentium II, -march=pentium2 delivers twice the performance of i386
#OptDbg=-O3
#OptDbg=-O3 -march=pentium2
CFLAGS=$(OptDbg) -Wall -Werror

# STRIP = 
INSTALL = /usr/bin/install
DESTDIR = 
BINDIR  = /usr/bin
MANDIR  = /var/share/man
MAN1DIR = $(MANDIR)/man1
MAN5DIR = $(MANDIR)/man5
LDLIBS=-lncurses

all: wiggle wiggle.man test

vpatch : vpatch.o extract.o split.o diff.o


# comment
wiggle : wiggle.o load.o split.o extract.o diff.o bestmatch.o ReadMe.o merge.o
wiggle.o load.o split.o extract.o diff.o bestmatch.o ReadMe.o merge.o : wiggle.h

test: wiggle dotest
	sh dotest

wiggle.man : wiggle.1
	nroff -man wiggle.1 > wiggle.man

clean:
	rm -f *.o *.man wiggle .version* version
	find . -name core -o -name '*.tmp*' -o -name .tmp | xargs rm -f

install : wiggle wiggle.1
	$(INSTALL) -D $(STRIP) -m 755 wiggle $(DESTDIR)$(BINDIR)/wiggle
	$(INSTALL) -D -m 644 wiggle.1 $(DESTDIR)$(MAN1DIR)/wiggle.1

version : ReadMe.c wiggle.1 Readme.c
	@rm -f version
	@sed -n -e 's/.*wiggle - v\([0-9.]*\) - .*/\1/p' ReadMe.c > .version-readme
	@sed -n -e 's/.*WIGGLE 1 "" v\([0-9.]*\)$$/\1/p' wiggle.1 > .version-man
	@cmp -s .version-readme .version-man && cat .version-man > version || { echo Inconsistant versions.; exit 1;}

dist : test clean version Test
	mkdir -p DIST
	rm -f DIST/wiggle-`cat version`
	ln -s .. DIST/wiggle-`cat version`
	tar czvf DIST/wiggle-`cat version`.tar.gz -h -C DIST --exclude RCS --exclude DIST wiggle-`cat version`
	rm -f DIST/wiggle-`cat version`

v : version
	cat version
