local fs = require "nixio.fs"
local sys = require "luci.sys"

m = Map("wg-obfuscator", translate("WireGuard Obfuscator Configuration"), 
    translate("Configure WireGuard Obfuscator instances to obfuscate WireGuard traffic."))

-- Global settings
s = m:section(TypedSection, "main", translate("Global Settings"))
s.anonymous = true
s.addremove = false

enabled = s:option(Flag, "enabled", translate("Enable"), translate("Enable WireGuard Obfuscator service"))
enabled.default = "0"

-- Instance configuration
s2 = m:section(TypedSection, "wg_obfuscator", translate("Instances"), 
    translate("Configure individual obfuscator instances. Each instance can have different settings."))
s2.template = "cbi/tblsection"
s2.addremove = true
s2.anonymous = false

-- Instance settings
source_lport = s2:option(Value, "source_lport", translate("Source Port"), 
    translate("Local port to listen for incoming connections"))
source_lport.datatype = "port"
source_lport.default = "13255"

target = s2:option(Value, "target", translate("Target"), 
    translate("Target server in format host:port"))
target.placeholder = "example.com:13255"
target.default = "10.13.1.100:13255"

key = s2:option(Value, "key", translate("Obfuscation Key"), 
    translate("Key used for obfuscation (must be the same on both sides)"))
key.password = true
key.default = "test"

source_if = s2:option(Value, "source_if", translate("Source Interface"), 
    translate("Interface to bind to (0.0.0.0 for all interfaces)"))
source_if.placeholder = "0.0.0.0"
source_if.default = "0.0.0.0"

masking = s2:option(ListValue, "masking", translate("Masking Type"), 
    translate("Protocol masking for DPI evasion"))
masking:value("NONE", translate("None"))
masking:value("AUTO", translate("Auto-detect"))
masking:value("STUN", translate("STUN"))
masking.default = "AUTO"

verbose = s2:option(ListValue, "verbose", translate("Log Level"), 
    translate("Verbosity level for logging"))
verbose:value("ERRORS", translate("Errors only"))
verbose:value("WARNINGS", translate("Warnings"))
verbose:value("INFO", translate("Info"))
verbose:value("DEBUG", translate("Debug"))
verbose:value("TRACE", translate("Trace"))
verbose.default = "INFO"

max_clients = s2:option(Value, "max_clients", translate("Max Clients"), 
    translate("Maximum number of concurrent clients"))
max_clients.datatype = "uinteger"
max_clients.default = "1024"

idle_timeout = s2:option(Value, "idle_timeout", translate("Idle Timeout"), 
    translate("Idle timeout in seconds"))
idle_timeout.datatype = "uinteger"
idle_timeout.default = "300"

max_dummy_length_data = s2:option(Value, "max_dummy_length_data", translate("Max Dummy Data"), 
    translate("Maximum dummy data length for packets"))
max_dummy_length_data.datatype = "uinteger"
max_dummy_length_data.default = "4"

fwmark = s2:option(Value, "fwmark", translate("Firewall Mark"), 
    translate("Firewall mark for packets (0 to disable)"))
fwmark.datatype = "uinteger"
fwmark.default = "0"

static_bindings = s2:option(TextValue, "static_bindings", translate("Static Bindings"), 
    translate("Static bindings for two-way mode (format: ip:port:localport, comma-separated)"))
static_bindings.rows = 3
static_bindings.placeholder = "1.2.3.4:12883:6670, 5.6.7.8:12083:6679"

-- Status section
s3 = m:section(TypedSection, "status", translate("Service Status"))
s3.anonymous = true
s3.addremove = false

status = s3:option(DummyValue, "status", translate("Status"))
status.template = "wg-obfuscator/status"

-- Buttons
buttons = s3:option(DummyValue, "buttons", "")
buttons.template = "wg-obfuscator/buttons"

function m.on_commit(self)
    -- Regenerate configuration when settings change
    sys.call("/usr/libexec/wg-obfuscator-config.sh >/dev/null 2>&1")
end

return m