sbin_PROGRAMS = cellulard

cellulard_CFLAGS = $(CELLULARD_CFLAGS) -DOSSOLOG_COMPILE=1

cellulard_LDADD = $(CELLULARD_LIBS)

cellulard_LDFLAGS = $(LAUNCHER_DEPS_LIBS) -Wl,--as-needed

cellulard_SOURCES = cellulard.c

MAINTAINERCLEANFILES = Makefile.in
