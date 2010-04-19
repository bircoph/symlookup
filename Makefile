# symlookup Makefile
# Copyright © 2007-2009 Andrew Savchenko
#
# This file is part of symlookup.
#
# symlookup is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation
#
# symlookup is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License version 3 for more details.
#
# You should have received a copy of the GNU General Public License version 3
# along with symlookup. If not, see <http://www.gnu.org/licenses/>.
#

include config.mak

.PHONY: all tags clean distclean install uninstall

all: symlookup

symlookup: *.c *.h
	$(CC) *.c $(CFLAGS) $(LFLAGS) -o symlookup
	$(STRIP)

install:
	install -d $(DESTDIR)$(bindir) \
		   $(DESTDIR)$(docdir)/symlookup \
		   $(DESTDIR)$(mandir)/man1
	install -m 0755 symlookup  $(DESTDIR)$(bindir)/symlookup
	install -m 0644 AUTHORS   $(DESTDIR)$(docdir)/symlookup
	install -m 0644 Changelog $(DESTDIR)$(docdir)/symlookup
	install -m 0644 LICENSE   $(DESTDIR)$(docdir)/symlookup
	install -m 0644 README    $(DESTDIR)$(docdir)/symlookup
	install -m 0644 TODO      $(DESTDIR)$(docdir)/symlookup
	install -m 0644 symlookup.1	$(DESTDIR)$(mandir)/man1/symlookup.1
	gzip --best $(DESTDIR)$(mandir)/man1/symlookup.1

clean:
	rm -f symlookup

tags:
	ctags *.c *.h

distclean:
	rm -f symlookup config.mak configure.log tags

uninstall:
	rm -f $(DESTDIR)$(bindir)/symlookup
	rm -rf $(DESTDIR)$(docdir)/symlookup
	rm -f $(DESTDIR)$(mandir)/man1/symlookup.1.gz
