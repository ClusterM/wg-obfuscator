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

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/ClusterM/wg-obfuscator.git
PKG_SOURCE_VERSION:=v$(PKG_VERSION)
PKG_MIRROR_HASH:=f674e335d34ddf362bb9b3b2df350b9676e329ae64da85e23669e0d35a6f3094

PKG_MAINTAINER:=Alexey Cluster <cluster@cluster.wtf>
PKG_LICENSE:=GPL-3.0-only
PKG_LICENSE_FILES:=LICENSE

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/wg-obfuscator
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=VPN
  TITLE:=WireGuard traffic obfuscator
  URL:=https://github.com/ClusterM/wg-obfuscator
endef

define Package/wg-obfuscator/description
  wg-obfuscator disguises WireGuard traffic as random data or as another
  protocol, which makes it much harder for DPI (Deep Packet Inspection)
  systems to detect and block it. It acts as a UDP proxy in front of a
  WireGuard peer and has to run on both ends of the tunnel with the same
  obfuscation key.
endef

define Package/wg-obfuscator/conffiles
/etc/config/wg-obfuscator
endef

define Build/Configure
endef

# The upstream Makefile appends -pthread to CFLAGS/LDFLAGS, but command line
# variables take precedence over that, so it has to be passed explicitly.
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
