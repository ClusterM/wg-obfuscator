# WireGuard obfuscator

This is a simple obfuscator for WireGuard. It is designed to make it harder to detect WireGuard traffic by making it look like something else. It does this by wrapping WireGuard packets in a layer of obfuscation using a simple XOR cipher. Usefull for bypassing DPI (Deep Packet Inspection) firewalls, e.g. if your ISP/government blocks WireGuard traffic.
