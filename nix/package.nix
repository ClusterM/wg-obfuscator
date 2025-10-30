{ lib
, stdenv
, fetchFromGitHub
, version ? "dirty"
}:

stdenv.mkDerivation rec {
  pname = "wg-obfuscator";
  inherit version;

  src = lib.cleanSource ../.;

  makeFlags = [
    "RELEASE=${if lib.hasInfix "dirty" version then "0" else "1"}"
  ];

  # Use the existing Makefile
  buildPhase = ''
    runHook preBuild
    make $makeFlags
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    # Install binary
    install -Dm755 wg-obfuscator $out/bin/wg-obfuscator

    # Install example config
    install -Dm644 wg-obfuscator.conf $out/share/doc/wg-obfuscator/wg-obfuscator.conf.example

    # Install systemd service (for reference)
    install -Dm644 wg-obfuscator.service $out/lib/systemd/system/wg-obfuscator.service

    runHook postInstall
  '';

  meta = with lib; {
    description = "WireGuard traffic obfuscator to bypass DPI";
    longDescription = ''
      WireGuard Obfuscator is a lightweight, zero-dependency tool that disguises
      WireGuard VPN traffic as random data or other protocols (like STUN) to bypass
      Deep Packet Inspection (DPI) systems.

      Features:
      - Compact and dependency-free
      - Independent from WireGuard client/server
      - Minimal bandwidth overhead
      - Protocol masking (STUN emulation)
      - Built-in NAT table for multiple clients
      - Two-way mode with static bindings
    '';
    homepage = "https://github.com/ClusterM/wg-obfuscator";
    license = licenses.mit; # Update based on actual LICENSE file
    maintainers = [ ];
    platforms = platforms.unix;
    mainProgram = "wg-obfuscator";
  };
}
