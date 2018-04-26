AUTOMAKE_OPTIONS = subdir-objects
bin_PROGRAMS = otamgr

if BUILD_TARGET_DEBUG
    CFLAGS_BUILD_TARGET = -DBUILD_TARGET_DEBUG
else
    CFLAGS_BUILD_TARGET = -DBUILD_TARGET_RELEASE
endif

otamgr_SOURCES = otamgr.c
otamgr_LDFLAGS = -ggdb3 -L. -levent_pthreads -lpthread -levent -laf_attr -laf_ipc -laf_util
otamgr_CFLAGS = -Wall $(CFLAGS_BUILD_TARGET)

