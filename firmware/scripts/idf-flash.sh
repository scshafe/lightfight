#!/usr/bin/env bash
# Owner step: build one role and flash a connected board through the
# espressif/idf container. Usage: idf-flash.sh <blaster|hub> /dev/ttyACM0
# Needs the owner's approval each time (AGENTS.md); the agent does not run it.
set -euo pipefail
role="${1:?role: blaster or hub}"
port="${2:?serial device, e.g. /dev/ttyACM0}"
cd "$(dirname "$0")/../.."
IMAGE="${LF_IDF_IMAGE:-espressif/idf:v5.4.2}"
docker run --rm -it --device "$port" -v "$PWD":/repo:ro -w /tmp "$IMAGE" bash -c "
  set -euo pipefail
  cp -r /repo/firmware /tmp/firmware
  cd /tmp/firmware/idf
  idf.py -B /tmp/build -DIDF_TARGET=esp32c3 -DSDKCONFIG=/tmp/sdkconfig \
    -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.$role' -p '$port' flash monitor
"
