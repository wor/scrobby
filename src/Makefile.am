bin_PROGRAMS = scrobby
scrobby_SOURCES = callback.cpp configuration.cpp libmpdclient.c misc.cpp \
	mpdpp.cpp scrobby.cpp song.cpp

# set the include path found by configure
AM_CPPFLAGS= $(all_includes)

# the library search path.
scrobby_LDFLAGS = $(all_libraries)
noinst_HEADERS = callback.h configuration.h libmpdclient.h misc.h mpdpp.h \
	scrobby.h song.h
