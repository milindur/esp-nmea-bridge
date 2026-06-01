#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=/workspaces/esp-nmea-bridge-workspace

cd "${WORKSPACE}"
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west packages pip --install
west blobs fetch hal_espressif
