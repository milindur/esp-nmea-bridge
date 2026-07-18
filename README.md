# ESP NMEA Bridge

ESP NMEA Bridge turns an ESP32-C6 board into a Wi-Fi gateway for marine NMEA-0183 data. Connect a GPS, AIS receiver, or other NMEA device over UART, and the bridge makes that data available to navigation apps and other clients over TCP. It can create its own Wi-Fi network, join an existing one, serve multiple TCP clients, and optionally forward data to another TCP server.

The project also includes a board definition for the Waveshare `esp32c6_dev_kit_n8` with 8 MiB flash and optional status LED support.

## What it does

A typical setup is:

1. Connect an NMEA-0183 data source to the ESP32-C6 UART input.
2. Power the ESP32-C6 board.
3. Join the bridge's Wi-Fi network, or let the bridge join your existing Wi-Fi.
4. Configure your navigation software to read NMEA-0183 data from the bridge over TCP.

This is useful when you want to make serial GPS or AIS data available to software such as OpenCPN, Signal K, OpenPlotter, or another system that accepts NMEA-0183 over TCP.

The bridge supports two TCP modes:

- **TCP server**: navigation apps connect to the ESP32-C6 and receive NMEA data. This is the usual mode for phones, tablets, laptops, or chart plotter software.
- **TCP client**: the ESP32-C6 connects to another TCP NMEA server and forwards data to it, for example an OpenPlotter or Signal K host.

## Hardware

Supported board:

- Waveshare `esp32c6_dev_kit_n8` / `esp32c6_dev_kit_n8/esp32c6/hpcore`

NMEA UART input on the supported board:

- UART: `uart1` / devicetree alias `serial1`
- RX: GPIO5
- TX: GPIO4
- Baud rate: 38400

Only RX is required when the ESP32-C6 only receives NMEA data from a GPS, AIS receiver, or other instrument.

> [!WARNING]
> NMEA-0183 electrical signaling is not always 3.3 V UART. Use a suitable NMEA-0183/RS-422-to-3.3 V UART adapter when connecting real marine equipment. Do not connect higher-voltage or differential NMEA lines directly to ESP32-C6 GPIO pins.

The board's USB UART remains available for the Zephyr console and flashing.

## Software requirements

Start with the official Zephyr Getting Started Guide:

- <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>

Additional ESP32-C6/Espressif requirements:

- Zephyr SDK installed with RISC-V support
- USB access to the board
- Espressif RF binary blobs after `west update`:

```sh
west blobs fetch hal_espressif
```

## Set up a Zephyr workspace

This repository is the west manifest repository. It is cloned into `esp-nmea-bridge/` and pins the Zephyr revision plus Zephyr's module set in `west.yml`.

Example for a fresh workspace:

```sh
mkdir esp-nmea-bridge-workspace
cd esp-nmea-bridge-workspace

python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip west

west init -m https://github.com/milindur/esp-nmea-bridge.git --mr main
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west blobs fetch hal_espressif
```

If you cloned this repository manually first, initialize west from the local checkout instead:

```sh
west init -l esp-nmea-bridge
west update
```

If the workspace has already been initialized, this is usually enough:

```sh
source .venv/bin/activate
west update
west blobs fetch hal_espressif
```

## Configure Wi-Fi and TCP

The default configuration lives in `prj.conf`. Wi-Fi credentials and local IP/port changes should not be committed to `prj.conf`; put them in a local overlay such as `local.conf` instead:

```conf
CONFIG_ESP_NMEA_BRIDGE_AP_SSID="ESP-NMEA0183"
CONFIG_ESP_NMEA_BRIDGE_AP_PSK="ChangeMe1234"
CONFIG_ESP_NMEA_BRIDGE_STA_SSID="My-WiFi"
CONFIG_ESP_NMEA_BRIDGE_STA_PSK="MyPassword"
CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT=10110
```

`local.conf` is ignored by `.gitignore`.

Important options:

- `CONFIG_ESP_NMEA_BRIDGE_AP_ENABLE`: create a Wi-Fi network from the ESP32-C6. This is useful when clients should connect directly to the bridge.
- `CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE`: connect the ESP32-C6 to an existing Wi-Fi network.
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_ENABLE`: listen for inbound TCP clients such as navigation apps.
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_ENABLE`: forward NMEA data to another TCP server.
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST`: target IPv4 address for TCP client mode. Leave empty to use the DHCP gateway learned on the station interface.
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_PORT`: TCP port for NMEA data. The default is `10110`.
- `CONFIG_ESP_NMEA_BRIDGE_MDNS_ENABLE`: enable mDNS hostname resolution for `CONFIG_NET_HOSTNAME.local`; defaults to off.
- `CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE`: advertise the TCP NMEA server as `_nmea-0183._tcp.local`; depends on mDNS and the TCP NMEA server and defaults to off.
- `CONFIG_ESP_NMEA_BRIDGE_STATUS_LED_ENABLE`: enable the optional status LED observer; it uses a `led-strip` devicetree alias when present and can be disabled with `CONFIG_ESP_NMEA_BRIDGE_STATUS_LED_ENABLE=n`.
- `CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_ENABLE`: enable the optional AIS self-MMSI filter. Set `CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_MMSI` to your own MMSI; only checksum-valid AIS `VDM` NMEA frames confirmed to carry that MMSI are dropped.

The committed defaults keep LAN discovery disabled. For a local deployment, enable it in `local.conf`:

```conf
CONFIG_ESP_NMEA_BRIDGE_MDNS_ENABLE=y
CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE=y
```

The default hostname is `esp-nmea-bridge`, so mDNS resolves `esp-nmea-bridge.local` when enabled.

## Development container

This repository includes a VS Code Dev Container for a hybrid Zephyr workspace:

- the app repository is bind-mounted from the host, so normal host-side Git workflows keep working;
- the Zephyr workspace parent, `.west/`, `zephyr/`, and modules live in the persistent Docker volume `esp-nmea-bridge-west-workspace`; build artifacts go to `build/` inside the bind-mounted app repository and are therefore accessible from the host;
- the container image provides the Zephyr SDK, Python environment, `west`, CMake, Ninja, `clangd`, `ccache`, and serial helper tools.

Open `esp-nmea-bridge/` in VS Code and run **Dev Containers: Reopen in Container**. On first creation, `.devcontainer/setup-workspace.sh` initializes the workspace and fetches Zephyr projects and Espressif blobs. Later starts reuse the Docker volume and do not run `west update` automatically.

When `west.yml` changes, update the cached workspace manually:

```sh
bash .devcontainer/update-workspace.sh
```

If a rebuilt container reports missing Python tools such as `jsonschema` or `esptool`, rerun the setup script inside the container:

```sh
bash .devcontainer/setup-workspace.sh
```

The container runs as the `vscode` user with `updateRemoteUserUID` enabled, so files created in the bind-mounted app repository use the host user's UID/GID instead of becoming root-owned. Deleting the Docker volume removes the cached Zephyr checkout, modules, and `.west/`, but not the host-mounted app repository or its `build/` directory.

## Build

Run builds from the application directory `esp-nmea-bridge/`. The build directory is the west default `build/` inside the application directory, so build results stay in the bind-mounted repository and remain accessible from the host when building inside the Dev Container.

Build a plain application image with a local overlay:

```sh
cd esp-nmea-bridge
west build -p always \
  -b esp32c6_dev_kit_n8/esp32c6/hpcore \
  . \
  -- -DEXTRA_CONF_FILE=local.conf
```

Without a local overlay file:

```sh
cd esp-nmea-bridge
west build -p always \
  -b esp32c6_dev_kit_n8/esp32c6/hpcore \
  .
```

### Build with MCUboot

Use sysbuild when you want MCUboot, signed application images, or future OTA updates:

```sh
cd esp-nmea-bridge
west build -p always \
  -b esp32c6_dev_kit_n8/esp32c6/hpcore \
  . \
  --sysbuild
```

This builds both MCUboot and the application. The signed application image is generated at:

```text
build/esp-nmea-bridge/zephyr/zephyr.signed.bin
```

Plain non-MCUboot builds remain supported; omit `--sysbuild` when you do not need MCUboot or OTA-capable images.

## Flash and monitor

From the application directory (`west flash` finds `./build` automatically):

```sh
west flash
west espressif monitor
```

If the board definition's udev rules are installed, flash explicitly through the stable JTAG link:

```sh
sudo cp boards/waveshare/esp32c6_dev_kit_n8/support/99-waveshare-esp32c6-dev-kit-n8.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger -s tty

west flash --esp-device /dev/waveshare/esp32c6-dev-kit-n8/jtag
```

## Connect a navigation app

With the default SoftAP settings, connect your phone, tablet, or computer to:

- Wi-Fi SSID: `ESP-NMEA0183`
- Password: `ChangeMe1234`
- Bridge IP address: `192.168.4.1`
- TCP NMEA port: `10110`

In your navigation app, add a TCP NMEA-0183 data source pointing to `192.168.4.1:10110`.

From a computer on the same network, you can test the TCP stream with:

```sh
nc 192.168.4.1 10110
```

If station mode is configured instead, use the IP address assigned by your Wi-Fi network. When mDNS is enabled, you can also try `esp-nmea-bridge.local`.

## Optional: mDNS and DNS-SD discovery

When `CONFIG_ESP_NMEA_BRIDGE_MDNS_ENABLE=y`, resolve the device hostname from a Linux host on the same Wi-Fi network:

```sh
avahi-resolve -4 -n esp-nmea-bridge.local
ping esp-nmea-bridge.local
```

When `CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE=y` and the TCP NMEA server is enabled, browse the advertised NMEA-0183 TCP service:

```sh
avahi-browse -t -r _nmea-0183._tcp
```

If direct browsing does not show the ESP service, enumerate all services and filter for the bridge:

```sh
avahi-browse --all --resolve --terminate | grep -A8 -B2 esp-nmea
```

The service is intentionally not available in Kconfig when `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_ENABLE=n`, so the bridge does not advertise `_nmea-0183._tcp` without a listening TCP server.

The advertised service uses TCP port `10110` by default and includes TXT metadata for a read-only MAIANA GPS/AIS RX passthrough (`txtvers=1`, `talkers=GP,AI`, `content=gps,ais-rx`, `source=maiana`, `ro=1`).

## Development and tests

Run the status LED policy test with Twister from the application directory. `-c -O build/twister` keeps a single output directory under `build/` instead of accumulating numbered `twister-out.N` directories:

```sh
west twister -c -O build/twister -T tests/status_led_policy --inline-logs
```

## Further Zephyr documentation

- Getting Started: <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>
- Application Development: <https://docs.zephyrproject.org/latest/develop/application/index.html>
- West: <https://docs.zephyrproject.org/latest/develop/west/index.html>
- Networking: <https://docs.zephyrproject.org/latest/connectivity/networking/index.html>
