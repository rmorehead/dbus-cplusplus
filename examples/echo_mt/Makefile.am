EXTRA_DIST = README echo-introspect.xml

AM_CPPFLAGS = -I$(top_srcdir)/include 

noinst_PROGRAMS = echo-server-mt

echo_server_mt_SOURCES = echo-server-mt-glue.h echo-server-mt.h echo-server-mt.cpp
echo_server_mt_LDADD = $(top_builddir)/src/libdbus-c++-1.la $(PTHREAD_LIBS)
echo_server_mt_CXXFLAGS = $(PTHREAD_CFLAGS)

echo-server-mt-glue.h: echo-introspect.xml
	$(top_builddir)/tools/dbusxx-xml2cpp $^ --adaptor=$@

noinst_PROGRAMS += echo-client-mt

echo_client_mt_SOURCES = echo-client-glue.h echo-client.h echo-client.cpp
echo_client_mt_LDADD = $(top_builddir)/src/libdbus-c++-1.la $(PTHREAD_LIBS)
echo_client_mt_CXXFLAGS = $(PTHREAD_CFLAGS)

echo-client-glue.h: echo-introspect.xml
	$(top_builddir)/tools/dbusxx-xml2cpp $^ --proxy=$@

BUILT_SOURCES = echo-server-mt-glue.h echo-client-glue.h
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

MAINTAINERCLEANFILES = \
	Makefile.in
