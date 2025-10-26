#
# Copyright (C) 2024 WireGuard Obfuscator
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=wg-obfuscator
PKG_VERSION:=1.5
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/ClusterM/wg-obfuscator/archive/refs/tags/v$(PKG_VERSION).tar.gz
PKG_HASH:=skip

PKG_MAINTAINER:=Cluster <cluster@cluster.wtf>
PKG_LICENSE:=GPL-2.0
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

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
/etc/wg-obfuscator.conf
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