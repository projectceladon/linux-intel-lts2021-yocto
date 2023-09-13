include Kbuild
ifeq ($(KBUILD_MODULES),)

KERNELRELEASE	?= `uname -r`
KERNEL_DIR	?= /lib/modules/$(KERNELRELEASE)/build
PWD		:= $(shell pwd)

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1
INSTALL = install
INSTALL_PROGRAM = $(INSTALL) -p -m 755
INSTALL_DIR     = $(INSTALL) -p -m 755 -d
INSTALL_DATA    = $(INSTALL) -m 644

MODULE_OPTIONS = devices=2

##########################################
# note on build targets
#
# module-assistant makes some assumptions about targets, namely
#  <modulename>: must be present and build the module <modulename>
#                <modulename>.ko is not enough
# install: must be present (and should only install the module)
#
# we therefore make <modulename> a .PHONY alias to <modulename>.ko
# and remove utils-installation from 'install'
# call 'make install-all' if you want to install everything
##########################################


.PHONY: all install clean distclean

# we don't control the .ko file dependencies, as it is done by kernel
# makefiles. therefore v4l2loopback.ko is a phony target actually
.PHONY: v4l2loopback.ko

all: v4l2loopback.ko

v4l2loopback: v4l2loopback.ko
v4l2loopback.ko:
	@echo "Building v4l2-loopback driver..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	rm -f *~
	rm -f Module.symvers Module.markers modules.order
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	$(MAKE) -C utils clean

distclean: clean
	rm -f man/v4l2loopback-ctl.1

.clang-format:
	curl "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/.clang-format" > $@

.PHONY: clang-format
clang-format: .clang-format
	clang-format -i *.c *.h utils/*.c

endif # !kbuild
