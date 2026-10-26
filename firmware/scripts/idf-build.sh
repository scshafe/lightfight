#!/usr/bin/env bash
# Build the ESP-IDF firmware for each role in the espressif/idf container. No
# board is touched: this only compiles. Usage: idf-build.sh [role ...]
# (default: blaster blaster-bench hub; each is an sdkconfig.<role> overlay). Output stays in the container (/tmp), the repository is
# mounted read-only apart from the throwaway build directories.
set -euo pipefail
cd "$(dirname "$0")/../.."

IMAGE="${LF_IDF_IMAGE:-espressif/idf:v5.4.2}"
roles=("$@")
[ ${#roles[@]} -gt 0 ] || roles=(blaster blaster-bench hub)

for role in "${roles[@]}"; do
  echo "== idf build: $role ($IMAGE)"
  docker run --rm -v "$PWD":/repo:ro -w /tmp "$IMAGE" bash -c "
    set -euo pipefail
    cp -r /repo/firmware /tmp/firmware
    cp -r /repo/protocol /tmp/protocol 2>/dev/null || true
    cd /tmp/firmware/idf
    idf.py -B /tmp/build-$role -DIDF_TARGET=esp32c3 \
      -DSDKCONFIG=/tmp/sdkconfig-$role \
      -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.$role' build 2>&1 | tail -25
    idf.py -B /tmp/build-$role size > /tmp/size-$role.txt 2>&1 || true
    grep -E 'Total image size|Flash Code|DRAM ' /tmp/size-$role.txt || true
  "
done
