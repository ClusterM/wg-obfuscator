# WireGuard Obfuscator

WireGuard Obfuscator is a tool designed to disguise WireGuard traffic as random data or a different protocol, making it much harder for DPI (Deep Packet Inspection) systems to detect and block. This can be extremely useful if your ISP or government attempts to block or throttle WireGuard traffic.

**Project Goals:**
* **Compact and dependency-free**: The application is designed to be as lightweight as possible, with no external dependencies at all. This makes it suitable even for installation on minimal hardware such as basic home routers.
* **Independent obfuscator**: Instead of maintaining a separate fork of WireGuard, the obfuscator is built to be fully independent of the VPN client/server. This allows seamless integration into an existing network architecture or even running the obfuscator on a separate device if the device running WireGuard is unable to support additional applications.
* **Preserve bandwidth efficiency**: The obfuscator continues to use only UDP and introduces minimal overhead to the size of original packets to ensure maximum throughput.

**What it's NOT:**
* **Not a standalone solution**: You need to run this tool on both ends of the WireGuard connection. You must deploy the obfuscator on both sides to ensure proper obfuscation and deobfuscation of traffic. Therefore, you cannot use it with third-party VPN servers. If you want to bypass your ISP's restrictions or censorship, you need to run your own VPN server (e.g., hosted on a VPS) and connect to it using WireGuard.
* **Not a VPN**: This is not a VPN service or a WireGuard client/server. It only obfuscates WireGuard traffic.

Table of Contents:
- [Feature overview](#feature-overview)
- [Basic concept](#basic-concept)
- [Configuration](#configuration)
  - [Avoiding Routing Loops](#avoiding-routing-loops)
  - [Masking](#masking)
  - [Two-way Mo- [How to download, build and install](#how-to-download-build-and-install)
  - [Linux](#linux)
  - [Windows](#windows)
  - [macOS](#macos)
  - [Android](#android)
  - [Running Docker container on Linux](#running-docker-container-on-linux)
  - [Running Docker container on MikroTik Routers](#running-docker-container-on-mikrotik-routers-routeros-74)rotik-routers-routeros-74)
- [Caveats and recommendations](#caveats-and-recommendations)
- [Download](#download)
- [Credits](#credits)
- [Support the developer and the project](#support-the-developer-and-the-project)


## Feature overview

Originally built as a quick personal solution, this project has grown into a fully-featured tool with the following capabilities:
* **WireGuard-specific design**  
  This obfuscator is purpose-built for the WireGuard protocol: it recognizes WireGuard packet types and actively monitors handshake success to ensure reliable operation.
* **Key-based obfuscation**  
  Obfuscation is performed using a user-specified key. While this arguably makes it more like encryption, note that providing strong cryptographic guarantees is not the goal here - WireGuard itself already handles secure encryption. The key’s purpose is to make your traffic look unrecognizable, not unbreakable.
* **Symmetric operation**  
  The tool automatically detects whether packets are obfuscated and processes them accordingly.
* **Handshake randomization**  
  WireGuard handshake packets are padded with random dummy data, so their obfuscated sizes vary significantly. This makes it difficult for anyone monitoring traffic to spot patterns or reliably fingerprint handshakes. Even data packets can have their size increased by a few random bytes too.
* **Masking**  
  Starting from version 1.4, the project introduces masking support: the ability to disguise traffic as another protocol. This is especially useful when DPI only allows whitelisted protocols. At the moment, the only available option is STUN emulation. Since STUN is commonly used for video calls, it is rarely blocked.
* **Very fast and efficient**  
  The obfuscator is designed to be extremely fast, with minimal CPU and memory overhead. It can handle high traffic loads without noticeable performance degradation.
* **Built-in NAT table**  
  The application features a high-performance, built-in NAT table. This allows hundreds of clients to connect to a single server port while preserving fast, efficient forwarding. Each client’s address and port are mapped to a unique server-side port.
* **Static (manual) bindings / two-way mode**  
  You can manually define static NAT table entries, which enables "two-way" mode - allowing both WireGuard peers to initiate connections toward each other through the obfuscator.
* **Multi-section configuration files**  
  Supports both simple configuration files and command-line arguments for quick one-off runs or advanced automation. You can define multiple obfuscator instances within a single configuration file.
* **Detailed and customizable logging**  
  Verbosity levels range from error-only output to full packet-level traces for advanced troubleshooting and analytics.
* **Cross-platform and lightweight**  
  Available as binaries for Linux, Windows, and Mac, as well as tiny multi-arch Docker images (amd64, arm64, arm/v7, arm/v6, 386, ppc64le, s390x). The images are extremely small and suitable even for embedded routers like MikroTik.
* **Very low dependency footprint**  
  No large external libraries or frameworks are required.
* **Android client**  
  A very simple Android port of the obfuscator is available: https://github.com/ClusterM/wg-obfuscator-android/ - it allows you to obfuscate WireGuard traffic on Android devices, including phones, tablets, and Android TVs.


## Basic Concept

```
┌────────────────┐ ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
| WireGuard peer | | WireGuard peer | | WireGuard peer | | WireGuard peer |
└───────▲────────┘ └───────▲────────┘ └────────▲───────┘ └───────▲────────┘
        |                  |                   └──────┐    ┌─────┘
┌───────▼────────┐ ┌───────▼────────┐           ┌─────▼────▼─────┐
|   Obfuscator   | |   Obfuscator   |           |   Obfuscator   |
└───────▲────────┘ └───────▲────────┘           └────────▲───────┘
        |                  |                             |
┌───────▼──────────────────▼─────────────────────────────▼────────────────┐
|       |                  |     Internet                |                |
└───────▲──────────────────▲─────────────────────────────▲────────────────┘
        |                  |                             |
        |          ┌───────▼────────┐                    |
        └─────────>|   Obfuscator   |<───────────────────┘
                   └───────▲────────┘
                           |
               ┌───────────▼────────────┐
               | WireGuard  server peer |
               └────────────────────────┘
```

In most cases, the obfuscator is used in a scenario where there is a clear separation between a server (with a static or public IP address) and clients (which may be behind NAT). We’ll focus on this setup here.
If **both** ends have public IPs and can initiate connections to each other, refer to the ["Two-way mode"](#two-way-mode) section below.

Usually, the obfuscator is installed on the same device as your WireGuard client or server. In this setup, you configure WireGuard to connect to the obfuscator’s address and port (typically `127.0.0.1` and a custom port), while the *real* remote address and port are specified in the obfuscator’s configuration.

For example, a standard WireGuard client configuration:

```
[Peer]
Endpoint = example.com:19999
```

would become:

```
[Peer]
Endpoint = 127.0.0.1:3333
```

And the obfuscator would be launched/configured like this:

```
source-lport = 3333
target = example.com:19999
```

On the **server** side, the WireGuard config:

```
[Interface]
ListenPort = 19999
```

would be changed to:

```
[Interface]
ListenPort = 5555
```

With the obfuscator running with this config:

```
source-lport = 19999
target = 127.0.0.1:5555
```

The application maintains its own internal address mapping table, so a single server-side obfuscator can handle connections from multiple clients - each with their own obfuscator instance - using just one server port. Likewise, on the client side, a single obfuscator can support connections to multiple peers (though this is rarely needed in typical use).

The obfuscator automatically determines the direction (obfuscation or deobfuscation) for each packet, so the configuration files on both the client and server sides look nearly identical. The only thing that matters is that both sides use the same key.

**The key** is simply a plain text string. Cryptographic strength is not required here: feel free to use any common word or phrase (longer is better, but even four or five characters is usually enough in practice). The main thing is that your key is not the same as everyone else’s!


## Configuration

### Command line parameters and configuration file
The obfuscator can be run with a command line configuration or using a configuration file. Available command line arguments are:
* `-?` or `--help`  
  Show a help message describing all command-line arguments and exit.
* `-V` or `--version`  
  Print version information and exit.
* `-c <filename>` or `--config=<filename>`  
  Path to the configuration file, can be used instead of the rest arguments.
* `-i <interface>` or `--source-if=<interface>`  
  Source interface to listen on. Optional, default is `0.0.0.0`, e.g. all interfaces. Can be used to listen only on a specific interface.
* `-p <port>` or `--source-lport=<port>`  
  Source port to listen. The source client should connect to this port. Required.
* `-t <address:port>` or `--target=<address:port>`  
  Target address and port in `address:port` format. All obfuscated/deobfuscated data will be forwarded to this address. Required.
* `-k <key>` or `--key=<key>`  
  Obfuscation key. Just a string. The longer, the better. Required, must be 1-255 characters long.
* `-a <type>` or `--masking=<type>`  
  Protocol masking type to disguise traffic. Optional, default is `AUTO`. Supported values: `STUN`, `AUTO`, `NONE`. See ["Masking"](#masking) for details.
* `-b <bindings>` or `--static-bindings=<bindings>`  
  Comma-separated static bindings for two-way mode, in the format `<client_ip>:<client_port>:<forward_port>`. See ["Two-way mode"](#two-way-mode) for details.
* `-f <mark>` or `--fwmark=<mark>`  
  Firewall mark to set on all packets. Can be used to prevent routing loops. Optional, default is `0`, (i.e. disabled). Can be `0`-`65535` or `0x0000`-`0xFFFF`.
* `-v <level>` or `--verbose=<level>`  
  Verbosity level. Optional, default is `INFO`. Accepted values are:  
    `ERRORS` (critical errors only)  
    `WARNINGS` (important messages)  
    `INFO` (informational messages: status messages, connection established, etc.)  
    `DEBUG` (detailed debug messages)  
    `TRACE` (very detailed debug messages, including packet dumps)  

Additional arguments for advanced users:
* `-m <max_clients>` or `--max-clients=<max_clients>`  
  Maximum number of clients. This is the maximum number of clients that can be connected to the obfuscator at the same time. If the limit is reached, new clients will be rejected. Optional, default is `1024`.
* `-l <timeout>` or `--idle-timeout=<timeout>`  
  Maximum idle timeout in seconds. This is the maximum time in seconds that a client can be idle before it is disconnected. If the client does not send any packets for this time, it will be disconnected. Optional, default is `300` seconds (5 minutes).
* `-d <length>` or `--max-dummy=<length>`  
  Maximum dummy length for data packets. This is the maximum length of dummy data in bytes that can be added to data packets. Used to obfuscate traffic and make it harder to detect. The value must be between `0` and `1024`. If set to `0`, no dummy data will be added. Default is `4`. Note: total packet size with dummy bytes will be limited to 1024 bytes.

You can use the `--config` argument to specify a configuration file, which allows you to set all these parameters in the `key=value` format. For example:
```
# Instance name
[main]

source-lport = 13255
target = 10.13.1.100:13255
key = love
static-bindings = 1.2.3.4:12883:6670, 5.6.7.8:12083:6679
verbose = 2

# You can specify multiple instances
[second_server]
source-if = 0.0.0.0
source-lport = 13255
target = 10.13.1.100:13255
key = hate
verbose = 4
```

As you can see, the configuration file allows you to define settings for multiple obfuscator instances. This makes it easy to run several instances of the obfuscator with different settings, all from a single configuration file.

Be sure to check the [Caveats and Recommendations](#caveats-and-recommendations) section below for important notes on configuration and usage.

### Avoiding Routing Loops

When using WireGuard, especially in combination with tools like WireGuard Obfuscator, it's important to ensure that traffic to the VPN server itself is **not accidentally routed through the VPN tunnel**. Otherwise, you may encounter a routing loop or complete loss of connection right after the handshake.

#### Why and When This Happens

WireGuard routes packets based on the `AllowedIPs` list. Normally, it automatically excludes the server’s IP address (as specified in the `Endpoint` field) to avoid routing handshake and keepalive packets through the tunnel itself.

However, when you use WireGuard Obfuscator running locally (e.g., on `127.0.0.1`), **WireGuard only sees the obfuscator's local address**, not the actual public IP of the VPN server. It has no awareness of the real server endpoint, which is hidden inside the obfuscator's config.

This becomes a problem especially when the peer is configured with:

```
AllowedIPs = 0.0.0.0/0
```

i.e., when all traffic is routed through the VPN tunnel. In this case, if the real server IP is not explicitly excluded, **WireGuard Obfuscator may try to send its own traffic to the VPN server through the tunnel**, leading to a routing loop or connection loss.

#### Universal Solution: Manually Exclude the Server IP from `0.0.0.0/0`

WireGuard does not support negation syntax (e.g., `!203.0.113.45`). To avoid routing traffic to the server through the tunnel, you can **manually split `0.0.0.0/0` into a set of smaller CIDR blocks that exclude the server's IP**.

For example, if your server’s public IP is `203.0.113.45`, then instead of:

```ini
AllowedIPs = 0.0.0.0/0
```

You would use:

```ini
AllowedIPs = 0.0.0.0/1, 128.0.0.0/2, 224.0.0.0/3, 208.0.0.0/4, 192.0.0.0/5,
204.0.0.0/6, 200.0.0.0/7, 202.0.0.0/8, 203.128.0.0/9, 203.64.0.0/10,
203.32.0.0/11, 203.16.0.0/12, 203.8.0.0/13, 203.4.0.0/14, 203.2.0.0/15,
203.1.0.0/16, 203.0.128.0/17, 203.0.0.0/18, 203.0.64.0/19, 203.0.96.0/20,
203.0.120.0/21, 203.0.116.0/22, 203.0.114.0/23, 203.0.112.0/24,
203.0.113.128/25, 203.0.113.64/26, 203.0.113.0/27, 203.0.113.48/28,
203.0.113.32/29, 203.0.113.40/30, 203.0.113.46/31, 203.0.113.44/32
```

This long list routes all traffic through the tunnel **except** for the server's IP (`203.0.113.45`), which stays outside the tunnel and avoids the loop.

You can use the following script to calculate the subnet list automatically:
[https://colab.research.google.com/drive/1spIsqkB4YOsctmZV83aG1HKISFFxxMCZ](https://colab.research.google.com/drive/1spIsqkB4YOsctmZV83aG1HKISFFxxMCZ)

#### Linux-Specific Solution: Using `fwmark`

On Linux, there's a cleaner approach (since version 1.4): use the `FwMark` option in the WireGuard config. This is useful **only when `AllowedIPs = 0.0.0.0/0`**, as it allows the system to distinguish between traffic going through the tunnel and traffic required to establish or maintain the tunnel (e.g., handshake packets).

Example WireGuard config:

```ini
[Interface]
FwMark = 0xdead
```

Then, in **WireGuard Obfuscator**, specify the same mark:

* In the config file:

  ```
  fwmark = 0xdead
  ```
* Or via command-line:

  ```
  --fwmark 0xdead
  ```

> **Note:** Using `fwmark` requires root privileges. Make sure to run WireGuard Obfuscator as root when using this option.

### Masking
As of version 1.4, masking support is available - the ability to disguise traffic as another protocol. This is especially useful when DPI only allows whitelisted protocols. You can set masking mode using the `masking` option in the config file or the `--masking` parameter on the command line.

At the moment, the only available option is STUN emulation. Since STUN is commonly used for video calls, it is rarely blocked. So, currently supported values are:
* `NONE` 
  No masking at all. The obfuscator will not mask outgoing traffic and will not recognize or process any incoming masked traffic.
* `AUTO`  
  The obfuscator will not mask outgoing traffic by default. However, if the first packet from the client (on the 'source-lport' side) is masked, the server will autodetect the masking type and switch to it, allowing the client to choose the masking mode independently.
* `STUN`  
  Forces the use of the STUN protocol for outgoing traffic and only accepts incoming traffic that is STUN-masked.

### Two-Way Mode
(for advanced users)

In some setups, both WireGuard peers have public IP addresses and can each initiate connections. In this scenario, you need both ends to accept and send connections through the obfuscator. This is where **two-way mode** comes in.

#### What Are Static Bindings?

A **static binding** tells the obfuscator, right from startup, which peer IPs and ports should be mapped to which local ports. This allows the obfuscator to know exactly how to route packets from the server to the correct local WireGuard instance - **even if that peer hasn’t sent any packets yet.**
Without static bindings, the obfuscator only learns how to forward packets after seeing traffic from a client.

#### Example: Two-way WireGuard with Obfuscation

Suppose you have two peers:

* **Peer A**: Public IP `1.2.3.4`, runs WireGuard locally on port `1111`
* **Peer B**: Public IP `5.6.7.8`, runs WireGuard locally on port `6666`

We can set up the obfuscator on both peers:
* **Peer A**: Obfuscator listens for local WireGuard traffic on port `2222` and listens for incoming obfuscated handshakes from **Peer A** on port `3333`.
* **Peer B**: Obfuscator listens for incoming obfuscated handshakes from **Peer B** on port `4444` and listens for local WireGuard traffic on port `5555`.

**Peer A WireGuard config** (`1.2.3.4`):

```
[Interface]
PrivateKey = <A's private key>
ListenPort = 1111

[Peer]
PublicKey = <B's public key>
Endpoint = 127.0.0.1:2222
```

**Peer A Obfuscator config** (`1.2.3.4`):

```
source-lport = 2222
target = 5.6.7.8:4444
static-bindings = 127.0.0.1:1111:3333
key = your_secret_key
```

**Peer B Obfuscator config** (`5.6.7.8`):

```
source-lport = 4444
target = 127.0.0.1:6666
static-bindings = 1.2.3.4:3333:5555
key = your_secret_key
```

**Peer B WireGuard config** (`5.6.7.8`):

```
[Interface]
PrivateKey = <B's private key>
ListenPort = 6666

[Peer]
PublicKey = <A's public key>
Endpoint = 127.0.0.1:5555
```

In this example the line:

```
static-bindings = 1.2.3.4:1111:3333
```

Visually, it looks like this:
```
   ┌───────────────────────────┐             ┌───────────────────────────┐
   │      Peer A (1.2.3.4)     │             │      Peer B (5.6.7.8)     │
   │  ┌─────────────────┐      |             │  ┌─────────────────┐      |
   │  │ WireGuard       │      |             │  │ WireGuard       │      |
   │  │ ListenPort=1111 |      |             │  │ ListenPort=6666 |      |
   │  └─────▲───────────┘      |             │  └─────▲───────────┘      |
   │        │                  │             │        │                  │
   │  ┌─────▼───────────────┐  │             │  ┌─────▼───────────────┐  │
   │  │ source-lport=2222   |  │             │  │ local port=5555     │  │
   │  │                     |  │             │  │                     │  │
   │  │ Obfuscator          |  │             │  │ Obfuscator          |  │
   │  │ static-bind         |  │             │  │ static-bind         |  │
   │  │ 127.0.0.1:1111:3333 |  │             │  │ 1.2.3.4:3333:5555   |  │
   │  │                     |  │             │  │                     │  │
   │  │ local port=3333     │  |             │  │ source-lport=4444   |  │
   │  └─────▼───────────────┘  │             │  └─────▼───────────────┘  │
   │        │                  │             │        │                  │
   └────────┼──────────────────┘             └────────┼──────────────────┘
            │                                         │
            │         UDP/obfuscated traffic          │
            │<--------------------------------------->│
```

When **Peer A** initiates a handshake with **Peer B**:

1. WireGuard on **Peer A** sends a non-obfuscated handshake UDP packet from port `1111` to the local obfuscator on port `2222`.
2. The obfuscator on **Peer A** obfuscates the packet using the key and sends it to **Peer B**’s obfuscator on port `4444`, using `3333` as the source port.  
   > Without static bindings, the obfuscator dynamically selects the source port and creates a mapping in its NAT table.  
   > With the static binding `127.0.0.1:1111:3333`, it knows to always use port `3333` as the source port for packets from `127.0.0.1:1111`.
3. **Peer B**’s obfuscator receives the packet, deobfuscates it, and forwards it to **Peer B**’s local WireGuard instance on port `6666`, using `5555` as the source port.  
   > Without static bindings, the obfuscator dynamically selects the source port and creates a mapping.  
   > With the static binding `1.2.3.4:3333:5555`, it knows to use port `5555` as the source port for packets from `1.2.3.4:3333`.
4. **Peer B**’s WireGuard processes the handshake and sends a response from port `6666` to the obfuscator on port `5555`.
5. **Peer B**’s obfuscator obfuscates the response and sends it back to **Peer A**’s obfuscator on port `3333`, using `4444` as the source port.  
   > With static bindings, the necessary mappings already exist.
6. **Peer A**’s obfuscator deobfuscates the response and forwards it from port `2222` to **Peer A**’s WireGuard on port `1111`.
   > With static bindings, the necessary mappings already exist.
7. **Peer A**’s WireGuard processes the response, completing the handshake.

When **Peer B** initiates a handshake with **Peer A**, the process is the same but in reverse:

1. WireGuard on **Peer B** sends a non-obfuscated handshake UDP packet from port `6666` to the local obfuscator on port `5555`.
2. The obfuscator on **Peer B** obfuscates the packet and sends it to **Peer A**’s obfuscator on port `3333`, using `4444` as the source port.  
   > Without static bindings, reverse connections would not work because the obfuscator would not know how to forward packets.  
   > With the static binding `1.2.3.4:3333:5555`, the mapping already exists, so it knows to forward packets to `1.2.3.4:3333`.
3. **Peer A**’s obfuscator receives the packet, deobfuscates it, and forwards it to **Peer A**’s WireGuard on port `1111`, using `2222` as the source port.  
   > Without static bindings, reverse connections would not work because the obfuscator would not know how to forward packets.  
   > With the static binding `127.0.0.1:1111:3333`, the mapping already exists, so it knows to forward packets to `127.0.0.1:1111`.
4. **Peer A**’s WireGuard processes the handshake and sends a response from port `1111` to the obfuscator on port `2222`.
5. **Peer A**’s obfuscator obfuscates the response and sends it to **Peer B**’s obfuscator on port `4444`, using `3333` as the source port.
6. **Peer B**’s obfuscator deobfuscates the packet and forwards it to **Peer B**’s WireGuard on port `6666`, using `5555` as the source port.
7. **Peer B**’s WireGuard processes the response, completing the handshake.

#### Summary

With static bindings, each obfuscator knows in advance how to forward packets between the server and local WireGuard, regardless of which peer initiates the connection. This enables fully bidirectional, peer-to-peer WireGuard tunnels - *even if both sides can initiate connections at any time.*


## How to download, build and install
See [Download](#download) section below for download links.

### Linux
On Linux, the obfuscator can be installed as a systemd service for automatic startup and management.

To build and install on Linux from the source code, simply run:
```sh
make
sudo make install
```

This will install the obfuscator as a systemd service.  
You can start it with:
```sh
sudo systemctl start wg-obfuscator
```

The configuration file is located at:  
`/etc/wg-obfuscator.conf`

#### Third-party packages

- **`ALT Linux`** *apt-rpm package* in [**Sisyphus**](https://packages.altlinux.org/en/sisyphus/srpms/wg-obfuscator)

### Windows
You can download ready-to-run binaries with all required DLL libraries.

If you want to build this tool for Windows from the source code, you need [MSYS2](https://www.msys2.org/) and the following packages:
* `base-devel`
* `gcc`
* `git`

Install the required packages, then run:
```sh
make
```
> **Note:** On Windows, the obfuscator is only available as a command-line application. You need to run it from### Android
A very simple Android port of the obfuscator is available: https://github.com/ClusterM/wg-obfuscator-android/ - it allows you to obfuscate WireGuard traffic on Android devices, including phones, tablets, and Android TVs.

### Running Docker Container on Linuxe just type:
```sh
make
```
> **Note:** On macOS, the obfuscator is only available as a command-line application. You need to run it from the terminal and manage startup yourself.

### Android
A very simple Android port of the obfuscator is available: https://github.com/ClusterM/wg-obfuscator-android/ - it allows you to obfuscate WireGuard traffic on Android devices, including phones, tablets, and Android TVs.

### Running Docker Container on Linux

WireGuard Obfuscator is available as a multi-architecture Docker image:
**[clustermeerkat/wg-obfuscator on Docker Hub](https://hub.docker.com/r/clustermeerkat/wg-obfuscator)**

**Supported tags:**

* **`latest`** - always points to the most recent stable release.
* **`nightly`** - built automatically from the current main branch; may be unstable. Use only for testing new features.
* **Version tags** (e.g. `1.0`, `1.1`) - for specific releases.

**Architectures available:**

* `linux/amd64`
* `linux/arm64`
* `linux/arm/v7`
* `linux/arm/v6`
* `linux/arm/v5`
* `linux/386`
* `linux/ppc64le`
* `linux/s390x`

#### Example: docker-compose.yml

> **Note:**
> Make sure to match the exposed port (`13255` in the example below) with the `source-lport` value in your configuration file.

```yaml
version: '3.8'

services:
  wg-obfuscator:
    image: clustermeerkat/wg-obfuscator:latest
    volumes:
      - ./.wg-obfuscator.conf:/etc/wg-obfuscator/wg-obfuscator.conf
    ports:
      - "13255:13255/udp"
    container_name: wg-obfuscator-container
    restart: unless-stopped
```

* **`image`** can be changed to use a specific tag (e.g., `clustermeerkat/wg-obfuscator:1.1`).
* Place your config as `.wg-obfuscator.conf` in the same directory as `docker-compose.yml`, or adjust the volume path.
* **Port mapping** (`13255:13255/udp`) must correspond to your obfuscator’s listen port.

#### Running manually

You can also run the container directly:

```sh
docker run -d \
  --name wg-obfuscator \
  -v $PWD/.wg-obfuscator.conf:/etc/wg-obfuscator/wg-obfuscator.conf \
  -p 13255:13255/udp \
  clustermeerkat/wg-obfuscator:latest
```

### Running Docker Container on MikroTik Routers (RouterOS 7.4+)

WireGuard Obfuscator can run as a container on MikroTik devices with **RouterOS 7.4+** (ARM64/x86\_64). Update to the latest RouterOS version to ensure you have all the latest container features and fixes.

#### Quick Setup

##### 1. Install the `container` package

* Download the latest **Extra Packages** for your RouterOS version and platform from [mikrotik.com/download](https://mikrotik.com/download)
* Extract and upload `container-*.npk` to the router
* Reboot the router

##### 2. Enable container device mode (**only required once!**)

```shell
/system/device-mode/update container=yes
```

* Confirm when prompted:
  – For most models, press the physical reset button
  – On x86, do a full power-off (cold reboot)

##### 3. Configure container registry (one time)

```shell
/container/config/set registry-url=https://registry-1.docker.io tmpdir=temp
```

##### 4. Enable container logging (one time)

First of all, you need to enable logging for the container subsystem. This is required to see container logs in the RouterOS log using the `/log` command.
```shell
/system logging add topics=container
```

##### 5. Create a veth interface for container networking

```shell
/interface/veth/add name=veth-wg-ob address=172.17.13.2/24 gateway=172.17.13.1
```
> **Note:** in this example, we use IP address `172.17.13.2` for the container and `172.17.13.1` for the host.
> You can choose any other IP addresses as long as they are in the same subnet.

##### 6. Add IP address for the veth interface

```shell
/ip address add address=172.17.13.1/24 interface=veth-wg-ob
```
> **Note:** This IP address is used by the host to communicate with the container. It must match the gateway set in the veth interface.

##### 7. Forward UDP ports to the container

In case if your obfuscator is configured to accept incoming connections from a server outside of the NAT, you need to forward the UDP port to the container. 
For client side obfuscator without two-way mode (static bindings), you can skip this step.

```shell
/ip firewall nat add chain=dstnat action=dst-nat protocol=udp dst-port=13255 to-addresses=172.17.13.2 to-ports=13255
```
> **Note:** This IP address is used by the container, it must match the `address` in the veth interface.

> **Note:** Port number `13255` is just an example. It must port you are using for incoming connections in your obfuscator configuration file (i.e. `source-lport` or port from static bindings).

##### 8. Create and mount a config directory

```shell
/container/mounts/add dst=/etc/wg-obfuscator name=wg-obfuscator-config src=/wg-obfuscator
```

##### 9. Add the container

Add the container using the `container/add` command. This will download the latest image from Docker Hub and create a container with the specified settings.
```shell
/container/add \
  interface=veth-wg-ob \
  logging=yes \
  mounts=wg-obfuscator-config \
  name=wg-obfuscator \
  root-dir=wg-obfuscator-data \
  start-on-boot=yes \
  remote-image=clustermeerkat/wg-obfuscator:latest
```

##### 10. Check the logs

```shell
/log print where topics~"container"
```
You should see `import successful` message.

##### 11. Start the container

After the container is added, you can start it using the following command:
```shell
/container/start [/container/find where name="wg-obfuscator"]
```

##### 12. Check the logs

```shell
/log print where topics~"container"
```
You should see logs indicating the container has started successfully.

##### 13. Edit your config file

* After the **first launch**, a default example config file will appear at `/wg-obfuscator/wg-obfuscator.conf` on your router.
* **Edit this file** to match your actual WireGuard and obfuscator settings.
  You can use WinBox, WebFig, or the `/file edit` command in the MikroTik terminal.

##### 14. Restart the container

```shell
/container/stop [/container/find where name="wg-obfuscator"]
/container/start [/container/find where name="wg-obfuscator"]
```

##### 15. Check the logs

```shell
/log print where topics~"container"
```
You should see logs indicating the container has started successfully and is ready to process WireGuard traffic.

**Notes:**

* `container` package and device-mode are only needed once per router.
* No external disk is required; image is small and uses internal storage.
* See [MikroTik Containers Docs](https://help.mikrotik.com/docs/display/ROS/Containers) for advanced usage.
* Don't forget to change WireGuard's `Endpoint` to point to the obfuscator's IP and port.
* Don't forget about the [Caveats and Recommendations](#caveats-and-recommendations) section below, especially regarding endpoint exclusion and routing loops.


## Caveats and Recommendations

* **Endpoint Exclusion and Routing Loops:**  
  See the ["Avoiding Routing Loops"](#avoiding-routing-loops) section above for details on how to prevent routing loops. It's important to ensure that traffic to the VPN server itself is not routed through the VPN tunnel.
* **PersistentKeepalive:**  
  To maintain a stable connection - especially when clients are behind NAT or firewalls - it is recommended to use WireGuard’s `PersistentKeepalive` option. A value of `25` seconds is usually sufficient.
* **Initial Handshake Requirement:**  
  After starting the obfuscator, no traffic will flow between WireGuard peers until a successful handshake has been established.
  If you restart the obfuscator *without* restarting WireGuard itself, it may take some time for the peers to re-establish the handshake and resume traffic. You can speed this up by briefly toggling the WireGuard interface.
* **MTU Settings:**  
  If you experience issues with packet loss (you can see `recv` or `recvfrom` errors in DEBUG level logs), ensure that your WireGuard configuration has appropriate MTU settings. Especially when using masking (it adds extra bytes to each packet), you may need to reduce the MTU. A common setting is `MTU = 1420`, but you may need to reduce it based on your network conditions.
* **IPv6 Support:**  
  The obfuscator does not currently support IPv6. It only works with IPv4 addresses and ports.
* **Check debug logs:**  
  If you encounter issues, run the obfuscator with `--verbose=DEBUG` (DEBUG level) to see detailed logs. This can help diagnose many common problems.


## Download
* You can always find the latest release (source code, Docker images and ready-to-use binaries for Linux, Windows, and macOS) at:  
https://github.com/ClusterM/wg-obfuscator/releases
* Also, you can download automatic CI builds at:  
  https://clusterm.github.io/wg-obfuscator/  
  Download it only if you want to test new features or bug fixes that are not yet released. Can be buggy or unstable, use at your own risk!
* Android port:  
  https://github.com/ClusterM/wg-obfuscator-android


## Credits
* Me: [Cluster](https://github.com/ClusterM), email: cluster@cluster.wtf
* [WireGuard](https://www.wireguard.com/) - the VPN protocol this tool is designed to obfuscate.
* [uthash](https://troydhanson.github.io/uthash/) - a great C library for hash tables, used for the NAT table.


## Support the Developer and the Project

* [GitHub Sponsors](https://github.com/sponsors/ClusterM)
* [Buy Me A Coffee](https://www.buymeacoffee.com/cluster)
* [Sber](https://messenger.online.sberbank.ru/sl/Lnb2OLE4JsyiEhQgC)
* [Donation Alerts](https://www.donationalerts.com/r/clustermeerkat)
* [Boosty](https://boosty.to/cluster)
