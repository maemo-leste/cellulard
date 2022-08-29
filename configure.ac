AC_INIT([Cellular connectivity daemon], patsubst(esyscmd([dpkg-parsechangelog -SVersion | sed -n '/^\([^-+]*\).*$/ {s//\1/;p}']), [
]), [], [cellulard])
AM_CONFIG_HEADER(config.h)

AC_CONFIG_MACRO_DIR([m4])
AM_MAINTAINER_MODE
AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL

AC_HEADER_STDC

AC_PATH_X
AC_PATH_XTRA
AC_SUBST(X_CFLAGS)

PKG_PROG_PKG_CONFIG

#+++++++++++++++
# Misc programs 
#+++++++++++++++

AC_SUBST(CFLAGS)

PKG_CHECK_MODULES(CELLULARD, glib-2.0 gio-2.0 mce libgofono libglibutil libosso)

AC_SUBST(CELLULARD_LIBS)
AC_SUBST(CELLULARD_CFLAGS)

#+++++++++++++++++++
# Directories setup
#+++++++++++++++++++

AC_OUTPUT([
Makefile
src/Makefile
])

