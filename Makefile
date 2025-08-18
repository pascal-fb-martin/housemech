# HouseMech - A simple home web service to automate actions.
#
# Copyright 2023, Pascal Martin
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA  02110-1301, USA.
#
# WARNING
#
# This Makefile depends on echttp and houseportal (dev) being installed.

prefix=/usr/local
SHARE=$(prefix)/share/house

INSTALL=/usr/bin/install

HAPP=housemech
HCAT=automation

# Application build. --------------------------------------------

OBJS=housemech.o \
     housemech_event.o \
     housemech_rule.o \
     housemech_control.o
LIBOJS=

all: housemech

main: housemech.o

clean:
	rm -f *.o *.a housemech

rebuild: clean all

%.o: %.c
	gcc -c -Wall -g -Os -I/usr/include/tcl -o $@ $<

housemech: $(OBJS)
	gcc -Os -o housemech $(OBJS) -lhouseportal -lechttp -ltcl -lssl -lcrypto -lmagic -lm -lrt

# Application files installation --------------------------------

install-scripts: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/mech
	$(INSTALL) -m 0644 bootstrap.tcl $(DESTDIR)$(SHARE)/mech

install-ui: install-preamble
	$(INSTALL) -m 0755 -d $(DESTDIR)$(SHARE)/public/mech
	$(INSTALL) -m 0644 public/* $(DESTDIR)$(SHARE)/public/mech

install-runtime: install-preamble
	$(INSTALL) -m 0755 -s housemech $(DESTDIR)$(prefix)/bin
	touch $(DESTDIR)/etc/default/housemech

install-app: install-ui install-scripts install-runtime

uninstall-app:
	rm -rf $(DESTDIR)$(SHARE)/public/mech
	rm -rf $(DESTDIR)$(SHARE)/mech
	rm -f $(DESTDIR)$(prefix)/bin/housemech

purge-app:

purge-config:
	rm -rf $(DESTDIR)/etc/default/housemech

# Build a private Debian package. -------------------------------

install-package: install-ui install-runtime install-systemd

debian-package: debian-package-generic

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker installation -------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(prefix)/bin
	cp housemech build$(prefix)/bin
	mkdir -p build$(prefix)/share/house/mech
	cp bootstrap.tcl build$(prefix)/share/house/mech
	chmod 644 build$(prefix)/share/house/mech/*
	mkdir -p build$(prefix)/share/house/public/mech
	cp public/* build$(prefix)/share/house/public/mech
	chmod 644 build$(prefix)/share/house/public/mech/*
	cp $(SHARE)/public/house.css build$(prefix)/share/house/public
	chmod 644 build$(prefix)/share/house/public/house.css
	cd build ; docker build -t housemech .
	rm -rf build

