VERSION=0.3-svn
OPTFLAGS?=-g -O2
CXXFLAGS=-Wall $(OPTFLAGS) -DVERSION=\"$(VERSION)\"

PREFIX?=/usr/local/
BINDIR?=$(DESTDIR)/$(PREFIX)/bin/
MANDIR?=$(DESTDIR)/usr/share/man/

export VERSION MANDIR

all: splitpipe joinpipe docs


# output of g++ -MM *.cc
joinpipe.o: joinpipe.cc misc.hh md5.hh
main.o: main.cc misc.hh
md5.o: md5.cc md5.hh
misc.o: misc.cc misc.hh
splitpipe.o: splitpipe.cc misc.hh ringbuffer.hh md5.hh

docs: 
	cd doc; make

clean:
	rm -rf *~ *.o splitpipe 
	cd tests; make clean
	cd doc; make clean

common_OBJECTS=misc.o md5.o
splitpipe_OBJECTS=splitpipe.o joinpipe.o main.o display.o $(common_OBJECTS)

splitpipe: $(splitpipe_OBJECTS)
	g++ $(splitpipe_OBJECTS) -o $@ -lncurses

joinpipe: splitpipe
	ln -sf splitpipe joinpipe

check: all
	cd tests; make

OUTPUTDIR=splitpipe-$(VERSION)
dist:
	rm -rf $(OUTPUTDIR)
	mkdir -p $(OUTPUTDIR)/tests/{,wrong-order,wrong-volume-in-session}
	cp Makefile PLAN *.cc *.hh CHANGELOG COPYING TODO README HACKING cdrecord-script $(OUTPUTDIR)
	cp tests/{*.cc,Makefile,*Run} $(OUTPUTDIR)/tests
	cp tests/wrong-order/test* $(OUTPUTDIR)/tests/wrong-order
	cp tests/wrong-volume-in-session/test* $(OUTPUTDIR)/tests/wrong-volume-in-session
	cd doc; make dist
	tar cvzf $(OUTPUTDIR).tar.gz $(OUTPUTDIR)/
	rm -rf $(OUTPUTDIR)

install: all 
	@mkdir -p $(BINDIR)
	install -s splitpipe $(BINDIR)
	ln -sf splitpipe $(BINDIR)/joinpipe
	cd doc; make install


