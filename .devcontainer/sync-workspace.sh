#!/usr/bin/env bash
set -euo pipefail

# Shared sync steps used by setup-workspace.sh and update-workspace.sh.
# Requires an initialized west workspace with an up-to-date zephyr/ checkout.

WORKSPACE=/workspaces/esp-nmea-bridge-workspace

cd "${WORKSPACE}"

# Keep Python dependencies in sync with the cached Zephyr checkout. The Docker
# image/Python venv may be rebuilt while the persistent west volume already
# contains zephyr/. This must run before any Zephyr west extension command
# (including zephyr-export): loading those extensions already imports packages
# from these requirements (e.g. jsonschema), which a fresh venv lacks.
pip install -r zephyr/scripts/requirements.txt
west packages pip --install

west zephyr-export

# Fetcher backends used by Zephyr blobs depend on packages installed above
# (notably requests/jsonschema), so keep this after Python dependency sync. Run
# it on every sync so a failed first run does not leave the cached workspace
# without required ESP-IDF blobs.
west blobs fetch hal_espressif

# Install the Zephyr SDK version pinned by the Zephyr checkout (SDK_VERSION
# file) into the persistent volume so image rebuilds do not re-download it.
# 'west sdk install' only detects existing SDKs via the CMake user package
# registry (~/.cmake), which an image rebuild wipes, so track completeness
# ourselves with a stamp file and only re-register a fully installed SDK. A
# missing stamp means a previous install was interrupted; remove the partial
# tree so the reinstall starts clean.
SDK_VERSION=$(cat zephyr/SDK_VERSION)
SDK_DIR=${WORKSPACE}/zephyr-sdk-${SDK_VERSION}
SDK_STAMP=${SDK_DIR}/.sync-workspace-complete
if [ -e "${SDK_STAMP}" ]; then
  "${SDK_DIR}/setup.sh" -c
else
  rm -rf "${SDK_DIR}"
  west sdk install -b "${WORKSPACE}" -t riscv64-zephyr-elf x86_64-zephyr-elf
  touch "${SDK_STAMP}"
fi
