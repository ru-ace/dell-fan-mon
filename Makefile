# Makefile for i8kutils
#
# Copyright (C) 2013-2017  Vitor Augusto <vitorafsr@gmail.com>
# Copyright (C) 2001-2005  Massimo Dal Zotto <dz@debian.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.

CFLAGS:=-g -O2 -fstack-protector-strong -Wformat -Werror=format-security -Wall
LDFLAGS:=-Wl,-Bsymbolic-functions -Wl,-z,relro

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

all: i8kmon-ng

i8kmon-ng: i8kmon-ng.c i8kmon-ng.h

clean:
	rm -f i8kmon-ng *.o

install:
	service i8kmon-ng stop
	install ./i8kmon-ng /usr/bin/
#	mv /etc/i8kmon-ng.conf /etc/i8kmon-ng.conf.prev
#	cp ./i8kmon-ng.conf /etc/
	service i8kmon-ng start
