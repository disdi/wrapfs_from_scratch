ifeq (${CONFIG_HEPUNION_FS},m)
include ${src}/conf.mk
endif
-include ${src}/priv_def.mk

# cf. include/linux/kernel.h
# enable pr_debug
ccflags-y += -DDEBUG
# sparse doesn't allow spaces
ccflags-y += -D'pr_fmt(fmt)=HEPUNION_NAME"\040%s:%d:%s[%d]:\040"fmt,__func__,__LINE__,current->comm,current->pid'

obj-$(CONFIG_HEPUNION_FS) += hepunion.o
hepunion-y  := dentry.o file.o inode.o main.o super.o lookup.o mmap.o

# all are boolean

