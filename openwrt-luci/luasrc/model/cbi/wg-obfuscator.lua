local fs = require "nixio.fs"
local sys = require "luci.sys"

m = Map("wg-obfuscator", translate("WireGuard Obfuscator Configuration"), 
    translate("Configure WireGuard Obfuscator instances to obfuscate WireGuard traffic."))

-- Add custom CSS and JavaScript
m:append(Template("wg-obfuscator/css"))
m:append(Template("wg-obfuscator/js"))

-- Instance configuration
s = m:section(TypedSection, "wg_obfuscator", translate("Instances"), 
    translate("Configure individual obfuscator instances. Each instance can have different settings."))
s.addremove = true
s.anonymous = false
s.addbtntitle = translate("Add instance")
s.nametitle = translate("Name")

-- Enable flag for each instance
enabled = s:option(Flag, "enabled", translate("Enable"), translate("Enable this instance"))
enabled.default = "0"
enabled.rmempty = false

-- Instance settings
source_lport = s:option(Value, "source_lport", translate("Source Port"), 
    translate("Local port to listen for incoming connections"))
source_lport.datatype = "port"
source_lport.default = "13255"

target = s:option(Value, "target", translate("Target"), 
    translate("Target server in format host:port"))
target.placeholder = "example.com:13255"
target.default = "10.13.1.100:13255"

key = s:option(Value, "key", translate("Obfuscation Key"), 
    translate("Key used for obfuscation (must be the same on both sides)"))
key.password = true
key.default = "test"

source_if = s:option(Value, "source_if", translate("Source Interface"), 
    translate("Interface to bind to (0.0.0.0 for all interfaces)"))
source_if.placeholder = "0.0.0.0"
source_if.default = "0.0.0.0"

masking = s:option(ListValue, "masking", translate("Masking Type"), 
    translate("Protocol masking for DPI evasion"))
masking:value("NONE", translate("None"))
masking:value("AUTO", translate("Auto-detect"))
masking:value("STUN", translate("STUN"))
masking.default = "AUTO"

verbose = s:option(ListValue, "verbose", translate("Log Level"), 
    translate("Verbosity level for logging"))
verbose:value("ERRORS", translate("Errors only"))
verbose:value("WARNINGS", translate("Warnings"))
verbose:value("INFO", translate("Info"))
verbose:value("DEBUG", translate("Debug"))
verbose:value("TRACE", translate("Trace"))
verbose.default = "INFO"

max_clients = s:option(Value, "max_clients", translate("Max Clients"), 
    translate("Maximum number of concurrent clients"))
max_clients.datatype = "uinteger"
max_clients.default = "1024"

idle_timeout = s:option(Value, "idle_timeout", translate("Idle Timeout"), 
    translate("Idle timeout in seconds"))
idle_timeout.datatype = "uinteger"
idle_timeout.default = "300"

max_dummy = s:option(Value, "max_dummy", translate("Max Dummy Data"), 
    translate("Maximum dummy data length for packets (0-255)"))
max_dummy.datatype = "uinteger"
max_dummy.default = "4"

static_bindings = s:option(TextValue, "static_bindings", translate("Static Bindings"), 
    translate("Static bindings for two-way mode (format: ip:port:localport, comma-separated)"))
static_bindings.rows = 3
static_bindings.placeholder = "1.2.3.4:12883:6670, 5.6.7.8:12083:6679"

-- Service Status and Control
s2 = m:section(SimpleSection)
s2.template = "wg-obfuscator/status"

s3 = m:section(SimpleSection)
s3.template = "wg-obfuscator/buttons"

function m.on_commit(self)
    -- Regenerate configuration when settings change
    sys.call("/usr/libexec/wg-obfuscator-config.sh >/dev/null 2>&1")
end

return m