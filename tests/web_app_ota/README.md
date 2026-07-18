# WebApp OTA boundary harness

This directory contains Phase 4 OTA boundary coverage.

## Native HTTP/backend harness

Run through Zephyr twister:

```sh
devcontainer exec --workspace-folder . /bin/bash -lc 'cd /workspaces/esp-nmea-bridge-workspace/esp-nmea-bridge && west twister -c -O build/twister -T tests/web_app_ota --inline-logs'
```

The ztest harness compiles `src/web_app.c` with fake socket, telemetry, and OTA-domain APIs. It covers:

- successful upload sequencing: success JSON is sent before `ota_update_schedule_reboot()`;
- trusted-network gate rejection before flash APIs are called;
- missing and oversized `Content-Length` rejection before image writes;
- early disconnect, write failure, and finish failure abort paths;
- success-response failure after finish preserving the committed pending-reboot state and still scheduling the delayed reboot;
- OTA receive timeout installation before upload streaming to bound slow clients;
- additive `/api/status.ota.upload_allowed` and `max_upload_bytes` fields;
- deferred self-confirmation: only a successfully sent `/api/status` response calls the OTA self-check boundary, while non-status responses and failed status writes do not.

## Browser-state harness

Run with Node.js:

```sh
node tests/web_app_ota/browser_state_harness.mjs
```

The script documents and checks the browser-side OTA state rules that are awkward to exercise in native Zephyr: max-size preflight, disabled/enabled form state, trusted-network gate messaging, pending-reboot message wiring, and expected OTA reboot/offline messaging.
