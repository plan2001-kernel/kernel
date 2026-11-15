#!/bin/sh
# Compile the exact kernel decoder without booting a guest.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT HUP INT TERM
"${CC:-cc}" -Wall -Wextra -Werror "$root/tools/host/console-input/decoder.c" -o "$out/decoder"
"$out/decoder"
