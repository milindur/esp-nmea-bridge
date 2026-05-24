# ESP NMEA Bridge

ESP NMEA Bridge is a Zephyr application for an ESP32-C6 board. It reads NMEA-0183 data from UART, frames it as NMEA frames, and distributes it through the in-process NMEA bridge to TCP NMEA sessions. The application can run as a Wi-Fi SoftAP, Wi-Fi station, TCP NMEA server, and optional TCP NMEA client at the same time.

The project also includes a board definition for the Waveshare `esp32c6_dev_kit_n8` with 8 MiB flash and optional status LED support.

## Requirements

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

Example for a fresh workspace with this app as an out-of-tree application:

```sh
mkdir esp-nmea-bridge-zephyr
cd esp-nmea-bridge-zephyr

python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip west

west init
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west blobs fetch hal_espressif

mkdir -p apps
git clone git@github.com:milindur/esp-nmea-bridge-zephyr.git apps/esp-nmea-bridge
```

If the repository has already been checked out as a complete workspace, this is usually enough:

```sh
source .venv/bin/activate
west update
west blobs fetch hal_espressif
```

## Configure the app

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

- `CONFIG_ESP_NMEA_BRIDGE_AP_ENABLE`: enable SoftAP mode
- `CONFIG_ESP_NMEA_BRIDGE_STA_ENABLE`: enable station mode
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_SERVER_ENABLE`: enable the inbound TCP NMEA server
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_ENABLE`: enable the outbound TCP NMEA client
- `CONFIG_ESP_NMEA_BRIDGE_TCP_NMEA_CLIENT_HOST`: target IP address; leave empty to use the DHCP gateway learned on the station interface
- `CONFIG_ESP_NMEA_BRIDGE_STATUS_LED_ENABLE`: enable the optional status LED observer; it uses a `led-strip` devicetree alias when present and can be disabled with `CONFIG_ESP_NMEA_BRIDGE_STATUS_LED_ENABLE=n`

## Build

From the workspace root:

```sh
west build -p always \
  -d build-esp32c6 \
  -b esp32c6_dev_kit_n8/esp32c6/hpcore \
  apps/esp-nmea-bridge \
  -- -DEXTRA_CONF_FILE=local.conf
```

Without a local overlay file:

```sh
west build -p always \
  -d build-esp32c6 \
  -b esp32c6_dev_kit_n8/esp32c6/hpcore \
  apps/esp-nmea-bridge
```

## Flash and monitor

```sh
west flash -d build-esp32c6
west espressif monitor
```

If the board definition's udev rules are installed, flash explicitly through the stable JTAG link:

```sh
sudo cp apps/esp-nmea-bridge/boards/waveshare/esp32c6_dev_kit_n8/support/99-waveshare-esp32c6-dev-kit-n8.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger -s tty

west flash -d build-esp32c6 --esp-device /dev/waveshare/esp32c6-dev-kit-n8/jtag
```

## Tests

Run the status LED policy test with Twister:

```sh
west twister -T apps/esp-nmea-bridge/tests/status_led_policy --inline-logs
```

## Further Zephyr documentation

- Getting Started: <https://docs.zephyrproject.org/latest/develop/getting_started/index.html>
- Application Development: <https://docs.zephyrproject.org/latest/develop/application/index.html>
- West: <https://docs.zephyrproject.org/latest/develop/west/index.html>
- Networking: <https://docs.zephyrproject.org/latest/connectivity/networking/index.html>
