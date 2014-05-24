LIBS      := $(shell pkg-config --cflags gtk+-2.0 webkit-1.0 libsoup-2.4 gthread-2.0)
ARCH      := $(shell uname -m)
COMMIT    := $(shell git log | head -n1 | sed "s/.* //")
DEBUG     := -ggdb -Wall -W -DG_ERRORCHECK_MUTEXES

CFLAGS    := -std=c99 $(LIBS) $(DEBUG) -DARCH="\"$(ARCH)\"" -DCOMMIT="\"$(COMMIT)\"" -fPIC -Wextra -pedantic -ggdb3
LDFLAGS   := $(shell pkg-config --libs gtk+-2.0 webkit-1.0 libsoup-2.4 gthread-2.0) -pthread $(LDFLAGS)

PREFIX    ?= $(DESTDIR)/usr
BINDIR    ?= $(PREFIX)/bin
REUZBLDATA  ?= $(PREFIX)/share/reuzbl
DOCSDIR   ?= $(PREFIX)/share/reuzbl/docs
EXMPLSDIR ?= $(PREFIX)/share/reuzbl/examples

all: reuzbl

reuzbl: reuzbl.c reuzbl.h config.h

%: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(LIBS) -o $@ $<

test: reuzbl
	./reuzbl --uri http://www.reuzbl.org

test-config: reuzbl
	./reuzbl --uri http://www.reuzbl.org < examples/configs/sampleconfig-dev

test-config-real: reuzbl
	./reuzbl --uri http://www.reuzbl.org < $(EXMPLSDIR)/configs/sampleconfig

clean:
	rm -f reuzbl

install:
	install -d $(BINDIR)
	install -d $(DOCSDIR)
	install -d $(EXMPLSDIR)
	install -D -m755 reuzbl $(BINDIR)/reuzbl
	cp -ax docs/*   $(DOCSDIR)
	cp -ax config.h $(DOCSDIR)
	cp -ax examples/* $(EXMPLSDIR)
	install -D -m644 AUTHORS $(DOCSDIR)
	install -D -m644 README $(DOCSDIR)


uninstall:
	rm -rf $(BINDIR)/reuzbl
	rm -rf $(REUZBLDATA)
