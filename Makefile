
SRC=dsr-module.c dsr-pkt.c dsr-dev.c dsr-io.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-rerr.c dsr-ack.c dsr-srt.c send-buf.c debug.c neigh.c maint-buf.c

NS_SRC=dsr-pkt.c dsr-io.c dsr-opt.c dsr-rreq.c dsr-rrep.c dsr-rerr.c dsr-ack.c dsr-srt.c send-buf.c neigh.c maint-buf.c link-cache.c

NS_SRC_CPP=ns-agent.cc

DEFS=-DDEBUG 

MODNAME=dsr
RTC_TRG=linkcache
RTC_SRC=link-cache.c

ifneq (,$(findstring 2.6,$(KERNELRELEASE)))

EXTRA_CFLAGS += -DKERNEL26 $(DEFS)

obj-m += $(MODNAME).o 
$(MODNAME)-objs := $(SRC:%.c=%.o)

obj-m += $(RTC_TRG).o
$(RTC_TRG)-objs := $(RTC_SRC:%.c=%.o)

else

export-objs := link-cache.o

KOBJS := $(SRC:%.c=%.o)

KERNEL=$(shell uname -r)
KERNEL_DIR=/lib/modules/$(KERNEL)/build
KERNEL_INC=$(KERNEL_DIR)/include

CC=gcc
CPP=g++

MIPS_CC=mipsel-linux-gcc
MIPS_LD=mipsel-linux-ld

# NS2
OBJS_NS=$(NS_SRC:%.c=%-ns.o)
OBJS_NS_CPP=$(NS_SRC_CPP:%.cc=%-ns.o)

NS_DEFS= # DON'T CHANGE (overridden by NS Makefile)

# Set extra DEFINES here. Link layer feedback is now a runtime option.
EXTRA_NS_DEFS=-DDEBUG

# Note: OPTS is overridden by NS Makefile
NS_CFLAGS=$(OPTS) $(CPP_OPTS) $(DEBUG) $(NS_DEFS) $(EXTRA_NS_DEFS)

NS_INC= # DON'T CHANGE (overridden by NS Makefile)

NS_TARGET=libdsruu-ns2.a
# Archiver and options
AR=ar
AR_FLAGS=rcs

#######
VERSION=$(shell if [ ! -d $(KERNEL_DIR) ]; then echo "No linux source found!!! Check your setup..."; exit; fi; grep ^VERSION $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
PATCHLEVEL=$(shell grep ^PATCHLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
SUBLEVEL=$(shell grep ^SUBLEVEL $(KERNEL_DIR)/Makefile | cut -d' ' -f 3)
#######

KDEFS=-D__KERNEL__ -DMODULE $(DEFS) -mips2 -O2 -fno-pic -mno-abicalls -mlong-calls -G0 -msoft-float -DEXPORT_SYMTAB -DCONFIG_MODVERSIONS -DMODVERSIONS -include $(KERNEL_INC)/linux/modversions.h 

KINC=-nostdinc $(shell $(CC) -print-search-dirs | sed -ne 's/install: \(.*\)/-I \1include/gp') -I$(KERNEL_INC)
KCFLAGS=-Wall -fno-strict-aliasing -O2 $(KDEFS) $(KINC)
MIPSDEFS=-mips2 $(KDEFS)

.PHONY: mips default depend clean ns

# Check for kernel version
ifeq ($(PATCHLEVEL),6)
default: $(MODNAME).ko $(RTC_TRG).ko TODO
else 
# Assume kernel 2.4
default: $(MODNAME).o $(RTC_TRG).o TODO
endif

mips:  
	$(MAKE) default CC=$(MIPS_CC) LD=$(MIPS_LD) 

$(MODNAME).ko: $(SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(RTC_TRG).ko: $(RTC_SRC) Makefile
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) MODVERDIR=$(PWD) modules

$(KOBJS): %.o: %.c Makefile
	$(CC) $(KCFLAGS) -c -o $@ $<

$(MODNAME).o: $(KOBJS)
	$(LD) -r $^ -o $@

$(RTC_TRG).o: $(RTC_SRC) Makefile
	$(CC) $(KCFLAGS) -c -o $@ $<

$(OBJS_NS_CPP): %-ns.o: %.cc Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

$(OBJS_NS): %-ns.o: %.c Makefile
	$(CPP) $(NS_CFLAGS) $(NS_INC) -c -o $@ $<

ns: dsr-uu.o $(NS_TARGET)

dsr-uu.o: endian.h $(OBJS_NS_CPP) $(OBJS_NS)

$(NS_TARGET): dsr-uu.o
	$(AR) $(AR_FLAGS) $@ $(OBJS_NS_CPP) $(OBJS_NS) > /dev/null

endian.h: endian.c
	$(CC) $(CFLAGS) -o endian endian.c
	./endian > endian.h

depend:
	@echo "Updating Makefile dependencies..."
	@makedepend -Y./ -- $(DEFS) -- $(SRC) &>/dev/null

TODO:
	grep -n "TODO:" *.c *.h > TODO
	cat TODO

TAGS: *.c *.h
	etags.emacs $(SRC) *.h

clean:
	rm -rf .*ko* $(RTC_TRG).*mod* $(MODNAME).*mod* .*cmd .tmp_versions *~ *.ko *.o *.ver Makefile.bak .*o.d TAGS TODO endian endian.h $(NS_TARGET)
endif
# DO NOT DELETE

dsr-module.o: dsr.h dsr-pkt.h timer.h dsr-dev.h dsr-io.h debug.h neigh.h
dsr-module.o: dsr-rreq.h maint-buf.h send-buf.h
dsr-pkt.o: dsr-opt.h dsr.h dsr-pkt.h timer.h
dsr-dev.o: debug.h dsr.h dsr-pkt.h timer.h kdsr.h dsr-opt.h dsr-rreq.h
dsr-dev.o: link-cache.h tbl.h list.h dsr-srt.h dsr-ack.h send-buf.h
dsr-dev.o: maint-buf.h dsr-io.h
dsr-io.o: dsr-dev.h dsr.h dsr-pkt.h timer.h dsr-rreq.h dsr-rrep.h dsr-srt.h
dsr-io.o: debug.h dsr-ack.h dsr-rtc.h maint-buf.h neigh.h dsr-opt.h
dsr-io.o: link-cache.h tbl.h list.h send-buf.h
dsr-opt.o: debug.h dsr.h dsr-pkt.h timer.h dsr-opt.h dsr-rreq.h dsr-rrep.h
dsr-opt.o: dsr-srt.h dsr-rerr.h dsr-ack.h
dsr-rreq.o: debug.h dsr.h dsr-pkt.h timer.h tbl.h list.h dsr-rrep.h dsr-srt.h
dsr-rreq.o: dsr-rreq.h dsr-opt.h link-cache.h send-buf.h neigh.h
dsr-rrep.o: dsr.h dsr-pkt.h timer.h debug.h tbl.h list.h dsr-rrep.h dsr-srt.h
dsr-rrep.o: dsr-rreq.h dsr-opt.h link-cache.h send-buf.h
dsr-rerr.o: dsr.h dsr-pkt.h timer.h dsr-rerr.h dsr-opt.h debug.h dsr-srt.h
dsr-rerr.o: dsr-ack.h link-cache.h tbl.h list.h maint-buf.h
dsr-ack.o: tbl.h list.h debug.h dsr-opt.h dsr.h dsr-pkt.h timer.h dsr-ack.h
dsr-ack.o: link-cache.h neigh.h maint-buf.h
dsr-srt.o: dsr.h dsr-pkt.h timer.h dsr-srt.h debug.h dsr-opt.h dsr-ack.h
dsr-srt.o: link-cache.h tbl.h list.h neigh.h dsr-rrep.h
send-buf.o: tbl.h list.h send-buf.h dsr.h dsr-pkt.h timer.h debug.h
send-buf.o: link-cache.h dsr-srt.h
debug.o: debug.h dsr.h dsr-pkt.h timer.h
neigh.o: tbl.h list.h neigh.h dsr.h dsr-pkt.h timer.h debug.h
maint-buf.o: dsr.h dsr-pkt.h timer.h debug.h tbl.h list.h neigh.h dsr-ack.h
maint-buf.o: link-cache.h dsr-rerr.h maint-buf.h
