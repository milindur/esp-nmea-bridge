#!/usr/bin/env bash
set -euo pipefail

WORKSPACE=/workspaces/esp-nmea-bridge-workspace
APP=esp-nmea-bridge
APP_DIR=${WORKSPACE}/${APP}

cd "${WORKSPACE}"

west update

bash "${APP_DIR}/.devcontainer/sync-workspace.sh"
