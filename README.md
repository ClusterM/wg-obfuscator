# WireGuard obfuscator

This is a simple obfuscator for WireGuard. It is designed to make it harder to detect WireGuard traffic by making it look like something else. It does this by wrapping WireGuard packets in a layer of obfuscation using a simple XOR cipher. Usefull for bypassing DPI (Deep Packet Inspection) firewalls, e.g. if your ISP/government blocks WireGuard traffic.

## How it works

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
# Port to listen for the source client (real client or client obfuscator)
source-lport = 13255

# Host and port of the target to forward to (server obfuscator or real server)
target = 10.13.1.100:13255

# Obfuscation key, must be the same on both sides
key = test
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
| Endpoint           = <source obfuscator's IP : source obfuscator's "source-lport">        |
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
| target             = <target obfuscator's IP and target obfuscator's "source-lport">      |
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

## Donate
* [Buy Me A Coffee](https://www.buymeacoffee.com/cluster)
* [Donation Alerts](https://www.donationalerts.com/r/clustermeerkat)
* [Boosty](https://boosty.to/cluster)
* PayPal is not available in Armenia :(
