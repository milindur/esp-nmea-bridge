# Runtime configuration via Zephyr Settings/NVS with Kconfig as default layer

Configuration was fully static via Kconfig. To make selected options (WiFi STA/AP, TCP NMEA client, AIS self-MMSI filter, hostname) editable from the web app, we persist runtime overrides through the Zephyr Settings subsystem with the NVS backend on the `storage_partition` already provided by the Espressif 8M partition table. Precedence is: Kconfig value is the default, a stored value overrides it; factory reset deletes stored values and falls back to Kconfig. All runtime-configurable features are always compiled in — their former Kconfig enables become runtime defaults, so every image exposes the same configuration surface.

## Considered Options

- **Flash as single source of truth** (first boot seeds factory defaults): cleaner single-source model, but loses the `local.conf` development workflow and makes flashing behaviour depend on stored state. Rejected.
- **Raw NVS / FCB without Settings**: fewer layers, but reimplements key management the Settings subsystem already provides. Rejected.

## Consequences

- The stored key/value format in flash is a compatibility surface across firmware updates; renaming keys needs migration thought.
- Each configuration option declares an apply scope: *live* (AIS filter, TCP NMEA client) or *reboot-required* (WiFi STA/AP, MAC randomisation, hostname). No live-apply paths exist for reboot-scope subsystems by design.
- The AP is deliberately not runtime-disableable: with the AP as rescue anchor, a wrong STA configuration can never lock the user out. AP enable stays Kconfig-only.
- WiFi PSKs are write-only through the API (`*_psk_set` flags instead of values) because the web app runs over plain HTTP on a network declared trusted.
