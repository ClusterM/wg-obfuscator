#
# Copyright (C) 2024-2026 Alexey Cluster <cluster@cluster.wtf>
#
# This is free software, licensed under the GNU General Public License v3.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-wg-obfuscator
PKG_VERSION:=1.6
PKG_RELEASE:=1

PKG_LICENSE:=GPL-3.0-only

# CURDIR resolves to the physical directory (openwrt-luci) rather than to the
# symlink the OpenWrt tree uses, so the name luci.mk derives everything else
# from has to be set explicitly.
LUCI_NAME:=luci-app-wg-obfuscator
LUCI_TITLE:=LuCI interface for WireGuard Obfuscator
LUCI_DESCRIPTION:=LuCI web interface for WireGuard Obfuscator configuration and management.
# luci.mk appends +luci-lua-runtime on its own because this package ships luasrc
LUCI_DEPENDS:=+wg-obfuscator +luci-base +luci-compat
LUCI_PKGARCH:=all
LUCI_URL:=https://github.com/ClusterM/wg-obfuscator
LUCI_MAINTAINER:=Alexey Cluster <cluster@cluster.wtf>

# This package lives outside of the LuCI feed, so luci.mk comes from the
# installed feed instead of a relative path.
include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
