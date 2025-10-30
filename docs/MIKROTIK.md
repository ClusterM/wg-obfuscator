# Running WireGuard Obfuscator on MikroTik Routers

WireGuard Obfuscator can run as a Docker container on MikroTik devices with **RouterOS 7.4+** (ARM64/x86_64). Update to the latest RouterOS version to ensure you have all the latest container features and fixes.

## Quick Setup

### 1. Install the `container` package

* Download the latest **Extra Packages** for your RouterOS version and platform from [mikrotik.com/download](https://mikrotik.com/download)
* Extract and upload `container-*.npk` to the router
* Reboot the router

### 2. Enable container device mode (**only required once!**)

```shell
/system/device-mode/update container=yes
```

* Confirm when prompted:
  – For most models, press the physical reset button
  – On x86, do a full power-off (cold reboot)

### 3. Configure container registry (one time)

```shell
/container/config/set registry-url=https://registry-1.docker.io tmpdir=temp
```

### 4. Enable container logging (one time)

First of all, you need to enable logging for the container subsystem. This is required to see container logs in the RouterOS log using the `/log` command.
```shell
/system logging add topics=container
```

### 5. Create a veth interface for container networking

```shell
/interface/veth/add name=veth-wg-ob address=172.17.13.2/24 gateway=172.17.13.1
```
> **Note:** in this example, we use IP address `172.17.13.2` for the container and `172.17.13.1` for the host.
> You can choose any other IP addresses as long as they are in the same subnet.

### 6. Add IP address for the veth interface

```shell
/ip address add address=172.17.13.1/24 interface=veth-wg-ob
```
> **Note:** This IP address is used by the host to communicate with the container. It must match the gateway set in the veth interface.

### 7. Forward UDP ports to the container

In case if your obfuscator is configured to accept incoming connections from a server outside of the NAT, you need to forward the UDP port to the container. 
For client side obfuscator without two-way mode (static bindings), you can skip this step.

```shell
/ip firewall nat add chain=dstnat action=dst-nat protocol=udp dst-port=13255 to-addresses=172.17.13.2 to-ports=13255
```
> **Note:** This IP address is used by the container, it must match the `address` in the veth interface.

> **Note:** Port number `13255` is just an example. It must port you are using for incoming connections in your obfuscator configuration file (i.e. `source-lport` or port from static bindings).

### 8. Create and mount a config directory

```shell
/container/mounts/add dst=/etc/wg-obfuscator name=wg-obfuscator-config src=/wg-obfuscator
```

### 9. Add the container

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

### 10. Check the logs

```shell
/log print where topics~"container"
```
You should see `import successful` message.

### 11. Start the container

After the container is added, you can start it using the following command:
```shell
/container/start [/container/find where name="wg-obfuscator"]
```

### 12. Check the logs

```shell
/log print where topics~"container"
```
You should see logs indicating the container has started successfully.

### 13. Edit your config file

* After the **first launch**, a default example config file will appear at `/wg-obfuscator/wg-obfuscator.conf` on your router.
* **Edit this file** to match your actual WireGuard and obfuscator settings.
  You can use WinBox, WebFig, or the `/file edit` command in the MikroTik terminal.

### 14. Restart the container

```shell
/container/stop [/container/find where name="wg-obfuscator"]
/container/start [/container/find where name="wg-obfuscator"]
```

### 15. Check the logs

```shell
/log print where topics~"container"
```
You should see logs indicating the container has started successfully and is ready to process WireGuard traffic.

## Important Notes

* `container` package and device-mode are only needed once per router.
* No external disk is required; image is small and uses internal storage.
* See [MikroTik Containers Docs](https://help.mikrotik.com/docs/display/ROS/Containers) for advanced usage.
* Don't forget to change WireGuard's `Endpoint` to point to the obfuscator's IP and port.
* **Important:** See the main README for information about [avoiding routing loops](../README.md#avoiding-routing-loops) and other best practices.

## Troubleshooting

If you encounter issues:

1. Check container status: `/container/print`
2. View logs: `/log print where topics~"container"`
3. Verify network configuration: `/ip address print` and `/interface/veth/print`
4. Ensure the config file exists and is valid: `/file print where name~"wg-obfuscator"`
5. Make sure WireGuard endpoint points to the obfuscator's IP:port, not the VPN server directly

