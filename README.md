# WireGuard Obfuscator

WireGuard Obfuscator is a tool designed to make WireGuard traffic look like random data, making it significantly harder to detect by DPI (Deep Packet Inspection) systems. This can be extremely useful if your ISP or government tries to block or throttle WireGuard traffic.

What started as a quick-and-dirty solution just for personal use has grown into a fully-featured project with the following capabilities:

* **Key-Based Obfuscation**  
  Obfuscation is performed using a user-specified key. While this arguably makes it more like encryption, keep in mind that strong cryptography is not the goal here—WireGuard itself already handles secure encryption. The key's purpose is to make your traffic look unrecognizable, not unbreakable.
* **Symmetric operation**  
  You can use the obfuscator on both ends of a WireGuard tunnel, or just one—it will figure out automatically whether packets are obfuscated or not, and will always do the right thing.
* **Packet Salting**  
  Each packet gets a random salt, ensuring that even identical packets always look different after obfuscation. This further frustrates signature-based DPI systems.
* **Handshake Randomization**  
  WireGuard handshake packets are padded with random dummy data, so their obfuscated sizes vary widely. This makes it difficult for anyone monitoring traffic to spot patterns or reliably fingerprint handshakes. Even data packets can have their size increased by a few random bytes.
* **Built-In NAT Table**  
  The application features a high-performance, built-in NAT table. This allows hundreds of clients to connect to a single server port while preserving fast, efficient forwarding. Each client’s address and port are mapped to a unique server-side port.
* **Static (Manual) Bindings / Two-Way Mode**  
  You can manually define static NAT table entries, which enables "two-way" mode—allowing both WireGuard peers to initiate connections toward each other through the obfuscator.
* **Multi-Section Configuration Files**  
  Supports both simple configuration files and command-line arguments for quick one-off runs or advanced automation. You can define multiple obfuscator instances within a single configuration file.
* **Detailed and customizable logging**  
  Verbosity levels range from errors-only to full packet-level traces for advanced troubleshooting and analytics.
* **Cross-Platform and Lightweight**  
  Available as binaries for Linux, Windows, and Mac, as well as tiny multi-arch Docker images (amd64, arm64, arm/v7, arm/v6, 386, ppc64le, s390x). The images are extremely small and suitable even for embedded routers like MikroTik.
* **Cross-compile ready**  
  Easily portable and compilable on Linux, macOS, and Windows (MSYS2/MinGW, with automatic fallback to poll()).
* **Very low dependency footprint**  
  No huge libraries or frameworks.
* **Android Client Coming Soon?**  
  A native Android version of the obfuscator is planned, allowing you to obfuscate WireGuard traffic directly on Android devices (including phones, tablets, or Android TVs). This will make it possible to use the obfuscator together with mobile WireGuard clients or WireGuard running on smart TVs.


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
If **both** ends have public IPs and can initiate connections to each other, see the section on ["Two-way mode"](##two-way-mode) below.

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

The application maintains its own internal address mapping table, so a single server-side obfuscator can handle connections from multiple clients—each with their own obfuscator instance—using just one server port. Likewise, on the client side, a single obfuscator can support connections to multiple peers (though this is rarely needed in typical use).

The obfuscator automatically determines the direction (obfuscation or deobfuscation) for each packet, so the configuration files on both the client and server sides look nearly identical. The only thing that matters is that both sides use the same key.

**The key** is simply a text string. Cryptographic strength is not required here—feel free to use any common word or phrase (longer is better, but even four or five characters is usually enough in practice). The main thing is that your key is not the same as everyone else’s!


## Configuration

You can pass parameters to the obfuscator using a configuration file or command line arguments. Available parameters are:
* `source-if`  
  Source interface to listen on. Optional, default is `0.0.0.0`, e.g. all interfaces. Can be used to listen only on a specific interface.
* `source-lport`  
  Source port to listen. Source client should connect to this port. Required.
* `target`  
  Target address and port in `address:port` format. Obfuscated/deobfuscated data will be forwarded to this address. Required.
* `key`  
  Obfuscation key. Just string. Longer - better. Required, must be 1-255 characters long.
* `static-bindings`  
  Comma-separated static bindings for two-way mode as <client_ip>:<client_port>:<forward_port>
  (see ["Two-way mode"](##two-way-mode))
* `verbose`  
 Verbosity level, 0-4. Optional, default is 2.  
 0 - ERRORS (critical errors only)  
 1 - WARNINGS (important messages: startup and shutdown messages)  
 2 - INFO (informational messages: status messages, connection established, etc.)  
 3 - DEBUG (detailed debug messages)  
 4 - TRACE (very detailed debug messages, including packet dumps)  

You can use configuration file with those parameters in `key=value` format. For example:
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

As you can see, the configuration file allows you to define settings for multiple obfuscator instances. This makes it easy to run several copies of the obfuscator with different settings, all from a single configuration file.

You can pass the configuration file to the obfuscator using `--config` argument. For example:
```bash
wg-obfuscator --config /etc/wg-obfuscator.conf
```

You can also pass parameters using command line arguments. For example:
```bash
wg-obfuscator --source-lport 13255 --target 10.13.1.100:13255 --key test
```
Type `wg-obfuscator.exe --help` for more information.


### Two-Way Mode
(for advanced users)

In some setups, both WireGuard peers have public IP addresses and can each initiate connections. In this scenario, you need both ends to accept and send connections through the obfuscator. This is where **two-way mode** comes in.

#### What Are Static Bindings?

A **static binding** tells the obfuscator, right from startup, which peer IPs and ports should be mapped to which local ports. This allows the obfuscator to know exactly how to route packets from the server to the correct local WireGuard instance—**even if that peer hasn’t sent any packets yet.**
Without static bindings, the obfuscator only learns how to forward packets after seeing traffic from a client.

#### Example: Two-way WireGuard with Obfuscation

Suppose you have two peers:

* **Peer A**: Public IP `1.2.3.4`, runs WireGuard locally on port `5555`, the obfuscator listens on port `15555` externally and on port `7777` internally
* **Peer B**: Public IP `5.6.7.8`, runs WireGuard locally on port `6666`, the obfuscator listens on port `16666` externally and on port `8888` internally

**Peer A WireGuard config** (`1.2.3.4`):

```
[Interface]
PrivateKey = <A's private key>
ListenPort = 5555

[Peer]
PublicKey = <B's public key>
Endpoint = 127.0.0.1:15555
```

**Peer A Obfuscator config** (`1.2.3.4`):

```
source-lport = 15555
target = 5.6.7.8:16666
static-bindings = 127.0.0.1:5555:7777
key = your_secret_key
```

**Peer B WireGuard config** (`5.6.7.8`):

```
[Interface]
PrivateKey = <B's private key>
ListenPort = 6666

[Peer]
PublicKey = <A's public key>
Endpoint = 127.0.0.1:8888
```

**Peer B Obfuscator config** (`5.6.7.8`):

```
source-lport = 16666
target = 127.0.0.1:6666
static-bindings = 1.2.3.4:7777:8888
key = your_secret_key
```

In this example the line:

```
static-bindings = 1.2.3.4:7777:8888
```

means:

* **If the obfuscator receives a UDP packet from `1.2.3.4`, where the sender’s *source port* is `7777`,**
* it will associate that remote peer with a local UDP socket that uses port `8888` as its *source port* when sending packets out.

So when the obfuscator sends packets to the peer's `target`, it does so from port `8888`.
This allows the remote obfuscator to recognize which static mapping (and which local WireGuard instance or socket) should handle returning packets, based on the source port used by the sender.

> **Note:**
> In UDP, the source port matters for this association. The packet will always be sent *to* the port specified in `target`, but the source port (the third value in the static binding) tells the obfuscator which local port to use for sending.

```
   ┌───────────────────────────┐         ┌───────────────────────────┐
   │        Peer A (1.2.3.4)   │         │        Peer B (5.6.7.8)   │
   │   ┌─────────────┐         │         │   ┌─────────────┐         │
   │   │ WireGuard   │         │         │   │ WireGuard   │         │
   │   │  (5555)     │         │         │   │  (6666)     │         │
   │   └─────▲───────┘         │         │   └─────▲───────┘         │
   │         │                 │         │         │                 │
   │   ┌─────▼───────┐         │         │   ┌─────▼───────┐         │
   │   │ Obfuscator  │         │         │   │ Obfuscator  │         │
   │   │             │         │         │   │             │         │
   │   │             │         │         │   │             │         │
   │   │ source-lport│         │         │   │ source-lport│         │
   │   │   15555     │         │         │   │   16666     │         │
   │   │             │         │         │   │             │         │
   │   │ static-bind.│         │         │   │ static-bind.│         │
   │   │ 127.0.0.1:5555:7777   │         │   │ 1.2.3.4:7777:8888     │
   │   └─────▼───────┘         │         │   └─────▼───────┘         │
   │         │                 │         │         │                 │
   └─────────┼─────────────────┘         └─────────┼─────────────────┘
             │                                         │
             │  UDP/obfuscated traffic                 │
             │                                         │
        (source:7777, dest:16666)      (source:8888, dest:15555)
             │<--------------------------------------->│
```

**Legend:**

* When Peer A’s obfuscator sends a packet to Peer B’s obfuscator:  
  * The packet is sent *from* port `7777` (as set in static-bindings on A) *to* port `16666` on Peer B.
* When Peer B’s obfuscator replies:  
  * The packet is sent *from* port `8888` (as set in static-bindings on B) *to* port `15555` on Peer A.
* The static binding line:  
  * `1.2.3.4:7777:8888` (on B) means: "packets *from* 1.2.3.4, *source port* 7777 → will be mapped to local UDP socket using *source port* 8888 for outbound packets".

**Step-by-step: How a Packet Travels in Two-way Mode**

**1. Peer A sends a packet:**

* WireGuard on Peer A creates a packet.
* The packet goes to A’s local obfuscator (`127.0.0.1:15555`).
* The obfuscator sends the packet to Peer B’s obfuscator (`5.6.7.8:16666`), using `7777` as the source port.
* On the network, the packet is:
  `source: 1.2.3.4:7777 → destination: 5.6.7.8:16666`

**2. Peer B receives the packet:**

* Peer B’s obfuscator sees a packet coming from `1.2.3.4:7777`.
* It checks its static bindings and finds `1.2.3.4:7777:8888`, so it knows to associate this traffic with its local UDP socket bound to port `8888`.
* The obfuscator passes the packet to local WireGuard on port `6666`.

**3. Peer B responds:**

* WireGuard on B sends a response to A, which is routed to B’s obfuscator (`127.0.0.1:8888`).
* The obfuscator sends the packet to Peer A’s obfuscator (`1.2.3.4:15555`), using `8888` as the source port.
* On the network:
  `source: 5.6.7.8:8888 → destination: 1.2.3.4:15555`

**4. Peer A receives the response:**

* Peer A’s obfuscator sees a packet coming from `5.6.7.8:8888`.
* It checks its static bindings (`127.0.0.1:5555:7777`) and knows to deliver the packet to the local WireGuard instance on port `5555`.

#### Summary

With static bindings, each obfuscator knows in advance how to forward packets between the server and local WireGuard, regardless of which peer initiates the connection. This enables fully bidirectional, peer-to-peer WireGuard tunnels—*even if both sides can initiate connections at any time.*


## How to build and install

You can always find the latest release (source code and ready-to-use binaries for Linux, Windows, and macOS) at:
https://github.com/ClusterM/wg-obfuscator/releases

### Linux
On Linux, the obfuscator can be installed as a systemd service for automatic startup and management.

To build and install on Linux, simply run:
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

### Windows
To build on Windows, you need [MSYS2](https://www.msys2.org/) and the following packages:
* `base-devel`
* `gcc`
* `git`
* `libargp-devel`

Install the required packages, then run:
```sh
make
```
> **Note:** On Windows, the obfuscator is only available as a command-line application. Run it from the MSYS2 terminal and manage startup manually.

### macOS
On macOS, install the `argp-standalone` package via Homebrew:
```sh
brew install argp-standalone
```

Then build as usual:
```sh
make
```
> **Note:** On macOS, the obfuscator is only available as a command-line application. You need to run it from the terminal and manage startup yourself.

### Android
Android support is planned.

### Running Docker Container on Linux

WireGuard Obfuscator is available as a multi-architecture Docker image:
**[clustermeerkat/wg-obfuscator on Docker Hub](https://hub.docker.com/repository/docker/clustermeerkat/wg-obfuscator)**

**Supported tags:**

* **`latest`** — always points to the most recent stable release.
* **`nightly`** — built automatically from the current main branch; may be unstable. Use only for testing new features.
* **Version tags** (e.g. `1.0`, `1.1`) — for specific releases.

**Architectures available:**

* `linux/amd64`
* `linux/arm64`
* `linux/arm/v7`
* `linux/arm/v6`
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

WireGuard Obfuscator can run as a container on MikroTik devices with **RouterOS 7.4+** (ARM64/x86\_64).

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

##### 4. Create a veth interface for container networking

```shell
/interface/veth/add name=veth-wg-ob address=192.168.100.2/24 gateway=192.168.100.1
```

##### 5. Create and mount a config directory

```shell
/container/mounts/add dst=/etc/wg-obfuscator name=wg-obfuscator-config src=/wg-obfuscator
```

##### 6. Add and start the container

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

##### 7. Edit your config file

* After the **first launch**, a default example config file will appear at `/wg-obfuscator/wg-obfuscator.conf` on your router.
* **Edit this file** to match your actual WireGuard and obfuscator settings.
  You can use WinBox, WebFig, or the `/file edit` command in the MikroTik terminal.

##### 8. Forward UDP ports to the container

```shell
/ip firewall nat add chain=dstnat action=dst-nat protocol=udp dst-port=13255 to-addresses=192.168.100.2 to-ports=13255
```

* Replace `13255` and the IP as needed for your network.

##### 9. Check logs

```shell
/container/print logs
```

**Notes:**

* `container` package and device-mode are only needed once per router.
* No external disk is required; image is small and uses internal storage.
* See [MikroTik Containers Docs](https://help.mikrotik.com/docs/display/ROS/Containers) for advanced usage.


## Caveats and Recommendations

* **Endpoint Exclusion and Routing Loops:**  
  WireGuard automatically excludes the server's IP address (as specified in the `Endpoint`) from the `AllowedIPs` list.
  If the obfuscator is running on the same machine as your WireGuard client or server, this can lead to a subtle issue: after the handshake, all traffic to the VPN server might get routed *through the VPN tunnel itself* (causing a routing loop or connection loss).
  **Solution:** Make sure to manually exclude the real (public) IP address of your VPN server from the `AllowedIPs` list in your WireGuard config.
* **PersistentKeepalive:**  
  To maintain a stable connection—especially when clients are behind NAT or firewalls—it is recommended to use WireGuard’s `PersistentKeepalive` option. A value of `25` or `60` seconds is generally sufficient.
* **Initial Handshake Requirement:**  
  After starting the obfuscator, no traffic will flow between WireGuard peers until a successful handshake has been established.
  If you restart the obfuscator *without* restarting WireGuard itself, it may take some time for the peers to re-establish the handshake and resume traffic. You can speed this up by briefly toggling the WireGuard interface.


## Credits
* [WireGuard](https://www.wireguard.com/) - the VPN protocol this tool is designed to obfuscate.
* [uthash](https://troydhanson.github.io/uthash/) - a great C library for hash tables, used for the NAT table.


## Donate

* [Buy Me A Coffee](https://www.buymeacoffee.com/cluster)
* [Donation Alerts](https://www.donationalerts.com/r/clustermeerkat)
* [Boosty](https://boosty.to/cluster)
* PayPal is not available in Armenia :(
