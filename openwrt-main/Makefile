#
# Copyright (C) 2024-2025 Alexey Cluster <cluster@cluster.wtf>
#
# This is free software, licensed under the GNU General Public License v3.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=wg-obfuscator
PKG_VERSION:=1.5
PKG_RELEASE:=1

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/ClusterM/wg-obfuscator.git
PKG_SOURCE_DATE:=2025-09-21
PKG_SOURCE_VERSION:=0b547e964ea47541c7ebca57bd7359e5511ea485
PKG_MIRROR_HASH:=skip

PKG_MAINTAINER:=Alexey Cluster <cluster@cluster.wtf>
PKG_LICENSE:=GPL-3.0
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_PARALLEL:=1
PKG_BUILD_DEPENDS:=

include $(INCLUDE_DIR)/package.mk

define Package/wg-obfuscator
  SECTION:=net
  CATEGORY:=Network
  TITLE:=WireGuard Obfuscator
  URL:=https://github.com/ClusterM/wg-obfuscator
  DEPENDS:=+libc
endef

define Package/wg-obfuscator/description
  WireGuard Obfuscator is a tool designed to disguise WireGuard traffic as random data or a different protocol, making it much harder for DPI (Deep Packet Inspection) systems to detect and block.
endef

define Package/wg-obfuscator/conffiles
/etc/config/wg-obfuscator
/etc/wg-obfuscator/wg-obfuscator.conf
endef

define Package/wg-obfuscator/postinst
#!/bin/sh
[ -z "$${IPKG_INSTROOT}" ] || exit 0
echo "Done."
exit 0
endef

define Build/Configure
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		RELEASE=1
endef

define Package/wg-obfuscator/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wg-obfuscator $(1)/usr/bin/
	
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/wg-obfuscator.init $(1)/etc/init.d/wg-obfuscator
	
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/wg-obfuscator.config $(1)/etc/config/wg-obfuscator
	
	$(INSTALL_DIR) $(1)/etc/wg-obfuscator
	$(INSTALL_CONF) ./files/wg-obfuscator.conf $(1)/etc/wg-obfuscator/wg-obfuscator.conf
	
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/wg-obfuscator-config.sh $(1)/usr/libexec/wg-obfuscator-config.sh
endef

$(eval $(call BuildPackage,wg-obfuscator))