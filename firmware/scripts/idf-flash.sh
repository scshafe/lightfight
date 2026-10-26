#!/usr/bin/env bash
# Owner step: build one role and flash a connected board through the
# espressif/idf container. Usage: idf-flash.sh <role> /dev/ttyACM0
# Roles are the overlays firmware/idf/sdkconfig.<role>: blaster, blaster-bench
# (one frame a second, for the waveform check), hub.
# Needs the owner's approval each time (AGENTS.md); the agent does not run it.
set -euo pipefail
cd "$(dirname "$0")/../.."
roles=$(ls firmware/idf | sed -n 's/^sdkconfig\.\([a-z-]*\)$/\1/p' | grep -v '^defaults$' | tr '\n' ' ')
role="${1:?role: one of $roles}"
port="${2:?serial device, e.g. /dev/ttyACM0}"
[ -f "firmware/idf/sdkconfig.$role" ] && [ "$role" != defaults ] || { echo "unknown role '$role'; roles: $roles" >&2; exit 2; }
[ -c "$port" ] || { echo "$port is not a serial device" >&2; exit 2; }
IMAGE="${LF_IDF_IMAGE:-espressif/idf:v5.4.2}"
docker run --rm -it --device "$port" -v "$PWD":/repo:ro -w /tmp "$IMAGE" bash -c "
  set -euo pipefail
  cp -r /repo/firmware /tmp/firmware
  cd /tmp/firmware/idf
  idf.py -B /tmp/build -DIDF_TARGET=esp32c3 -DSDKCONFIG=/tmp/sdkconfig \
    -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.$role' -p '$port' flash monitor
"
