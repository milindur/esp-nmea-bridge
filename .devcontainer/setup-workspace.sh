#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=/workspaces/esp-nmea-bridge-workspace
APP=esp-nmea-bridge
APP_DIR=${WORKSPACE}/${APP}

if [ ! -d "${APP_DIR}" ]; then
  echo "App directory not mounted: ${APP_DIR}" >&2
  exit 1
fi

# The parent workspace is a named Docker volume. On first use Docker may create it
# as root, while the app repository is a host bind mount. Make only the volume
# root writable by the remote user; do not recursively chown the bind-mounted repo.
sudo chown "$(id -u):$(id -g)" "${WORKSPACE}"

cd "${WORKSPACE}"

if [ ! -d .west ]; then
  echo "Initializing west workspace from ${APP}/west.yml"
  west init -l "${APP}"
fi

if [ ! -d zephyr ]; then
  echo "Fetching Zephyr projects and modules"
  west update
else
  echo "Zephyr checkout already exists; skipping west update."
  echo "Run this when west.yml changes: bash ${APP}/.devcontainer/update-workspace.sh"
fi

bash "${APP_DIR}/.devcontainer/sync-workspace.sh"

west topdir
