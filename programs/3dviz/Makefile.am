bin_PROGRAMS += programs/3dviz/3dviz

programs_3dviz_3dviz_SOURCES = \
  programs/3dviz/x11.h \
  programs/3dviz/main.cc \
  programs/3dviz/x11.c 
programs_3dviz_3dviz_LDADD = \
  base/libbase.la \
  geometry/libgeometry.la \
  $(GL_LIBS) \
  $(GSL_LIBS) \
  $(X11_LIBS)
