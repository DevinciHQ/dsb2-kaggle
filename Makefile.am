BUILT_SOURCES =
EXTRA_INSTALL_TARGETS =
EXTRA_LTLIBRARIES =
bin_PROGRAMS =
bin_SCRIPTS =
check_PROGRAMS =
lib_LTLIBRARIES =
man1_MANS =
nobase_dist_pkgdata_DATA =
nobase_lib_LTLIBRARIES =
noinst_LIBRARIES =
noinst_LTLIBRARIES =
noinst_PROGRAMS =
noinst_SCRIPTS =

TESTS = $(check_PROGRAMS)

AM_TESTS_ENVIRONMENT = PYTHONPATH=$(srcdir)/generated/python:$(srcdir)/python/.libs; export PYTHONPATH;

AM_CPPFLAGS = -I$(srcdir) -I. $(CAPNP_CFLAGS)
AM_CFLAGS = -g -Wall -march=native -O3
AM_CXXFLAGS = -g -Wall -march=native -O3
AM_LDFLAGS = -lpthread

ACLOCAL_AMFLAGS = -I m4

include $(srcdir)/base/Makefile.am
include $(srcdir)/geometry/Makefile.am
include $(srcdir)/programs/3dviz/Makefile.am
include $(srcdir)/python/Makefile.am

install-exec-local: $(EXTRA_INSTALL_TARGETS)
