#  Copyright (c) 2017 Afero, Inc. All rights reserved.

include $(TOPDIR)/rules.mk

PKG_NAME:=otamgr
PKG_VERSION:=0.1
PKG_RELEASE:=1

USE_SOURCE_DIR:=$(CURDIR)/pkg

PKG_BUILD_PARALLEL:=1
PKG_FIXUP:=autoreconf
PKG_INSTALL:=1
PKG_USE_MIPS16:=0

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/nls.mk

define Package/otamgr
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=OTA Manager Sample Code
  DEPENDS:=+libevent2 +libpthread +libevent2-pthreads +af-ipc +af-util +attrd
  URL:=http://www.afero.io
endef

define Package/otamgr/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/otamgr $(1)/usr/bin/
endef

define Build/Clean
    $(RM) -rf $(CURDIR)/pkg/src/.deps/*
    $(RM) -rf $(CURDIR)/pkg/src/*.o
    $(RM) -rf $(CURDIR)/pkg/autom4te.cache/*
    $(RM) -rf $(CURDIR)/pkg/ipkg-install/*
    $(RM) -rf $(CURDIR)/pkg/ipkg-ar71xx/otamgr/*
    $(RM) -rf $(CURDIR)/.quilt_checked  $(CURDIR)/.prepared $(CURDIR)/.configured_ $(CURDIR)/.built
    $(RM) -rf $(STAGING_DIR)/pkginfo/otamgr.*
    $(RM) -rf $(STAGING_DIR)/root-ar71xx/usr/bin/otamgr
endef

$(eval $(call BuildPackage,otamgr))

