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
|  Oobfuscator   |
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

Are are three possible configurations of a WireGuard peer:
* **Client**: The peer is a client with a dynamic IP address or behind a NAT, it can only initiate connections to the fixed IP address of the server.
* **Server**: The peer is a server with a static IP address, it listens for incoming connections but can't initiate outgoing connections.
* **Hybrid**: The peer has a static IP address, listens for incoming connections but can also initiate outgoing connections to the fixed IP address of the server.

### Client
For example you have a simple configuration like this:
```
[Interface]
PublicKey = ...
Address = ...

[Peer]
PublicKey = ...
AllowedIPs = 10.13.0.0/16
Endpoint = example.com:13300
```

You can change it to this (endpoint is specified but listen port is not):
```
[Interface]
PublicKey = ...
Address = ...

[Peer]
PublicKey = ...
AllowedIPs = ...
Endpoint = 127.0.0.1:13200
```
Where `127.0.0.1:13200` is IP address and port of the obfuscator. You can run it on the same PC as the WireGuard peer or on the same network.

Obfuscator configuration will be like this:
```
listen_port = 13200

# Host and port of the server-side obfuscator
forward_to = server.example.com:13300

# Obfuscation key, must be the same on both sides
key = test
```

### Server
For example you have a simple configuration like this (listen port is specified but endpoint is not):
```
[Interface]
PublicKey = ...
Address = ...
ListenPort = 13300

[Peer]
PublicKey = ...
AllowedIPs = ...
```

You can change it to this, just change the listen port:
```
[Interface]
PublicKey = ...
Address = ...
ListenPort = 13333

[Peer]
PublicKey = ...
AllowedIPs = ...
```

Obfuscator configuration will be like this:
```
# Port to listen for the client, e.g. same as "forward_port" in the client configuration
listen_port = 13300

# Host and port of the real WireGuard server
forward_to = 127.0.0.1:13333

key = test
```

### Hybrid
In such case you can specify IP address and port of the other side obfuscator, just add line:
```
client_fixed_addr = 1.2.3.4:31241
```
Where `1.2.3.4:31241` is IP address and port of the other side obfuscator. This option will disable automatic client address/port detection based on a handshake and will permanently use the specified address. In such case your server can initiate connections to the other side too.

So, the configuration will be like this:
```
listen_port = 13300

# Host and port of the real WireGuard server
forward_to = 127.0.0.1:13333

client_fixed_addr = 1.2.3.4:31241
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
