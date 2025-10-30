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
  DEPENDS:=+wg-obfuscator +luci-base +luci-compat
  PKGARCH:=all
endef

define Package/luci-app-wg-obfuscator/description
  LuCI web interface for WireGuard Obfuscator configuration and management.
endef

define Package/luci-app-wg-obfuscator/postinst
#!/bin/sh
[ -z "$${IPKG_INSTROOT}" ] || exit 0
echo "Done."
exit 0
endef

define Build/Compile
endef

define Package/luci-app-wg-obfuscator/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci
	cp -pR ./luasrc/* $(1)/usr/lib/lua/luci/
	
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./root/etc/uci-defaults/luci-wg-obfuscator $(1)/etc/uci-defaults/
	
	# Install menu and ACL for ucode LuCI
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-wg-obfuscator.json $(1)/usr/share/luci/menu.d/
	
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-wg-obfuscator.json $(1)/usr/share/rpcd/acl.d/
	
	# Install translations if they exist
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	@if [ -f ./po/ru/wg-obfuscator.po ] && command -v po2lmo >/dev/null 2>&1; then \
		po2lmo ./po/ru/wg-obfuscator.po $(1)/usr/lib/lua/luci/i18n/wg-obfuscator.ru.lmo; \
	fi
endef

$(eval $(call BuildPackage,luci-app-wg-obfuscator))