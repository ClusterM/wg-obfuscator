module("luci.controller.wg-obfuscator", package.seeall)

function index()
    if not nixio.fs.access("/usr/bin/wg-obfuscator") then
        return
    end

    local page = entry({"admin", "services", "wg-obfuscator"}, cbi("wg-obfuscator"), _("WireGuard Obfuscator"), 60)
    page.dependent = true
    page.acl_depends = { "luci-app-wg-obfuscator" }

    entry({"admin", "services", "wg-obfuscator", "status"}, call("action_status"), nil).leaf = true
    entry({"admin", "services", "wg-obfuscator", "restart"}, call("action_restart"), nil).leaf = true
end

function action_status()
    local sys = require "luci.sys"
    local status = {
        running = (sys.call("pgrep -f '/usr/bin/wg-obfuscator' >/dev/null 2>&1") == 0),
        config_exists = nixio.fs.access("/etc/wg-obfuscator/wg-obfuscator.conf")
    }
    
    luci.http.prepare_content("application/json")
    luci.http.write_json(status)
end

function action_restart()
    local sys = require "luci.sys"
    local result = sys.call("/etc/init.d/wg-obfuscator restart >/dev/null 2>&1")
    
    luci.http.prepare_content("application/json")
    luci.http.write_json({ success = (result == 0) })
end