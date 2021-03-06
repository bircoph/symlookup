# symlookup Makefile
# Copyright © 2007-2011 Andrew Savchenko
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

SRCS = output.c \
       parser.c \
       scanelf.c \
       symlookup.c

ifdef HAVE_RPM
SRCS += rpmutils.c
endif
ifdef HAVE_PORTAGE
SRCS += portageutils.c
endif

OBJS = $(SRCS:.c=.o)
DEPS = $(SRCS:.c=.d)
# remove autogenerated and non-existing headers
HDRS = $(SRCS:.cpp=.h) safemem.h

all: $(DEPS) symlookup

%.d: %.c
	$(CC) -MM -MG $< > $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# rpm's fts conflicts with system-wide one,
# so include rpm inc dir only when needed
ifdef HAVE_RPM
rpmutils.o: rpmutils.c rpmutils.h
	$(CC) $(CFLAGS) $(RPM_INCFLAGS) -c $< -o $@
endif

symlookup: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) ${LIBS} -o symlookup
	$(STRIP)

install:
	install -d $(DESTDIR)$(bindir) \
		   $(DESTDIR)$(docdir)/symlookup-$(VERSION) \
		   $(DESTDIR)$(mandir)/man1
	install -m 0755 symlookup  $(DESTDIR)$(bindir)/symlookup
	install -m 0644 AUTHORS   $(DESTDIR)$(docdir)/symlookup-$(VERSION)
	install -m 0644 Changelog $(DESTDIR)$(docdir)/symlookup-$(VERSION)
	install -m 0644 LICENSE   $(DESTDIR)$(docdir)/symlookup-$(VERSION)
	install -m 0644 README    $(DESTDIR)$(docdir)/symlookup-$(VERSION)
	install -m 0644 TODO      $(DESTDIR)$(docdir)/symlookup-$(VERSION)
	install -m 0644 symlookup.1	$(DESTDIR)$(mandir)/man1/symlookup.1

tags:
	ctags $(SRCS) $(HDRS)

clean:
	rm -f $(OBJS) $(DEPS)

distclean: clean
	rm -f symlookup config.mak configure.log tags

uninstall:
	rm -f $(DESTDIR)$(bindir)/symlookup
	rm -rf $(DESTDIR)$(docdir)/symlookup*
	rm -f $(DESTDIR)$(mandir)/man1/symlookup.1*

# on distclean per-file deps will be removed anyway
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
-include $(DEPS)
endif
endif
