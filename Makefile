# HousecMIs - A simple home web service to calculate an index from CMIS
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

HAPP=housemech
HROOT=/usr/local
SHARE=$(HROOT)/share/house

# Application build. --------------------------------------------

OBJS=housemech.o housemech_event.o housemech_rule.o housemech_control.o
LIBOJS=

all: housemech

main: housemech.o

clean:
	rm -f *.o *.a housemech

rebuild: clean all

%.o: %.c
	gcc -c -g -Os -I/usr/include/tcl -o $@ $<

housemech: $(OBJS)
	gcc -Os -o housemech $(OBJS) -lhouseportal -lechttp -ltcl -lssl -lcrypto -lm -lrt

# Application files installation --------------------------------

install-scripts:
	mkdir -p $(SHARE)/mech
	cp bootstrap.tcl $(SHARE)/mech
	chmod 644 $(SHARE)/mech/*
	chmod 755 $(SHARE) $(SHARE)/mech

install-ui:
	mkdir -p $(SHARE)/public/mech
	cp public/* $(SHARE)/public/mech
	chmod 644 $(SHARE)/public/mech/*
	chmod 755 $(SHARE) $(SHARE)/public $(SHARE)/public/mech

install-app: install-ui install-scripts
	mkdir -p $(HROOT)/bin
	mkdir -p /var/lib/house
	mkdir -p /etc/house
	rm -f $(HROOT)/bin/housemech
	cp housemech $(HROOT)/bin
	chown root:root $(HROOT)/bin/housemech
	chmod 755 $(HROOT)/bin/housemech
	touch /etc/default/housemech

uninstall-app:
	rm -rf $(SHARE)/public/mech
	rm -rf $(SHARE)/mech
	rm -f $(HROOT)/bin/housemech

purge-app:

purge-config:
	rm -rf /etc/default/housemech

# System installation. ------------------------------------------

include $(SHARE)/install.mak

# Docker installation -------------------------------------------

docker: all
	rm -rf build
	mkdir -p build
	cp Dockerfile build
	mkdir -p build$(HROOT)/bin
	cp housemech build$(HROOT)/bin
	mkdir -p build$(HROOT)/share/house/mech
	cp bootstrap.tcl build$(HROOT)/share/house/mech
	chmod 644 build$(HROOT)/share/house/mech/*
	mkdir -p build$(HROOT)/share/house/public/mech
	cp public/* build$(HROOT)/share/house/public/mech
	chmod 644 build$(HROOT)/share/house/public/mech/*
	cp $(SHARE)/public/house.css build$(HROOT)/share/house/public
	chmod 644 build$(HROOT)/share/house/public/house.css
	cd build ; docker build -t housemech .
	rm -rf build

