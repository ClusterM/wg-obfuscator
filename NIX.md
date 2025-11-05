# Nix Flake Usage Guide

This repository provides a Nix flake for easy installation and deployment of WireGuard Obfuscator on NixOS and other Nix-based systems.

## Quick Start

### Run directly (without installing)

```bash
nix run github:ClusterM/wg-obfuscator -- --help
```

### Try it out

```bash
nix shell github:ClusterM/wg-obfuscator
wg-obfuscator --version
```

### Install to user profile

```bash
nix profile install github:ClusterM/wg-obfuscator
```

## Development

### Enter development shell

```bash
nix develop
# Now you can use make, gcc, gdb, etc.
make
./wg-obfuscator --help
```

### Build from source

```bash
nix build
./result/bin/wg-obfuscator --version
```

## NixOS Configuration

### Basic Configuration

Add to your `flake.nix` inputs:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    wg-obfuscator = {
      url = "github:ClusterM/wg-obfuscator";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, wg-obfuscator, ... }: {
    nixosConfigurations.your-host = nixpkgs.lib.nixosSystem {
      modules = [
        wg-obfuscator.nixosModules.default
        ./configuration.nix
      ];
    };
  };
}
```

### Simple Server-Side Configuration

In your `configuration.nix`:

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.main = {
      enable = true;
      listenPort = 19999;              # External port clients connect to
      target = "127.0.0.1:51820";      # Local WireGuard server
      key = "your_secret_key";
      masking = "AUTO";                # Auto-detect client masking
    };
  };

  # WireGuard server configuration
  networking.wireguard.interfaces.wg0 = {
    listenPort = 51820;  # Changed from default, obfuscator listens on 19999
    # ... rest of WireGuard config
  };
}
```

### Simple Client-Side Configuration

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.client = {
      enable = true;
      listenPort = 51821;              # Local port WireGuard connects to
      target = "vpn.example.com:19999"; # Remote obfuscator
      key = "your_secret_key";
      masking = "STUN";                # Use STUN masking
    };
  };

  # WireGuard client configuration
  networking.wireguard.interfaces.wg0 = {
    peers = [{
      endpoint = "127.0.0.1:51821";  # Point to local obfuscator
      # ... rest of peer config
    }];
  };
}
```

### Secure Configuration with keyFile

Store your key in a secret file instead of in the Nix store:

```nix
{
  # Using agenix or sops-nix for secret management
  age.secrets.wg-obfuscator-key = {
    file = ./secrets/wg-obfuscator-key.age;
    mode = "400";
  };

  services.wg-obfuscator = {
    enable = true;

    instances.main = {
      enable = true;
      listenPort = 19999;
      target = "127.0.0.1:51820";
      keyFile = config.age.secrets.wg-obfuscator-key.path;  # Load from file
      masking = "AUTO";
    };
  };
}
```

### Advanced Configuration with fwmark

Prevent routing loops when using `AllowedIPs = 0.0.0.0/0`:

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.client = {
      enable = true;
      listenPort = 51821;
      target = "vpn.example.com:19999";
      key = "your_secret_key";
      fwmark = 57005;  # 0xdead in decimal
      masking = "STUN";
    };
  };

  networking.wireguard.interfaces.wg0 = {
    fwMark = 57005;  # Must match obfuscator fwmark
    peers = [{
      endpoint = "127.0.0.1:51821";
      allowedIPs = [ "0.0.0.0/0" ];
      # ... rest of peer config
    }];
  };
}
```

### Two-Way Mode (Peer-to-Peer)

For scenarios where both peers have public IPs and can initiate connections:

**Peer A (1.2.3.4):**

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.p2p = {
      enable = true;
      listenPort = 2222;               # For outgoing to Peer B
      target = "5.6.7.8:4444";
      key = "shared_secret_key";
      staticBindings = "127.0.0.1:1111:3333";  # Local WG port -> external port
    };
  };

  networking.wireguard.interfaces.wg0 = {
    listenPort = 1111;
    peers = [{
      endpoint = "127.0.0.1:2222";
      # ... rest of config
    }];
  };
}
```

**Peer B (5.6.7.8):**

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.p2p = {
      enable = true;
      listenPort = 4444;               # Receives from Peer A
      target = "127.0.0.1:6666";
      key = "shared_secret_key";
      staticBindings = "1.2.3.4:3333:5555";  # Peer A's external port -> local port
    };
  };

  networking.wireguard.interfaces.wg0 = {
    listenPort = 6666;
    peers = [{
      endpoint = "127.0.0.1:5555";
      # ... rest of config
    }];
  };
}
```

### Multiple Instances

Run multiple obfuscators for different VPN servers:

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances = {
      vpn1 = {
        enable = true;
        listenPort = 51821;
        target = "vpn1.example.com:19999";
        key = "key_for_vpn1";
        verbose = "INFO";
      };

      vpn2 = {
        enable = true;
        listenPort = 51822;
        target = "vpn2.example.com:19999";
        key = "key_for_vpn2";
        verbose = "DEBUG";
      };
    };
  };
}
```

### Custom Tuning

```nix
{
  services.wg-obfuscator = {
    enable = true;

    instances.main = {
      enable = true;
      listenPort = 19999;
      target = "127.0.0.1:51820";
      key = "your_secret_key";

      # Advanced tuning
      maxClients = 2048;       # Support more clients
      idleTimeout = 600;       # 10 minutes
      maxDummy = 16;           # More padding for data packets
      verbose = "TRACE";       # Maximum logging

      interface = "0.0.0.0";   # Listen on all interfaces
    };
  };
}
```

## Configuration Options

### Instance Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable` | bool | false | Enable this instance |
| `listenPort` | port | required | Port to listen on |
| `target` | string | required | Target address:port to forward to |
| `key` | string | "" | Obfuscation key (plaintext) |
| `keyFile` | path | null | Path to file containing the key |
| `interface` | string | "0.0.0.0" | Interface to bind to |
| `masking` | enum | "AUTO" | Protocol masking: NONE, AUTO, or STUN |
| `staticBindings` | string | null | Static NAT bindings for two-way mode |
| `fwmark` | int | null | Firewall mark (0-65535) |
| `verbose` | enum | "INFO" | Log level: ERRORS, WARNINGS, INFO, DEBUG, TRACE |
| `maxClients` | int | 1024 | Maximum simultaneous clients |
| `idleTimeout` | int | 300 | Idle timeout in seconds |
| `maxDummy` | int | 4 | Max dummy data bytes (0-1024) |

### Service Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable` | bool | false | Enable wg-obfuscator service |
| `package` | package | pkgs.wg-obfuscator | Package to use |
| `instances` | attrs | {} | Named instances to run |

## Building with Flake

### Local development

```bash
# Build the package
nix build .#wg-obfuscator

# Run without installing
nix run .# -- --help

# Enter dev shell
nix develop

# Format Nix files
nix fmt
```

### Using as an overlay

```nix
{
  nixpkgs.overlays = [
    inputs.wg-obfuscator.overlays.default
  ];

  environment.systemPackages = with pkgs; [
    wg-obfuscator
  ];
}
```

## Firewall Configuration

The NixOS module automatically opens the required UDP ports for all enabled instances. If you need custom firewall rules:

```nix
{
  services.wg-obfuscator.enable = true;

  # Ports are opened automatically, but you can add more rules:
  networking.firewall = {
    allowedUDPPorts = [ ]; # Already handled by module

    # Example: limit connections from specific IPs
    extraCommands = ''
      iptables -A INPUT -p udp --dport 19999 -s 1.2.3.4 -j ACCEPT
      iptables -A INPUT -p udp --dport 19999 -j DROP
    '';
  };
}
```

## Troubleshooting

### Check service status

```bash
systemctl status wg-obfuscator
journalctl -u wg-obfuscator -f
```

### Increase logging

```nix
services.wg-obfuscator.instances.main.verbose = "DEBUG";  # or "TRACE"
```

### Verify obfuscation is working

On client side with DEBUG/TRACE logging, you should see:
- "Obfuscating packet" for outgoing WireGuard packets
- "Deobfuscating packet" for incoming obfuscated packets

### Common issues

**Routing loops**: Make sure to either:
1. Exclude server IP from `AllowedIPs = 0.0.0.0/0` (see README.md)
2. Use `fwmark` on Linux (both WireGuard and obfuscator must match)

**No handshake**:
- Check keys match on both sides
- Verify ports are open in firewall
- Enable DEBUG logging to see packet flow

**MTU issues**:
- Reduce WireGuard MTU to 1420 or lower
- Especially important when using masking

## Security Considerations

1. **Use keyFile for secrets**: Don't put keys in Nix configuration (they end up in world-readable `/nix/store`)
2. **Use secret management**: Consider agenix, sops-nix, or NixOps secrets
3. **Firewall**: The module opens required ports automatically, but consider additional restrictions
4. **Root required**: The service runs as root (needed for fwmark and networking capabilities)

## Migration from Traditional Setup

If you're migrating from a traditional Linux installation:

**Old systemd service:**
```ini
[Service]
ExecStart=wg-obfuscator -c /etc/wg-obfuscator.conf
```

**New NixOS config:**
```nix
services.wg-obfuscator = {
  enable = true;
  instances.main = {
    enable = true;
    # Copy settings from /etc/wg-obfuscator.conf
    listenPort = 13255;
    target = "10.13.1.100:13255";
    keyFile = "/run/secrets/wg-obfuscator-key";
  };
};
```

## Contributing

When modifying the flake:

```bash
# Check flake structure
nix flake check

# Update flake.lock
nix flake update

# Format Nix files
nix fmt
```
