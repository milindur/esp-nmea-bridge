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

function offlineReason({ now, otaRebootExpectedUntil, error }) {
  const expectedOtaReboot = now < otaRebootExpectedUntil;
  return expectedOtaReboot
    ? 'OTA upload complete. The bridge is rebooting into the test image; this page reconnects automatically.'
    : (error instanceof Error && error.message
      ? `No response from the bridge (${error.message}). Retrying every 2 s.`
      : 'No response from the bridge. Retrying every 2 s.');
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
