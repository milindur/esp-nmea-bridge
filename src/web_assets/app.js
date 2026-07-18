const $ = (id) => document.getElementById(id);
const fmt = new Intl.NumberFormat('en-US');
const format = (v) => fmt.format(v ?? 0);
const counters = ['frames', 'bytes', 'peers', 'drops', 'warn-count'];

let statusOffline = false;
let otaAvailable = false;
let otaUploadAllowed = false;
let otaUploadInProgress = false;
let otaRebootExpectedUntil = 0;
let otaMaxUploadBytes = 0;
let otaLastStatus = null;

function setText(id, text) {
  const el = $(id);
  if (el && el.textContent !== text) el.textContent = text;
}

function setState(id, state, okState) {
  const el = $(id);
  if (!el) return;
  const value = state || 'unknown';
  const isOk = value === okState;
  el.classList.remove('state-ok', 'state-warn', 'state-bad');
  el.classList.add(isOk ? 'state-ok' : 'state-warn');
  el.querySelector('b').textContent = value.toUpperCase();
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${format(bytes)} B`;
  if (bytes < 1024 * 1024) return `${format(Math.round(bytes / 102.4) / 10)} KiB`;
  return `${format(Math.round(bytes / 104857.6) / 10)} MiB`;
}

/* Theme */

const themeColors = { light: '#eef0f3', dark: '#14171a' };
const osDark = window.matchMedia('(prefers-color-scheme: dark)');

function effectiveTheme() {
  const manual = document.documentElement.dataset.theme;
  return manual === 'light' || manual === 'dark' ? manual : (osDark.matches ? 'dark' : 'light');
}

function syncThemeUi() {
  const theme = effectiveTheme();
  const meta = document.querySelector('meta[name="theme-color"]');
  if (meta) meta.content = themeColors[theme];
  const toggle = $('theme-toggle');
  if (toggle) toggle.setAttribute('aria-label', `Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`);
}

function wireTheme() {
  const toggle = $('theme-toggle');
  if (toggle) {
    toggle.addEventListener('click', () => {
      const next = effectiveTheme() === 'dark' ? 'light' : 'dark';
      document.documentElement.dataset.theme = next;
      try { localStorage.setItem('theme', next); } catch (error) { /* private mode */ }
      syncThemeUi();
    });
  }
  if (osDark.addEventListener) osDark.addEventListener('change', syncThemeUi);
  else if (osDark.addListener) osDark.addListener(syncThemeUi);
  syncThemeUi();
}

/* Tabs */

function switchView(name) {
  for (const tab of document.querySelectorAll('.tab[data-view]')) {
    const active = tab.dataset.view === name;
    tab.classList.toggle('is-active', active);
    tab.setAttribute('aria-selected', String(active));
    tab.tabIndex = active ? 0 : -1;
    const view = $(`view-${tab.dataset.view}`);
    if (view) view.hidden = !active;
  }
}

function wireTabs() {
  const tabs = Array.from(document.querySelectorAll('.tab[data-view]'));
  for (const tab of tabs) tab.addEventListener('click', () => switchView(tab.dataset.view));

  const list = document.querySelector('.tabs');
  if (list) {
    list.addEventListener('keydown', (event) => {
      const index = tabs.indexOf(document.activeElement);
      if (index < 0) return;
      let next;
      if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
      else if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
      else if (event.key === 'Home') next = 0;
      else if (event.key === 'End') next = tabs.length - 1;
      else return;
      event.preventDefault();
      tabs[next].focus();
      switchView(tabs[next].dataset.view);
    });
  }
}

/* OTA */

function setOtaMessage(text, severity) {
  const el = $('ota-message');
  if (!el) return;
  if (el.textContent !== text) el.textContent = text;
  el.classList.remove('is-ok', 'is-warn', 'is-bad');
  if (severity) el.classList.add(`is-${severity}`);
}

function setOtaProgress(percent) {
  const bar = $('ota-progress-bar');
  if (!bar) return;
  const clamped = Math.max(0, Math.min(100, percent));
  bar.style.width = `${clamped}%`;
  bar.parentElement.setAttribute('aria-valuenow', String(Math.round(clamped)));
}

function selectedFile() {
  const fileInput = $('ota-file');
  return fileInput && fileInput.files ? fileInput.files[0] : null;
}

function selectedFileTooLarge(file) {
  return Boolean(file && otaMaxUploadBytes > 0 && file.size > otaMaxUploadBytes);
}

function updateOtaControls() {
  const fileInput = $('ota-file');
  const button = $('ota-upload-button');
  const file = selectedFile();
  const disabled = statusOffline || !otaAvailable || !otaUploadAllowed || otaUploadInProgress || Date.now() < otaRebootExpectedUntil;
  if (fileInput) fileInput.disabled = disabled;
  if (button) button.disabled = disabled || !file || selectedFileTooLarge(file);
}

function rowItem(label, value, severity) {
  const cls = severity ? ` class="row is-${severity}"` : ' class="row"';
  return `<div${cls}><dt>${label}</dt><dd>${value}</dd></div>`;
}

function setOffline(error) {
  const expectedOtaReboot = Date.now() < otaRebootExpectedUntil;
  statusOffline = true;

  for (const id of ['connection-state', 'input-state', 'wifi-state']) {
    const el = $(id);
    if (!el) continue;
    el.classList.remove('state-ok', 'state-warn');
    el.classList.add('state-bad');
    el.querySelector('b').textContent = expectedOtaReboot ? 'REBOOTING' : 'OFFLINE';
  }

  setText('connection-detail', '—');
  setText('wifi-detail', '—');
  for (const id of counters) setText(id, '—');

  const notice = $('offline-notice');
  if (notice) notice.hidden = false;
  setText('offline-title', expectedOtaReboot ? 'Rebooting for firmware update' : 'Bridge unreachable');
  setText('offline-reason', expectedOtaReboot
    ? 'OTA upload complete. The bridge is rebooting into the test image; this page reconnects automatically.'
    : (error instanceof Error && error.message
      ? `No response from the bridge (${error.message}). Retrying every 2 s.`
      : 'No response from the bridge. Retrying every 2 s.'));
  setText('updated', expectedOtaReboot ? 'Rebooting · retrying every 2 s' : 'Offline · retrying every 2 s');

  $('details').innerHTML = expectedOtaReboot
    ? rowItem('Status updates', 'paused during reboot', 'warn') +
      rowItem('Next retry', 'in 2 s') +
      rowItem('Expected result', 'test image comes online and self-confirms', 'ok')
    : rowItem('Status updates', 'unreachable', 'bad') +
      rowItem('Counters', 'unavailable', 'warn') +
      rowItem('Next retry', 'in 2 s');

  updateOtaControls();
}

function clearOffline() {
  statusOffline = false;
  const notice = $('offline-notice');
  if (notice) notice.hidden = true;
}

function renderOta(ota) {
  const status = ota || { enabled: false, state: 'disabled', uploaded_bytes: 0, expected_bytes: 0, max_upload_bytes: 0, upload_allowed: false, slot: 0, confirmed: true, last_error: '' };
  const state = status.state || 'unknown';
  const stateSeverity = !status.enabled || state === 'error' ? 'bad' : (state === 'ready' || state === 'confirmed' ? 'ok' : 'warn');
  const badge = $('ota-state');
  const file = selectedFile();

  otaLastStatus = status;
  otaMaxUploadBytes = Number(status.max_upload_bytes || 0);
  otaUploadAllowed = Boolean(status.upload_allowed);
  otaAvailable = Boolean(status.enabled) && state !== 'uploading' && state !== 'pending_reboot';
  if (state !== 'pending_reboot') otaRebootExpectedUntil = 0;

  if (badge) {
    badge.textContent = status.enabled ? state.replace(/_/g, ' ') : 'disabled';
    badge.classList.remove('state-ok', 'state-warn', 'state-bad');
    badge.classList.add(`state-${stateSeverity}`);
  }

  setText('ota-enabled', status.enabled ? 'enabled' : 'disabled');
  setText('ota-upload-gate', status.enabled ? (status.upload_allowed ? 'allowed' : 'blocked by trusted-network policy') : 'not built in');
  setText('ota-bytes', `${formatBytes(status.uploaded_bytes)} / ${formatBytes(status.expected_bytes)}`);
  setText('ota-max-bytes', otaMaxUploadBytes > 0 ? formatBytes(otaMaxUploadBytes) : 'slot limit unknown');
  setText('ota-slot', status.enabled ? String(status.slot) : 'not built in');
  setText('ota-confirmed', status.confirmed ? 'yes' : 'no');
  setText('ota-error', status.last_error || 'none');

  if (!otaUploadInProgress) {
    if (!status.enabled) {
      setOtaMessage('OTA updates are disabled in this firmware build.', 'warn');
      setOtaProgress(0);
    } else if (!status.upload_allowed) {
      setOtaMessage('OTA is compiled in, but firmware uploads are blocked by the trusted-network gate.', 'warn');
      setOtaProgress(0);
    } else if (selectedFileTooLarge(file)) {
      setOtaMessage(`Selected file is ${formatBytes(file.size)}; maximum accepted size is ${formatBytes(otaMaxUploadBytes)}.`, 'bad');
      setOtaProgress(0);
    } else if (state === 'pending_reboot') {
      otaRebootExpectedUntil = Math.max(otaRebootExpectedUntil, Date.now() + 120000);
      setOtaMessage('Update accepted. Waiting for the bridge to reboot into the test image.', 'warn');
      setOtaProgress(100);
    } else if (state === 'error') {
      setOtaMessage(status.last_error || 'OTA update failed. Choose a new image and retry.', 'bad');
    } else if (state === 'confirmed') {
      setOtaMessage('Running image is confirmed. Choose a signed binary to update.', 'ok');
      setOtaProgress(0);
    } else {
      setOtaMessage('Choose a signed binary to upload to the inactive app slot.', 'ok');
      setOtaProgress(0);
    }
  }

  updateOtaControls();
}

function render(status) {
  clearOffline();
  renderOta(status.ota);
  setState('connection-state', status.connection_state, 'connected');
  setState('input-state', status.input_state, 'active');
  setState('wifi-state', status.wifi.sta_ready ? 'linked' : 'idle', 'linked');

  const sessions = status.tcp.active_sessions ?? 0;
  setText('connection-detail', `${format(sessions)} TCP session${sessions === 1 ? '' : 's'}`);
  setText('wifi-detail', status.wifi.sta_ready ? 'IPv4 address assigned' : 'Waiting for IPv4 address');

  setText('frames', format(status.bridge.frames_in));
  setText('bytes', formatBytes(status.uart.bytes_rx));
  setText('peers', `${format(status.tcp_server.active_peers)} / ${format(status.tcp_server.max_peers)}`);
  setText('drops', format(status.bridge.sink_dropped_oldest));

  const warnCount = (status.warnings.data_quality ? 1 : 0) + (status.warnings.frame_loss ? 1 : 0);
  setText('warn-count', String(warnCount));

  $('details').innerHTML =
    rowItem('Active TCP NMEA sessions', format(status.tcp.active_sessions), status.tcp.active_sessions > 0 ? 'ok' : null) +
    rowItem('Inbound TCP peers', `${format(status.tcp_server.active_peers)} / ${format(status.tcp_server.max_peers)}`) +
    rowItem('UART frames received', format(status.uart.frames_rx)) +
    rowItem('AIS self-MMSI filtered', format(status.uart.ais_self_mmsi_filtered), status.uart.ais_self_mmsi_filtered > 0 ? 'ok' : null) +
    rowItem('Overlong UART frames', format(status.uart.overlong_frames), status.uart.overlong_frames > 0 ? 'warn' : null) +
    rowItem('Ingest dropped (oldest)', format(status.bridge.ingest_dropped_oldest), status.bridge.ingest_dropped_oldest > 0 ? 'warn' : null) +
    rowItem('No-sink publishes', format(status.bridge.publish_no_sinks)) +
    rowItem('Invalid / oversize', `${format(status.bridge.publish_invalid)} · ${format(status.bridge.publish_oversize)}`) +
    rowItem('STA IPv4 ready', status.wifi.sta_ready ? 'yes' : 'no', status.wifi.sta_ready ? 'ok' : null) +
    rowItem('OTA status', status.ota ? (status.ota.enabled ? status.ota.state : 'disabled') : 'not reported', status.ota && status.ota.enabled ? 'ok' : 'warn') +
    rowItem('Data quality warning', status.warnings.data_quality ? 'yes' : 'no', status.warnings.data_quality ? 'bad' : 'ok') +
    rowItem('Frame loss warning', status.warnings.frame_loss ? 'yes' : 'no', status.warnings.frame_loss ? 'bad' : 'ok');

  setText('updated', `Updated ${new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'})} · every 2 s`);
}

async function pollStatus() {
  try {
    const r = await fetch('/api/status', { cache: 'no-store' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    render(await r.json());
  } catch (error) {
    setOffline(error);
  }
}

function uploadFirmware(file) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/upload');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (event) => {
      if (!event.lengthComputable) return;
      const percent = Math.round((event.loaded / event.total) * 100);
      setOtaProgress(percent);
      setOtaMessage(`Uploading ${formatBytes(event.loaded)} / ${formatBytes(event.total)}. Keep this page open.`, 'warn');
    };
    xhr.onload = () => {
      let payload = null;
      if (xhr.responseText) {
        try {
          payload = JSON.parse(xhr.responseText);
        } catch (error) {
          reject(new Error('upload response was not valid JSON'));
          return;
        }
      }
      if (xhr.status >= 200 && xhr.status < 300 && payload && payload.ok) {
        resolve(payload);
      } else {
        const message = payload && payload.error ? payload.error : (xhr.responseText || 'upload failed');
        reject(new Error(`HTTP ${xhr.status}: ${message}`));
      }
    };
    xhr.onerror = () => reject(new Error('network error during upload'));
    xhr.onabort = () => reject(new Error('upload aborted'));
    xhr.send(file);
  });
}

async function handleOtaSubmit(event) {
  event.preventDefault();
  const file = selectedFile();

  if (!file) {
    setOtaMessage('Choose a firmware binary before uploading.', 'warn');
    return;
  }
  if (!otaUploadAllowed) {
    setOtaMessage('Firmware uploads are blocked by the trusted-network gate.', 'warn');
    updateOtaControls();
    return;
  }
  if (selectedFileTooLarge(file)) {
    setOtaMessage(`Selected file is ${formatBytes(file.size)}; maximum accepted size is ${formatBytes(otaMaxUploadBytes)}.`, 'bad');
    updateOtaControls();
    return;
  }

  otaUploadInProgress = true;
  setOtaProgress(0);
  setOtaMessage(`Starting upload of ${file.name} (${formatBytes(file.size)}).`, 'warn');
  updateOtaControls();

  try {
    const result = await uploadFirmware(file);
    if (result.state === 'pending_reboot') {
      otaRebootExpectedUntil = Date.now() + 120000;
      renderOta({ ...(otaLastStatus || {}), enabled: true, upload_allowed: true, state: 'pending_reboot', uploaded_bytes: file.size, expected_bytes: file.size });
      setOtaProgress(100);
      setOtaMessage('Upload accepted. The bridge is rebooting; this page reconnects automatically.', 'ok');
    } else {
      setOtaMessage(`Upload accepted with state ${result.state || 'unknown'}. Waiting for status refresh.`, 'ok');
    }
    otaUploadInProgress = false;
    await pollStatus();
  } catch (error) {
    otaUploadInProgress = false;
    setOtaMessage(error instanceof Error ? error.message : 'OTA upload failed.', 'bad');
    updateOtaControls();
  }
}

function wireDropZone(fileInput) {
  const zone = document.querySelector('.file-picker');
  if (!zone) return;
  for (const type of ['dragover', 'drop']) {
    document.addEventListener(type, (event) => event.preventDefault());
  }
  zone.addEventListener('dragover', () => zone.classList.add('is-dragover'));
  zone.addEventListener('dragleave', () => zone.classList.remove('is-dragover'));
  zone.addEventListener('drop', (event) => {
    zone.classList.remove('is-dragover');
    if (fileInput.disabled || !event.dataTransfer || !event.dataTransfer.files.length) return;
    fileInput.files = event.dataTransfer.files;
    fileInput.dispatchEvent(new Event('change'));
  });
}

function wireOtaUi() {
  const form = $('ota-form');
  const fileInput = $('ota-file');

  if (form) form.addEventListener('submit', handleOtaSubmit);
  if (fileInput) {
    wireDropZone(fileInput);
    fileInput.addEventListener('change', () => {
      const file = selectedFile();
      setText('ota-file-name', file ? `${file.name} · ${formatBytes(file.size)}` : 'Choose firmware binary');
      if (file && selectedFileTooLarge(file)) {
        setOtaMessage(`Selected file is ${formatBytes(file.size)}; maximum accepted size is ${formatBytes(otaMaxUploadBytes)}.`, 'bad');
      } else if (otaLastStatus) {
        renderOta(otaLastStatus);
        return;
      }
      updateOtaControls();
    });
  }
}

wireTheme();
wireTabs();
wireOtaUi();
pollStatus();
setInterval(pollStatus, 2000);
