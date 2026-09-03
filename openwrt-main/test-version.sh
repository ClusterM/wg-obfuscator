#!/bin/sh
# Version check override for the OpenWrt package CI, invoked on the target as
# "test-version.sh <package name> <package version>".
#
# wg-obfuscator has no --version flag: it prints its banner to stderr before
# the argument parser runs, so --help is the cheapest invocation that both
# exits successfully and reveals the version. Providing this override also
# stops the CI from probing wg-obfuscator-config.sh for a version, which would
# run the generator against UCI.

expected="$2"

output=$(/usr/bin/wg-obfuscator --help 2>&1)

if echo "$output" | grep -qF "WireGuard Obfuscator v$expected"; then
	echo "wg-obfuscator reports version $expected"
	exit 0
fi

echo "wg-obfuscator did not report version $expected, output was:"
echo "$output" | head -n 5
exit 1
