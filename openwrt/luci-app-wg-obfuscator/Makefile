#
# Copyright (C) 2024 WireGuard Obfuscator
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-wg-obfuscator
PKG_VERSION:=1.5
PKG_RELEASE:=1

PKG_MAINTAINER:=Cluster <cluster@cluster.wtf>
PKG_LICENSE:=GPL-2.0

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-wg-obfuscator
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI interface for WireGuard Obfuscator
  DEPENDS:=+wg-obfuscator +luci-base
  PKGARCH:=all
endef

define Package/luci-app-wg-obfuscator/description
  LuCI web interface for WireGuard Obfuscator configuration and management.
endef

define Build/Compile
endef

define Package/luci-app-wg-obfuscator/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci
	cp -pR ./luasrc/* $(1)/usr/lib/lua/luci/
	
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./root/etc/uci-defaults/luci-wg-obfuscator $(1)/etc/uci-defaults/
	
	# Install translations if they exist
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	@if [ -f ./po/zh-cn/wg-obfuscator.po ] && command -v po2lmo >/dev/null 2>&1; then \
		po2lmo ./po/zh-cn/wg-obfuscator.po $(1)/usr/lib/lua/luci/i18n/wg-obfuscator.zh-cn.lmo; \
	fi
endef

$(eval $(call BuildPackage,luci-app-wg-obfuscator))