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

function target.validate(self, value, section)
    if not value or value == "" then
        return nil, translate("Target cannot be empty")
    end
    
    -- Check for colon separator
    local host, port = value:match("^([^:]+):(%d+)$")
    
    if not host or not port then
        return nil, translate("Invalid format. Expected: host:port (e.g., example.com:51820)")
    end
    
    -- Validate host is not empty
    if host == "" then
        return nil, translate("Host cannot be empty")
    end
    
    -- Validate port
    local port_num = tonumber(port)
    if not port_num or port_num < 1 or port_num > 65535 then
        return nil, translate("Invalid port. Must be between 1 and 65535")
    end
    
    return value
end

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
max_dummy.datatype = "range(0,255)"
max_dummy.default = "4"

static_bindings = s:option(TextValue, "static_bindings", translate("Static Bindings"), 
    translate("Static bindings for two-way mode. Enter each binding as ip:port:localport, one per line."))
static_bindings.rows = 3
static_bindings.placeholder = "1.2.3.4:12883:6670\n5.6.7.8:12083:6679"
static_bindings.optional = true

function static_bindings.validate(self, value, section)
    if not value or value == "" then
        return value
    end
    
    -- Split by newlines and commas
    local bindings = {}
    for line in value:gmatch("[^\r\n]+") do
        line = line:match("^%s*(.-)%s*$")  -- trim whitespace
        if line ~= "" then
            table.insert(bindings, line)
        end
    end
    
    -- Validate each binding
    for _, binding in ipairs(bindings) do
        -- Format: ip:port:port or comma-separated list
        for item in binding:gmatch("[^,]+") do
            item = item:match("^%s*(.-)%s*$")  -- trim whitespace
            if item ~= "" then
                -- Check format: ip:port:port
                local parts = {}
                for part in item:gmatch("[^:]+") do
                    table.insert(parts, part)
                end
                
                if #parts ~= 3 then
                    return nil, translate("Invalid format. Expected: ip:port:localport")
                end
                
                -- Validate IP (basic check)
                local ip = parts[1]
                local ip_parts = {}
                for octet in ip:gmatch("[^.]+") do
                    table.insert(ip_parts, tonumber(octet))
                end
                
                if #ip_parts ~= 4 then
                    return nil, translate("Invalid IP address: ") .. ip
                end
                
                for _, octet in ipairs(ip_parts) do
                    if not octet or octet < 0 or octet > 255 then
                        return nil, translate("Invalid IP address: ") .. ip
                    end
                end
                
                -- Validate ports
                local remote_port = tonumber(parts[2])
                local local_port = tonumber(parts[3])
                
                if not remote_port or remote_port < 1 or remote_port > 65535 then
                    return nil, translate("Invalid remote port: ") .. parts[2]
                end
                
                if not local_port or local_port < 1 or local_port > 65535 then
                    return nil, translate("Invalid local port: ") .. parts[3]
                end
            end
        end
    end
    
    return value
end

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