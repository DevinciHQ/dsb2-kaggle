noinst_LTLIBRARIES += \
  base/libbase.la

base_libbase_la_SOURCES = \
  base/columnfile-reader.cc \
  base/columnfile-select.cc \
  base/columnfile-writer.cc \
  base/error.cc \
  base/file.cc \
  base/hash.cc \
  base/random.cc \
  base/string.cc
base_libbase_la_LIBADD = \
  $(CAPNP_LIBS) \
  $(LIBLZ4_LIBS) \
  $(LIBLZMA_LIBS) \
  $(ZLIB_LIBS) \
  -lsnappy
