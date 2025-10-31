{
  description = "WireGuard Obfuscator - disguise WireGuard traffic to bypass DPI";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    let
      # Overlay to add wg-obfuscator to pkgs
      overlay = final: prev: {
        wg-obfuscator = final.callPackage ./nix/package.nix {
          version = self.rev or "dirty";
        };
      };
    in
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ overlay ];
        };
      in
      {
        packages = {
          default = pkgs.wg-obfuscator;
          wg-obfuscator = pkgs.wg-obfuscator;
        };

        apps = {
          default = {
            type = "app";
            program = "${pkgs.wg-obfuscator}/bin/wg-obfuscator";
          };
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc
            gnumake
            gdb
          ];

          shellHook = ''
            echo "WireGuard Obfuscator development environment"
            echo "Build with: make"
            echo "Debug build: DEBUG=1 make"
            echo "Clean: make clean"
          '';
        };

        # Formatter for `nix fmt`
        formatter = pkgs.nixpkgs-fmt;
      }
    ) // {
      # NixOS module
      nixosModules.default = { config, lib, pkgs, ... }:
        with lib;
        let
          cfg = config.services.wg-obfuscator;

          # Format for instance configuration
          instanceOpts = { name, ... }: {
            options = {
              enable = mkEnableOption "this wg-obfuscator instance";

              listenPort = mkOption {
                type = types.port;
                description = "Port to listen for incoming connections";
                example = 13255;
              };

              target = mkOption {
                type = types.str;
                description = "Target address and port to forward to (address:port)";
                example = "10.13.1.100:13255";
              };

              key = mkOption {
                type = types.str;
                description = "Obfuscation key (must match on both sides)";
                example = "your_secret_key";
              };

              keyFile = mkOption {
                type = types.nullOr types.path;
                default = null;
                description = "File containing the obfuscation key (alternative to key option)";
                example = "/run/secrets/wg-obfuscator-key";
              };

              interface = mkOption {
                type = types.str;
                default = "0.0.0.0";
                description = "Network interface to bind to";
              };

              masking = mkOption {
                type = types.enum [ "NONE" "AUTO" "STUN" ];
                default = "AUTO";
                description = "Protocol masking type for DPI evasion";
              };

              staticBindings = mkOption {
                type = types.nullOr types.str;
                default = null;
                description = "Static bindings for two-way mode (client_ip:client_port:forward_port)";
                example = "1.2.3.4:12883:6670, 5.6.7.8:12083:6679";
              };

              fwmark = mkOption {
                type = types.nullOr types.int;
                default = null;
                description = "Firewall mark to set on packets (0-65535)";
                example = 57005; # 0xdead
              };

              verbose = mkOption {
                type = types.enum [ "ERRORS" "WARNINGS" "INFO" "DEBUG" "TRACE" ];
                default = "INFO";
                description = "Logging verbosity level";
              };

              maxClients = mkOption {
                type = types.int;
                default = 1024;
                description = "Maximum number of simultaneous clients";
              };

              idleTimeout = mkOption {
                type = types.int;
                default = 300;
                description = "Idle timeout in seconds";
              };

              maxDummy = mkOption {
                type = types.int;
                default = 4;
                description = "Maximum dummy data length for data packets (0-1024)";
              };
            };
          };

          # Generate config file content
          mkConfig = instances: concatStringsSep "\n\n" (
            mapAttrsToList (name: inst: ''
              [${name}]
              source-if = ${inst.interface}
              source-lport = ${toString inst.listenPort}
              target = ${inst.target}
              ${if inst.keyFile != null then "# key loaded from keyFile" else "key = ${inst.key}"}
              masking = ${inst.masking}
              ${optionalString (inst.staticBindings != null) "static-bindings = ${inst.staticBindings}"}
              ${optionalString (inst.fwmark != null) "fwmark = ${toString inst.fwmark}"}
              verbose = ${inst.verbose}
              max-clients = ${toString inst.maxClients}
              idle-timeout = ${toString inst.idleTimeout}
              max-dummy = ${toString inst.maxDummy}
            '') (filterAttrs (_: inst: inst.enable) instances)
          );

          configFile = pkgs.writeText "wg-obfuscator.conf" (mkConfig cfg.instances);

          # Check if any instance uses keyFile
          anyKeyFile = any (inst: inst.enable && inst.keyFile != null) (attrValues cfg.instances);

        in
        {
          options.services.wg-obfuscator = {
            enable = mkEnableOption "WireGuard Obfuscator service";

            package = mkOption {
              type = types.package;
              default = pkgs.wg-obfuscator or (pkgs.callPackage ./nix/package.nix { });
              defaultText = literalExpression "pkgs.wg-obfuscator";
              description = "The wg-obfuscator package to use";
            };

            instances = mkOption {
              type = types.attrsOf (types.submodule instanceOpts);
              default = { };
              description = "Named instances of wg-obfuscator to run";
              example = literalExpression ''
                {
                  main = {
                    enable = true;
                    listenPort = 13255;
                    target = "10.13.1.100:13255";
                    key = "your_secret_key";
                  };
                }
              '';
            };
          };

          config = mkIf cfg.enable {
            assertions = [
              {
                assertion = cfg.instances != { };
                message = "At least one wg-obfuscator instance must be configured";
              }
              {
                assertion = all (inst:
                  inst.enable -> (inst.key != "" || inst.keyFile != null)
                ) (attrValues cfg.instances);
                message = "Each enabled wg-obfuscator instance must have either 'key' or 'keyFile' set";
              }
            ];

            systemd.services.wg-obfuscator = {
              description = "WireGuard Obfuscator";
              after = [ "network.target" ];
              wantedBy = [ "multi-user.target" ];

              serviceConfig = {
                Type = "simple";
                Restart = "always";
                RestartSec = 10;
                User = "root"; # Required for fwmark and low-level networking

                # Security hardening
                NoNewPrivileges = true;
                PrivateTmp = true;
                ProtectSystem = "strict";
                ProtectHome = true;
                ProtectKernelTunables = true;
                ProtectKernelModules = true;
                ProtectControlGroups = true;
                RestrictAddressFamilies = [ "AF_INET" "AF_INET6" ];
                RestrictNamespaces = true;
                LockPersonality = true;
                RestrictRealtime = true;
                RestrictSUIDSGID = true;
                PrivateMounts = true;

                # Capabilities needed for networking and fwmark
                AmbientCapabilities = [ "CAP_NET_BIND_SERVICE" "CAP_NET_RAW" ];
                CapabilityBoundingSet = [ "CAP_NET_BIND_SERVICE" "CAP_NET_RAW" ];
              };

              # Runtime config generation with keyFile support
              script = if anyKeyFile then
                let
                  runtimeConfig = "/run/wg-obfuscator/config.conf";
                  instances = filterAttrs (_: inst: inst.enable) cfg.instances;
                in
                ''
                  mkdir -p /run/wg-obfuscator

                  # Generate config with keys from keyFiles
                  cat > ${runtimeConfig} <<'EOF'
                  ${concatStringsSep "\n\n" (
                    mapAttrsToList (name: inst: ''
                      [${name}]
                      source-if = ${inst.interface}
                      source-lport = ${toString inst.listenPort}
                      target = ${inst.target}
                      masking = ${inst.masking}
                      ${optionalString (inst.staticBindings != null) "static-bindings = ${inst.staticBindings}"}
                      ${optionalString (inst.fwmark != null) "fwmark = ${toString inst.fwmark}"}
                      verbose = ${inst.verbose}
                      max-clients = ${toString inst.maxClients}
                      idle-timeout = ${toString inst.idleTimeout}
                      max-dummy = ${toString inst.maxDummy}
                    '') (attrValues instances)
                  )}
                  EOF

                  # Append keys from keyFiles or inline keys
                  ${concatStringsSep "\n" (
                    imap0 (i: inst:
                      let
                        sectionName = elemAt (attrNames instances) i;
                      in
                      if inst.keyFile != null then ''
                        echo "key = $(cat ${inst.keyFile})" >> ${runtimeConfig}
                      '' else ''
                        sed -i '/\[${sectionName}\]/a key = ${inst.key}' ${runtimeConfig}
                      ''
                    ) (attrValues instances)
                  )}

                  exec ${cfg.package}/bin/wg-obfuscator -c ${runtimeConfig}
                ''
              else
                ''
                  exec ${cfg.package}/bin/wg-obfuscator -c ${configFile}
                '';
            };

            # Open firewall ports for all enabled instances
            networking.firewall.allowedUDPPorts =
              map (inst: inst.listenPort)
              (filter (inst: inst.enable) (attrValues cfg.instances));
          };
        };

      # Overlay export
      overlays.default = overlay;
    };
}
