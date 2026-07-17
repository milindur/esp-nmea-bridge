const $ = (id) => document.getElementById(id);
const fmt = new Intl.NumberFormat('en-US');
const format = (v) => fmt.format(v ?? 0);
const metrics = ['frames', 'bytes', 'peers', 'drops', 'warn-count'];

let otaAvailable = false;
let otaUploadAllowed = false;
let otaUploadInProgress = false;
let otaRebootExpectedUntil = 0;
let otaMaxUploadBytes = 0;
let otaLastStatus = null;

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
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

function setOtaMessage(text, severity) {
  const el = $('ota-message');
  if (!el) return;
  el.textContent = text;
  el.classList.remove('is-ok', 'is-warn', 'is-bad');
  if (severity) el.classList.add(`is-${severity}`);
}

function setOtaProgress(percent) {
  const bar = $('ota-progress-bar');
  if (bar) bar.style.width = `${Math.max(0, Math.min(100, percent))}%`;
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
  const disabled = !otaAvailable || !otaUploadAllowed || otaUploadInProgress || Date.now() < otaRebootExpectedUntil;
  if (fileInput) fileInput.disabled = disabled;
  if (button) button.disabled = disabled || !file || selectedFileTooLarge(file);
}

function setOffline(error) {
  const expectedOtaReboot = Date.now() < otaRebootExpectedUntil;

  for (const id of ['connection-state', 'input-state', 'wifi-state']) {
    const el = $(id);
    if (!el) continue;
    el.classList.remove('state-ok', 'state-warn');
    el.classList.add('state-bad');
    el.querySelector('b').textContent = expectedOtaReboot ? 'REBOOTING' : 'OFFLINE';
  }

  for (const id of metrics) setText(id, '—');

  const reason = expectedOtaReboot
    ? 'OTA upload completed. The bridge is rebooting into the test image; polling will resume automatically.'
    : (error instanceof Error && error.message ? `Reason: ${error.message}` : 'No response from /api/status');
  const notice = $('offline-notice');
  if (notice) notice.hidden = false;
  setText('offline-reason', reason);
  setText('poll-label', expectedOtaReboot ? 'OTA reboot watch' : 'Offline watch');
  setText('updated', `${expectedOtaReboot ? 'Rebooting' : 'Offline'} ${new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'})}`);
  setText('app-health', expectedOtaReboot ? 'ESP NMEA Bridge · OTA reboot expected' : 'ESP NMEA Bridge · offline / API unreachable');

  $('details').innerHTML = expectedOtaReboot
    ? rowItem('Status API', 'temporarily unavailable during OTA reboot', 'warn') +
      rowItem('Next retry', 'in 2 s') +
      rowItem('Expected result', 'test image comes online and self-confirms', 'ok')
    : rowItem('Status API', 'unreachable', 'bad') +
      rowItem('Displayed counters', 'unavailable', 'warn') +
      rowItem('Next retry', 'in 2 s');

  updateOtaControls();
}

function clearOffline() {
  const notice = $('offline-notice');
  if (notice) notice.hidden = true;
  setText('poll-label', 'Watch in progress');
  setText('app-health', 'ESP NMEA Bridge · firmware live');
}

function rowItem(label, value, severity) {
  const cls = severity ? ` class="row is-${severity}"` : ' class="row"';
  return `<div${cls}><dt>${label}</dt><dd>${value}</dd></div>`;
}

function renderOta(ota) {
  const status = ota || { enabled: false, state: 'disabled', uploaded_bytes: 0, expected_bytes: 0, max_upload_bytes: 0, upload_allowed: false, slot: 0, confirmed: true, last_error: '' };
  const state = status.state || 'unknown';
  const stateSeverity = !status.enabled || state === 'error' ? 'bad' : (state === 'ready' || state === 'confirmed' ? 'ok' : 'warn');
  const badge = $('ota-state');
  const panel = $('ota');
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
  if (panel) {
    panel.classList.remove('is-ota-disabled', 'is-ota-gated', 'is-ota-uploading', 'is-ota-pending', 'is-ota-error', 'is-ota-confirmed');
    if (!status.enabled) panel.classList.add('is-ota-disabled');
    else if (!status.upload_allowed) panel.classList.add('is-ota-gated');
    else if (state === 'uploading') panel.classList.add('is-ota-uploading');
    else if (state === 'pending_reboot') panel.classList.add('is-ota-pending');
    else if (state === 'error') panel.classList.add('is-ota-error');
    else if (state === 'confirmed') panel.classList.add('is-ota-confirmed');
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

  setText('frames', format(status.bridge.frames_in));
  setText('bytes', format(status.uart.bytes_rx));
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

  setText('updated', `Updated ${new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'})}`);
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
  const fileInput = $('ota-file');
  const file = fileInput && fileInput.files ? fileInput.files[0] : null;

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
      setOtaMessage('Upload accepted. The bridge is rebooting; polling will resume automatically.', 'ok');
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

function wireOtaUi() {
  const form = $('ota-form');
  const fileInput = $('ota-file');

  if (form) form.addEventListener('submit', handleOtaSubmit);
  if (fileInput) {
    fileInput.addEventListener('change', () => {
      const file = fileInput.files && fileInput.files[0];
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

wireOtaUi();
pollStatus();
setInterval(pollStatus, 2000);
