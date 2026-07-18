# Agent Guidelines

This repository is the Zephyr application repository. When using the Dev Container,
it is mounted into a Zephyr west workspace at
`/workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge`.

## Agent skills

### Issue tracker

Issues and PRDs are tracked in GitHub Issues for `milindur/esp-nmea-bridge`. See `docs/agents/issue-tracker.md`.

### Triage labels

This repo uses the default five-label triage vocabulary. See `docs/agents/triage-labels.md`.

### Domain docs

This repo uses a single-context domain documentation layout. See `docs/agents/domain.md`.

## Dev Container / Zephyr commands

- Pi is expected to run on the host.
- Build, test, and other Zephyr/west commands should run inside the Dev Container via the Dev Container CLI.
- Do not rely on a VS Code-started Dev Container. Start or reuse the CLI-managed container first:
  - `devcontainer up --workspace-folder .`
- Then execute commands with:
  - `devcontainer exec --workspace-folder . /bin/bash -lc '<command>'`
- Inside the Dev Container, use `/workspaces/esp-nmea-bridge-workspace` as the west workspace root and `/workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge` as the application directory.
- Run build, test, and flash commands from the application directory. The build directory is `build/` inside the application directory; Twister output goes to `build/twister/`. Because the application repository is bind-mounted, both are accessible on the host at `esp-nmea-bridge/build/`.

Build:

```sh
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . /bin/bash -lc \
  'cd /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge && west build -p always --sysbuild -b esp32c6_dev_kit_n8/esp32c6/hpcore . -- -DEXTRA_CONF_FILE=local.conf'
```

Test:

```sh
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . /bin/bash -lc \
  'cd /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge && west twister -c -O build/twister -T tests/ --inline-logs'
```

Flash:

```sh
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . /bin/bash -lc \
  'cd /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge && west flash --esp-device /dev/waveshare/esp32c6-dev-kit-n8/jtag'
```

Monitor console:

```sh
devcontainer exec --workspace-folder . /bin/bash -lc \
  'tio /dev/waveshare/esp32c6-dev-kit-n8/uart -b 115200'
```
