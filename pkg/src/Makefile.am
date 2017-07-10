AUTOMAKE_OPTIONS = subdir-objects
bin_PROGRAMS = otamgr

otamgr_SOURCES = otamgr.c
otamgr_LDADD = -L. -levent_pthreads -lpthread -levent -laf_ipc -laf_util -laf_attr
otamgr_CFLAGS = -Wall -ggdb3

