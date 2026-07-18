#!/usr/bin/env node
// Mirrors the browser state rules in src/web_assets/app.js — keep logic and
// user-facing copy in sync with that file when either side changes.
import assert from 'node:assert/strict';

function selectedFileTooLarge(file, otaMaxUploadBytes) {
  return Boolean(file && otaMaxUploadBytes > 0 && file.size > otaMaxUploadBytes);
}

function controlsDisabled({ statusOffline, otaAvailable, otaUploadAllowed, otaUploadInProgress, otaRebootExpectedUntil, now }) {
  return Boolean(statusOffline) || !otaAvailable || !otaUploadAllowed || otaUploadInProgress || now < otaRebootExpectedUntil;
}

function offlineReason({ now, otaRebootExpectedUntil, configRebootExpectedUntil = 0, error }) {
  const expectedOtaReboot = now < otaRebootExpectedUntil;
  const expectedConfigReboot = now < configRebootExpectedUntil;
  return expectedOtaReboot
    ? 'OTA upload complete. The bridge is rebooting into the test image; this page reconnects automatically.'
    : (expectedConfigReboot
      ? 'The bridge is restarting to apply configuration changes; this page reconnects automatically.'
      : (error instanceof Error && error.message
        ? `No response from the bridge (${error.message}). Retrying every 2 s.`
        : 'No response from the bridge. Retrying every 2 s.'));
}

function staSsidError(ssidBytes) {
  return ssidBytes < 1 || ssidBytes > 32 ? 'Enter an SSID of 1 to 32 bytes.' : '';
}

function staPskError(pskLength) {
  return pskLength !== 0 && (pskLength < 8 || pskLength > 63)
    ? 'Enter a password of 8 to 63 characters, or leave blank to keep the stored one.' : '';
}

function staPskPlaceholder(staPskStored) {
  return staPskStored ? 'Leave blank to keep the stored password' : 'No password stored (open network)';
}

function staBody({ enabled, ssid, psk, pskClear, rotateMac }) {
  const body = { sta_enabled: enabled, sta_rotate_mac: rotateMac };
  if (ssid !== '' || enabled) body.sta_ssid = ssid;
  if (pskClear) body.sta_psk_clear = true;
  else if (psk !== '') body.sta_psk = psk;
  return body;
}

function rebootCleared({ rebootInProgress, rebootGraceUntil, statusOffline, now }) {
  // Mirrors clearOffline(): a successful poll clears reboot state once the
  // grace period passed, or after any observed offline phase.
  return statusOffline || (rebootInProgress && now > rebootGraceUntil);
}

function resolveTheme(stored, osPrefersDark) {
  return stored === 'light' || stored === 'dark' ? stored : (osPrefersDark ? 'dark' : 'light');
}

function otaMessage({ status, file, uploadInProgress }) {
  if (uploadInProgress) return 'Uploading firmware image. Keep this page open.';
  if (!status.enabled) return 'OTA updates are disabled in this firmware build.';
  if (!status.upload_allowed) return 'OTA is compiled in, but firmware uploads are blocked by the trusted-network gate.';
  if (selectedFileTooLarge(file, status.max_upload_bytes)) return `Selected file is ${file.size} B; maximum accepted size is ${status.max_upload_bytes} B.`;
  if (status.state === 'pending_reboot') return 'Update accepted. Waiting for the bridge to reboot into the test image.';
  if (status.state === 'error') return status.last_error || 'OTA update failed. Choose a new image and retry.';
  if (status.state === 'confirmed') return 'Running image is confirmed. Choose a signed binary to update.';
  return 'Choose a signed binary to upload to the inactive app slot.';
}

function versionBadge(firmwareVersion) {
  return firmwareVersion ? `v${firmwareVersion}` : '—';
}

function connectionDetail(status) {
  const clients = status.tcp_server ? status.tcp_server.active_peers : null;
  return clients == null ? '—' : `${clients} TCP client${clients === 1 ? '' : 's'}`;
}

function wifiDetail(wifi) {
  if (!wifi.sta_ready) return '—';
  const wifiIp = wifi.ip || '—';
  const wifiRssi = wifi.rssi == null ? '—' : `${String(wifi.rssi).replace('-', '−')} dBm`;
  return `${wifiIp} · ${wifiRssi}`;
}

const now = 1_000;
assert.equal(selectedFileTooLarge({ size: 1025 }, 1024), true);
assert.equal(selectedFileTooLarge({ size: 1024 }, 1024), false);
assert.equal(selectedFileTooLarge({ size: 5_000 }, 0), false);
assert.equal(controlsDisabled({ otaAvailable: true, otaUploadAllowed: true, otaUploadInProgress: false, otaRebootExpectedUntil: now + 120_000, now }), true);
assert.equal(controlsDisabled({ otaAvailable: true, otaUploadAllowed: false, otaUploadInProgress: false, otaRebootExpectedUntil: 0, now }), true);
assert.equal(controlsDisabled({ otaAvailable: true, otaUploadAllowed: true, otaUploadInProgress: false, otaRebootExpectedUntil: 0, now }), false);
assert.equal(controlsDisabled({ statusOffline: true, otaAvailable: true, otaUploadAllowed: true, otaUploadInProgress: false, otaRebootExpectedUntil: 0, now }), true);
assert.match(offlineReason({ now, otaRebootExpectedUntil: now + 120_000 }), /rebooting into the test image/);
assert.match(offlineReason({ now, otaRebootExpectedUntil: 0, error: new Error('HTTP 500') }), /HTTP 500/);
assert.doesNotMatch(offlineReason({ now, otaRebootExpectedUntil: 0 }), /\/api\//);
assert.doesNotMatch(offlineReason({ now, otaRebootExpectedUntil: now + 120_000 }), /\/api\//);
assert.match(offlineReason({ now, otaRebootExpectedUntil: 0, configRebootExpectedUntil: now + 60_000 }), /apply configuration changes/);
assert.match(offlineReason({ now, otaRebootExpectedUntil: now + 120_000, configRebootExpectedUntil: now + 60_000 }), /test image/);
assert.equal(staSsidError(0), 'Enter an SSID of 1 to 32 bytes.');
assert.equal(staSsidError(1), '');
assert.equal(staSsidError(32), '');
assert.equal(staSsidError(33), 'Enter an SSID of 1 to 32 bytes.');
assert.equal(staPskError(0), '');
assert.equal(staPskError(7), 'Enter a password of 8 to 63 characters, or leave blank to keep the stored one.');
assert.equal(staPskError(8), '');
assert.equal(staPskError(63), '');
assert.equal(staPskError(64), 'Enter a password of 8 to 63 characters, or leave blank to keep the stored one.');
assert.match(staPskPlaceholder(true), /keep the stored password/);
assert.match(staPskPlaceholder(false), /open network/);
assert.deepEqual(staBody({ enabled: false, ssid: '', psk: '', pskClear: false, rotateMac: true }),
  { sta_enabled: false, sta_rotate_mac: true });
assert.deepEqual(staBody({ enabled: true, ssid: 'Marina', psk: '', pskClear: false, rotateMac: false }),
  { sta_enabled: true, sta_rotate_mac: false, sta_ssid: 'Marina' });
assert.deepEqual(staBody({ enabled: true, ssid: 'Marina', psk: 'harbour99', pskClear: false, rotateMac: false }),
  { sta_enabled: true, sta_rotate_mac: false, sta_ssid: 'Marina', sta_psk: 'harbour99' });
assert.deepEqual(staBody({ enabled: true, ssid: 'Marina', psk: 'ignored99', pskClear: true, rotateMac: false }),
  { sta_enabled: true, sta_rotate_mac: false, sta_ssid: 'Marina', sta_psk_clear: true });
assert.equal('sta_ssid' in staBody({ enabled: true, ssid: '', psk: '', pskClear: false, rotateMac: true }), true);
assert.equal(rebootCleared({ rebootInProgress: true, rebootGraceUntil: now + 5_000, statusOffline: false, now }), false);
assert.equal(rebootCleared({ rebootInProgress: true, rebootGraceUntil: now - 1, statusOffline: false, now }), true);
assert.equal(rebootCleared({ rebootInProgress: false, rebootGraceUntil: 0, statusOffline: true, now }), true);
assert.equal(rebootCleared({ rebootInProgress: false, rebootGraceUntil: 0, statusOffline: false, now }), false);
assert.equal(resolveTheme(null, true), 'dark');
assert.equal(resolveTheme(null, false), 'light');
assert.equal(resolveTheme('light', true), 'light');
assert.equal(resolveTheme('dark', false), 'dark');
assert.equal(resolveTheme('junk', false), 'light');
assert.match(otaMessage({ status: { enabled: false }, file: null, uploadInProgress: false }), /disabled/);
assert.match(otaMessage({ status: { enabled: true, upload_allowed: false }, file: null, uploadInProgress: false }), /trusted-network gate/);
assert.match(otaMessage({ status: { enabled: true, upload_allowed: true, max_upload_bytes: 1024 }, file: { size: 1025 }, uploadInProgress: false }), /maximum accepted size/);
assert.match(otaMessage({ status: { enabled: true, upload_allowed: true, state: 'pending_reboot' }, file: null, uploadInProgress: false }), /reboot/);
assert.match(otaMessage({ status: { enabled: true, upload_allowed: true, state: 'confirmed' }, file: null, uploadInProgress: false }), /confirmed/);

assert.equal(versionBadge('0.5.0'), 'v0.5.0');
assert.equal(versionBadge(''), '—');
assert.equal(versionBadge(undefined), '—');
assert.equal(connectionDetail({ tcp_server: { active_peers: 1 } }), '1 TCP client');
assert.equal(connectionDetail({ tcp_server: { active_peers: 2 } }), '2 TCP clients');
assert.equal(connectionDetail({ tcp_server: { active_peers: 0 } }), '0 TCP clients');
assert.equal(connectionDetail({ tcp_server: {} }), '—');
assert.equal(connectionDetail({}), '—');
assert.equal(wifiDetail({ sta_ready: true, ip: '192.168.4.7', rssi: -58 }), '192.168.4.7 · −58 dBm');
assert.equal(wifiDetail({ sta_ready: true, ip: '10.0.0.2', rssi: 0 }), '10.0.0.2 · 0 dBm');
assert.equal(wifiDetail({ sta_ready: true, ip: null, rssi: null }), '— · —');
assert.equal(wifiDetail({ sta_ready: false, ip: '192.168.4.7', rssi: -58 }), '—');

console.log('browser OTA state harness passed');
