#
# (C) Copyright 2000-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.
#
# See file CREDITS for list of people who contributed to this
# project.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston,
# MA 02111-1307 USA
#

include $(TOPDIR)/config.mk

LIB	     = $(obj)lib$(BOARD).a
COBJS    = $(BOARD).o
OBJS	  := $(addprefix $(obj),$(COBJS))

# This is how to include our libraries. The .o files
# in these folders get linked in (the library files
# themselves are not used).
LIBDIRS  = $(TOPDIR)/lib_labx $(TOPDIR)/lib_labx/idl

$(LIB): $(OBJS) libs
	$(AR) $(ARFLAGS) $@ $(OBJS) $(LIBDIRS:%=%/*.o)

libs:
# Don't know why do_idls needs to be executed separately.
	$(MAKE) -C $(TOPDIR)/lib_labx/idl do_idls
	$(MAKE) -C $(TOPDIR)/lib_labx/idl
	$(MAKE) -C $(TOPDIR)/lib_labx

.PHONY: libs

distclean:	clean
	rm -f $(LIB) core *.bak $(obj).depend

#########################################################################

# defines $(obj).depend target
include $(SRCTREE)/rules.mk

sinclude $(obj).depend

#########################################################################
