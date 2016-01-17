lib_LTLIBRARIES += \
  python/_dsb2.la
noinst_SCRIPTS += \
  generated/python/dsb2.py

BUILT_SOURCES += \
  generated/python/swig_wrap.cc

EXTRA_INSTALL_TARGETS += \
  python-module-install

PYTHON_LIB_HDR = \
  python/columnfile.h

PYTHON_LIB_DEPS = \
  base/libbase.la

python__dsb2_la_SOURCES = \
  generated/python/swig_wrap.cc \
  python/columnfile-reader.cc \
  python/columnfile.cc
python__dsb2_la_LDFLAGS = -module
python__dsb2_la_CXXFLAGS = -fno-strict-aliasing
python__dsb2_la_CPPFLAGS = $(PYTHON2_CFLAGS) $(AM_CPPFLAGS)
python__dsb2_la_LIBADD = $(PYTHON_LIB_DEPS)

generated/python/swig_wrap.cc: python/dsb2.swig $(PYTHON_LIB_HDR)
	$(MKDIR_P) generated/python
	$(AM_V_GEN)$(SWIG) $(SWIG_PYTHON_OPT) -I$(srcdir) $(OUTPUT_OPTION) python/dsb2.swig

generated/python/dsb2.py: generated/python/swig_wrap.cc
	@if test -f $@; then :; else \
		rm -f generated/python/swig_wrap.cc; \
		$(MAKE) $(AM_MAKEFLAGS) generated/python/swig_wrap.cc; \
	fi

python-module-install:
	@$(MKDIR_P) $(DESTDIR)$(LIBDIR)/python2.7/dist-packages
	@$(LN_S) -f $(LIBDIR)/_dsb2.so $(DESTDIR)$(LIBDIR)/python2.7/dist-packages/_dsb2.so
	@$(INSTALL_DATA) generated/python/dsb2.py $(DESTDIR)$(LIBDIR)/python2.7/dist-packages/dsb2.py
