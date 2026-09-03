#
# Copyright (C) 2024-2026 Alexey Cluster <cluster@cluster.wtf>
#
# This is free software, licensed under the GNU General Public License v3.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=wg-obfuscator
PKG_VERSION:=1.6
PKG_RELEASE:=1

# Point WG_OBFUSCATOR_SRC at a source checkout to build that tree instead of
# the pinned upstream revision. build.sh and the CI workflow set it so that
# local changes are actually compiled.
WG_OBFUSCATOR_SRC:=$(strip $(WG_OBFUSCATOR_SRC))

ifeq ($(WG_OBFUSCATOR_SRC),)
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/ClusterM/wg-obfuscator.git
PKG_SOURCE_DATE:=2026-08-18
PKG_SOURCE_VERSION:=6440304054a27158b6d373545925880f0e51bafb
# Feed submission requires a real hash here; regenerate it on a configured
# build tree with: make package/wg-obfuscator/check FIXUP=1
PKG_MIRROR_HASH:=skip
endif

PKG_MAINTAINER:=Alexey Cluster <cluster@cluster.wtf>
PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/wg-obfuscator
  SECTION:=net
  CATEGORY:=Network
  TITLE:=WireGuard Obfuscator
  URL:=https://github.com/ClusterM/wg-obfuscator
endef

define Package/wg-obfuscator/description
  WireGuard Obfuscator is a tool designed to disguise WireGuard traffic as random data or a different protocol, making it much harder for DPI (Deep Packet Inspection) systems to detect and block.
endef

define Package/wg-obfuscator/conffiles
/etc/config/wg-obfuscator
endef

ifneq ($(WG_OBFUSCATOR_SRC),)
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(WG_OBFUSCATOR_SRC)/Makefile $(WG_OBFUSCATOR_SRC)/LICENSE $(PKG_BUILD_DIR)/
	$(CP) $(WG_OBFUSCATOR_SRC)/*.c $(WG_OBFUSCATOR_SRC)/*.h $(PKG_BUILD_DIR)/
endef
endif

define Build/Configure
endef

# The upstream Makefile appends -pthread to CFLAGS/LDFLAGS, but command line
# variables override that, so it has to be passed explicitly.
define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -pthread" \
		LDFLAGS="$(TARGET_LDFLAGS) -pthread" \
		RELEASE=1
endef

define Package/wg-obfuscator/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wg-obfuscator $(1)/usr/bin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wg-obfuscator.init $(1)/etc/init.d/wg-obfuscator

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/wg-obfuscator.config $(1)/etc/config/wg-obfuscator

	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/wg-obfuscator-config.sh $(1)/usr/libexec/wg-obfuscator-config.sh
endef

$(eval $(call BuildPackage,wg-obfuscator))
