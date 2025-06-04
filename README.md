# WireGuard obfuscator

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
* **Multi-Section Config Files**  
  Supports both simple config files and command-line arguments for quick one-off runs or advanced automation. You can define multiple obfuscator instances within a single config file.
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


## How it works

TODO!!!

```
+----------------+
| WireGuard peer |
+----------------+
        ^
        |
        v
+----------------+
|   Obfuscator   |
+----------------+
        ^
        |
        v
+----------------+
|   Internet     |
+----------------+
        ^
        |
        v
+----------------+
|   Obfuscator   |
+----------------+
        ^
        |
        v
+----------------+
| WireGuard peer |
+----------------+
```

Since the obfuscator is a simple XOR cipher, it is totally simmetric. You need to install this application on the same network as the WireGuard peer you want to obfuscate, you need to do this on the other peer too. The obfuscator will then obfuscate the WireGuard packets and send them to the Internet. On the other side the obfuscator will deobfuscate the packets and send them to the WireGuard peer.

It can be used even if one of the peers is behind a NAT or has a dynamic IP address. The obfuscator will keep track of the IP address of the peer after handshake and will send the packets to the correct IP address.


## How to use

You can pass parameters to the obfuscator using a configuration file or command line arguments. Available parameters are:
* `source-if` - source interface to listen on. Optional, default is `0.0.0.0`, e.g. all interfaces. Can be used to listen only on a specific interface.
* `source` - source client address and port in `address:port` format. Optional. By default any address and port is accepted but server replies will be sent to the last successfully handshake address, so it can work over NAT. If specified, only packets from this address will be accepted and all server replies will be sent to this address, in such case target side cat initiate connections to the source side too.
* `source-lport` - source port to listen. Source client should connect to this port. Required.
* `target-if` - target interface to listen on. Optional, default is `0.0.0.0`, e.g. all interfaces. Can be used to listen only on a specific interface.
* `target` - target address and port in `address:port` format. Obfuscated data will be sent to this address. Required.
* `target-lport` - target port to listen. Optional. Default is auto (assigned by the OS). If specified, target can initiate connections to the source side too.
* `key` - obfuscation key. Just string. Longer - better. Required.
* `verbose` - verbosity level, 0-4. Optional, default is 2.

You can use configuration file with those parameters in `key=value` format. For example:
```
# Instance name
[main]

# Port to listen for the source client (real client or client obfuscator)
source-lport = 13255

# Host and port of the target to forward to (server obfuscator or real server)
target = 10.13.1.100:13255

# Obfuscation key, must be the same on both sides
key = test

# You can specify multiple instances, so you can obfuscate multiple connections:
[second_server]
source-lport = 13256
target = 10.13.1.101:13255
key = test2
```

You can pass the configuration file to the obfuscator using `--config` argument. For example:
```bash
wg-obfuscator --config /etc/wg-obfuscator.conf
```

You can also pass parameters using command line arguments. For example:
```bash
wg-obfuscator --source-lport 13255 --target 10.13.1.100:13255 --key test
```
Type `wg-obfuscator.exe --help` for more information.

### Settings diagram

```
+------------------------------------------------------------------------------------------+
|                                 Source WireGuard peer                                    |
| ListenPort         = <from "source" on the source obfuscator                             |
|                      (required only for hybrid configuration)>                           |
+------------------------------------------------------------------------------------------+
| Endpoint           = <source obfuscator's IP : source obfuscator's "source-lport">       |
+------------------------------------------------------------------------------------------+
                                            ^
                                            |
                                            v
+------------------------------------------------------------------------------------------+
|                                   Source obfuscator                                      |
| source             = <source WireGuard peer's IP : source WireGuard peer's "ListenPort"  |
|                    = (required only for hybrid configuration)>                           |
| source-lport       = <port from "Endpoint" on the source WireGuard peer>                 |
+------------------------------------------------------------------------------------------+
| target-lport       = <port from "source" on the target obfuscator                        |
|                    = (required only for hybrid configuration)>                           |
| target             = <target obfuscator's IP and target obfuscator's "source-lport">     |
+------------------------------------------------------------------------------------------+
                                            ^
                                            |
                                            v
+------------------------------------------------------------------------------------------+
|                                       Internet                                           |
+------------------------------------------------------------------------------------------+
                                            ^
                                            |
                                            v
+------------------------------------------------------------------------------------------+
|                                    Tartget obfuscator                                    |
| source             = <soucrce obfuscator's IP : source obfuscator's "target-lport"       |
|                    = (required only for hybrid configuration)>                           |
| source-lport       = <port from "target" on the source obfuscator>                       |
+------------------------------------------------------------------------------------------+
| target-lport       = <port from target WireGuard peer's "Endpoint">                      |
|                    = (required only for hybrid configuration)>                           |
| target             = <target WireGuard peer's IP : target WireGuard peer's "ListenPort"> |
+------------------------------------------------------------------------------------------+
                                            ^
                                            |
                                            v
+------------------------------------------------------------------------------------------+
|                                   Target WireGuard peer                                  |
| ListenPort         = <from "target" on the target obfuscator>                            |
+------------------------------------------------------------------------------------------+
| Endpoint           = <target obfuscator's IP : target obfuscator's "target-lport">       |
|                      (required only for hybrid configuration)>                           |
+------------------------------------------------------------------------------------------+
```

## How to build and install

On Linux:
```bash
make
sudo make install
```

It will be installed as a systemd service. You can start it with:
```bash
sudo systemctl start wg-obfuscator
```
Configularion file is located at `/etc/wg-obfuscator.conf`.

You can also run it from the command line, type `wg-obfuscator --help` for more information.

On Windows and MacOS you can only run it from the command line.


## Caveats

WireGuard automatically excludes the IP address of the server specified in the Endpoint from the `AllowedIPs` list. This can cause a non-obvious issue: if the obfuscator is located on the local machine, after the handshake, the traffic to the VPN server will be routed through the VPN itself. Therefore, you need to manually exclude the real IP address of the VPN server from the `AllowedIPs` list.


## Donate

* [Buy Me A Coffee](https://www.buymeacoffee.com/cluster)
* [Donation Alerts](https://www.donationalerts.com/r/clustermeerkat)
* [Boosty](https://boosty.to/cluster)
* PayPal is not available in Armenia :(
