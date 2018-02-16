
ACLOCAL_AMFLAGS = -I m4

AM_MAKEFLAGS = --no-print-directory

bin_PROGRAMS = src/iwpand

src_iwpand_SOURCES = src/main.c \
			src/dbus.h src/dbus.c \
			src/phy.h src/phy.c \
			src/lowpan.h src/lowpan.c
src_iwpand_LDADD = @ELL_LIBS@ -ldl

src_iwpand_CFLAGS = @ELL_CFLAGS@

AM_CFLAGS = -fvisibility=hidden

MAINTAINERCLEANFILES = Makefile.in configure config.h.in aclocal.m4

DISTCLEANFILES =
