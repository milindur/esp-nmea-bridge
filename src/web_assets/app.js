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

/* Configuration */

let configLoaded = false;
let configSaving = false;
let rebootInProgress = false;
let configRebootExpectedUntil = 0;
let rebootGraceUntil = 0;
let staPskStored = false;
let apPskStored = false;

function rebootStateCleared() {
  configRebootExpectedUntil = 0;
  rebootGraceUntil = 0;
  rebootInProgress = false;
}

function setConfigMessage(text, severity) { setFormMessage('ais-config-message', text, severity); }
function setStaMessage(text, severity) { setFormMessage('sta-config-message', text, severity); }
function setTcpMessage(text, severity) { setFormMessage('tcp-config-message', text, severity); }
function setApMessage(text, severity) { setFormMessage('ap-config-message', text, severity); }
function setSystemMessage(text, severity) { setFormMessage('system-config-message', text, severity); }

function setFormMessage(id, text, severity) {
  const el = $(id);
  if (!el) return;
  el.textContent = text;
  el.classList.remove('is-ok', 'is-warn', 'is-bad');
  if (severity) el.classList.add(`is-${severity}`);
}

function setFieldError(id, text) {
  const el = $(id);
  if (!el) return;
  el.textContent = text || '';
  el.hidden = !text;
}

function setConfigFieldError(text) { setFieldError('ais-mmsi-error', text); }

function updateConfigControls() {
  const disabled = !configLoaded || configSaving || rebootInProgress;
  for (const id of ['ais-enabled', 'ais-mmsi', 'sta-enabled', 'sta-ssid', 'sta-rotate-mac', 'sta-psk-clear',
                    'tcp-enabled', 'tcp-host', 'tcp-port', 'ap-ssid', 'ap-psk-clear', 'system-hostname']) {
    const el = $(id);
    if (el) el.disabled = disabled;
  }
  const psk = $('sta-psk');
  if (psk) psk.disabled = disabled || $('sta-psk-clear').checked;
  const apPsk = $('ap-psk');
  if (apPsk) apPsk.disabled = disabled || $('ap-psk-clear').checked;
  for (const id of ['ais-save', 'sta-save', 'tcp-save', 'ap-save', 'system-save',
                    'factory-reset-open', 'factory-reset-do', 'factory-reset-cancel']) {
    const el = $(id);
    if (el) el.disabled = disabled;
  }
  const reboot = $('reboot-button');
  if (reboot) reboot.disabled = rebootInProgress;
}

function renderRebootRequired(required) {
  const banner = $('reboot-banner');
  if (banner) banner.hidden = !required;
  for (const id of ['sta-reboot-badge', 'ap-reboot-badge', 'system-reboot-badge']) {
    const badge = $(id);
    if (badge) badge.hidden = !required;
  }
}

function renderAisConfig(cfg) {
  $('ais-enabled').checked = Boolean(cfg.ais_filter_enabled);
  $('ais-mmsi').value = String(cfg.ais_own_mmsi ?? 0);
}

/* Latest stored enable states from API responses, so the soft STA hint
 * reflects the device's persisted configuration — never unsaved edits. */
let staEnabledState = false;
let tcpEnabledState = false;

function renderTcpStaHint() {
  const hint = $('tcp-sta-hint');
  if (hint) hint.hidden = !(tcpEnabledState && !staEnabledState);
}

function renderTcpConfig(cfg) {
  tcpEnabledState = Boolean(cfg.tcp_client_enabled);
  $('tcp-enabled').checked = tcpEnabledState;
  $('tcp-host').value = String(cfg.tcp_client_host ?? '');
  $('tcp-port').value = String(cfg.tcp_client_port ?? '');
  renderTcpStaHint();
}

function renderStaConfig(cfg) {
  staEnabledState = Boolean(cfg.sta_enabled);
  renderTcpStaHint();
  $('sta-enabled').checked = Boolean(cfg.sta_enabled);
  $('sta-ssid').value = String(cfg.sta_ssid ?? '');
  $('sta-rotate-mac').checked = Boolean(cfg.sta_rotate_mac);
  staPskStored = Boolean(cfg.sta_psk_set);
  const psk = $('sta-psk');
  psk.value = '';
  psk.placeholder = staPskStored ? 'Leave blank to keep the stored password' : 'No password stored (open network)';
  const clear = $('sta-psk-clear');
  clear.checked = false;
  $('sta-psk-clear-row').hidden = !staPskStored;
}

function renderApConfig(cfg) {
  $('ap-ssid').value = String(cfg.ap_ssid ?? '');
  apPskStored = Boolean(cfg.ap_psk_set);
  const psk = $('ap-psk');
  psk.value = '';
  psk.placeholder = apPskStored ? 'Leave blank to keep the stored password' : 'No password stored (open access point)';
  const clear = $('ap-psk-clear');
  clear.checked = false;
  $('ap-psk-clear-row').hidden = !apPskStored;
}

function renderSystemConfig(cfg) {
  $('system-hostname').value = String(cfg.hostname ?? '');
}

/* Renders everything; per-card saves render only their own card so an
 * unsaved edit in the other card is never clobbered. */
function renderConfig(cfg) {
  renderAisConfig(cfg);
  renderStaConfig(cfg);
  renderApConfig(cfg);
  renderSystemConfig(cfg);
  renderTcpConfig(cfg);
  renderRebootRequired(Boolean(cfg.reboot_required));
}

async function loadConfig() {
  try {
    const r = await fetch('/api/config', { cache: 'no-store' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    renderConfig(await r.json());
    configLoaded = true;
    setConfigFieldError('');
    setFieldError('sta-ssid-error', '');
    setFieldError('sta-psk-error', '');
    setFieldError('ap-ssid-error', '');
    setFieldError('ap-psk-error', '');
    setFieldError('system-hostname-error', '');
    setFieldError('tcp-host-error', '');
    setFieldError('tcp-port-error', '');
    setConfigMessage('Changes apply immediately after saving.');
    setStaMessage('Changes take effect after the next reboot.');
    setApMessage('Changes take effect after the next reboot.');
    setSystemMessage('Changes take effect after the next reboot.');
    setTcpMessage('Changes apply immediately after saving.');
  } catch (error) {
    setConfigMessage('Loading configuration failed. Retrying in 5 s.', 'bad');
    setStaMessage('Loading configuration failed. Retrying in 5 s.', 'bad');
    setApMessage('Loading configuration failed. Retrying in 5 s.', 'bad');
    setSystemMessage('Loading configuration failed. Retrying in 5 s.', 'bad');
    setTcpMessage('Loading configuration failed. Retrying in 5 s.', 'bad');
    setTimeout(loadConfig, 5000);
  }
  updateConfigControls();
}

async function postConfig(body, onFieldErrors, renderCard) {
  const r = await fetch('/api/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const payload = await r.json().catch(() => null);
  if (!r.ok) {
    const errors = payload && payload.errors ? payload.errors : {};
    onFieldErrors(errors);
    const first = Object.values(errors)[0];
    throw new Error(first || `HTTP ${r.status}`);
  }
  renderCard(payload || {});
  renderRebootRequired(Boolean((payload || {}).reboot_required));
  return payload || {};
}

async function handleConfigSubmit(event) {
  event.preventDefault();
  const mmsiText = $('ais-mmsi').value.trim();

  setConfigFieldError('');
  if (!/^\d{1,9}$/.test(mmsiText)) {
    setConfigFieldError('Enter an MMSI between 0 and 999999999.');
    setConfigMessage('Not saved.', 'bad');
    return;
  }

  configSaving = true;
  updateConfigControls();
  setConfigMessage('Saving…', 'warn');

  try {
    await postConfig({
      ais_filter_enabled: $('ais-enabled').checked,
      ais_own_mmsi: Number(mmsiText),
    }, (errors) => {
      if (errors.ais_own_mmsi) setConfigFieldError(`Own MMSI ${errors.ais_own_mmsi}.`);
    }, renderAisConfig);
    setConfigMessage('Saved. The filter now uses the new settings.', 'ok');
  } catch (error) {
    setConfigMessage(`Saving failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

function staSsidError(ssidBytes) {
  return ssidBytes < 1 || ssidBytes > 32 ? 'Enter an SSID of 1 to 32 bytes.' : '';
}

function staPskError(pskLength) {
  return pskLength !== 0 && (pskLength < 8 || pskLength > 63)
    ? 'Enter a password of 8 to 63 characters, or leave blank to keep the stored one.' : '';
}

function staBody({ enabled, ssid, psk, pskClear, rotateMac }) {
  const body = { sta_enabled: enabled, sta_rotate_mac: rotateMac };
  // An empty SSID field while disabling keeps the stored SSID, so a
  // factory-default device can still be saved (e.g. to turn STA off).
  if (ssid !== '' || enabled) body.sta_ssid = ssid;
  if (pskClear) body.sta_psk_clear = true;
  else if (psk !== '') body.sta_psk = psk;
  return body;
}

async function handleStaSubmit(event) {
  event.preventDefault();
  const enabled = $('sta-enabled').checked;
  const ssid = $('sta-ssid').value;
  const pskClear = $('sta-psk-clear').checked;
  const psk = pskClear ? '' : $('sta-psk').value;
  const body = staBody({ enabled, ssid, psk, pskClear, rotateMac: $('sta-rotate-mac').checked });
  const ssidError = 'sta_ssid' in body ? staSsidError(new TextEncoder().encode(ssid).length) : '';
  const pskError = staPskError(new TextEncoder().encode(psk).length);

  setFieldError('sta-ssid-error', ssidError);
  setFieldError('sta-psk-error', pskError);
  if (ssidError || pskError) {
    setStaMessage('Not saved.', 'bad');
    return;
  }

  configSaving = true;
  updateConfigControls();
  setStaMessage('Saving…', 'warn');

  try {
    const payload = await postConfig(body, (errors) => {
      if (errors.sta_ssid) setFieldError('sta-ssid-error', `SSID ${errors.sta_ssid}.`);
      if (errors.sta_psk) setFieldError('sta-psk-error', `Password ${errors.sta_psk}.`);
      if (errors.sta_psk_clear) setFieldError('sta-psk-error', `Password ${errors.sta_psk_clear}.`);
    }, renderStaConfig);
    setStaMessage(payload.reboot_required
      ? 'Saved. Reboot the bridge to apply the new Wi-Fi settings.'
      : 'Saved.', 'ok');
  } catch (error) {
    setStaMessage(`Saving failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

async function handleApSubmit(event) {
  event.preventDefault();
  const ssid = $('ap-ssid').value;
  const pskClear = $('ap-psk-clear').checked;
  const psk = pskClear ? '' : $('ap-psk').value;
  const ssidBytes = new TextEncoder().encode(ssid).length;
  const ssidError = ssidBytes < 1 || ssidBytes > 32 ? 'Enter an SSID of 1 to 32 bytes.' : '';
  const pskLength = new TextEncoder().encode(psk).length;
  const pskError = pskLength !== 0 && (pskLength < 8 || pskLength > 63)
    ? 'Enter a password of 8 to 63 characters, or leave blank.' : '';

  setFieldError('ap-ssid-error', ssidError);
  setFieldError('ap-psk-error', pskError);
  if (ssidError || pskError) {
    setApMessage('Not saved.', 'bad');
    return;
  }

  const body = { ap_ssid: ssid };
  if (pskClear) body.ap_psk_clear = true;
  else if (psk !== '') body.ap_psk = psk;

  configSaving = true;
  updateConfigControls();
  setApMessage('Saving…', 'warn');

  try {
    const payload = await postConfig(body, (errors) => {
      if (errors.ap_ssid) setFieldError('ap-ssid-error', `SSID ${errors.ap_ssid}.`);
      if (errors.ap_psk) setFieldError('ap-psk-error', `Password ${errors.ap_psk}.`);
      if (errors.ap_psk_clear) setFieldError('ap-psk-error', `Password ${errors.ap_psk_clear}.`);
    }, renderApConfig);
    setApMessage(payload.reboot_required
      ? 'Saved. Reboot the bridge to apply the new access point settings.'
      : 'Saved.', 'ok');
  } catch (error) {
    setApMessage(`Saving failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

function hostnameError(hostname) {
  return /^[a-z0-9](?:[a-z0-9-]{0,30}[a-z0-9])?$/.test(hostname)
    ? '' : 'Enter 1 to 32 lowercase letters, digits, or hyphens; no hyphen at the start or end.';
}

async function handleSystemSubmit(event) {
  event.preventDefault();
  const hostname = $('system-hostname').value.trim();
  const error = hostnameError(hostname);

  setFieldError('system-hostname-error', error);
  if (error) {
    setSystemMessage('Not saved.', 'bad');
    return;
  }

  configSaving = true;
  updateConfigControls();
  setSystemMessage('Saving…', 'warn');

  try {
    const payload = await postConfig({ hostname }, (errors) => {
      if (errors.hostname) setFieldError('system-hostname-error', `Hostname ${errors.hostname}.`);
    }, renderSystemConfig);
    setSystemMessage(payload.reboot_required
      ? 'Saved. Reboot the bridge to apply the new hostname.'
      : 'Saved.', 'ok');
  } catch (error) {
    setSystemMessage(`Saving failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

function tcpHostError(host) {
  if (host === '') return '';
  const octets = host.split('.');
  // No leading zeros: matches the device-side parser.
  if (octets.length !== 4 || octets.some((o) => !/^(0|[1-9]\d{0,2})$/.test(o) || Number(o) > 255)) {
    return 'Enter an IPv4 address, or leave blank to use the Wi-Fi gateway.';
  }
  return '';
}

async function handleTcpSubmit(event) {
  event.preventDefault();
  const host = $('tcp-host').value.trim();
  const portText = $('tcp-port').value.trim();
  const hostError = tcpHostError(host);
  const portError = /^\d{1,5}$/.test(portText) && Number(portText) >= 1 && Number(portText) <= 65535
    ? '' : 'Enter a port between 1 and 65535.';

  setFieldError('tcp-host-error', hostError);
  setFieldError('tcp-port-error', portError);
  if (hostError || portError) {
    setTcpMessage('Not saved.', 'bad');
    return;
  }

  configSaving = true;
  updateConfigControls();
  setTcpMessage('Saving…', 'warn');

  try {
    await postConfig({
      tcp_client_enabled: $('tcp-enabled').checked,
      tcp_client_host: host,
      tcp_client_port: Number(portText),
    }, (errors) => {
      if (errors.tcp_client_host) setFieldError('tcp-host-error', `Address ${errors.tcp_client_host}.`);
      if (errors.tcp_client_port) setFieldError('tcp-port-error', `Port ${errors.tcp_client_port}.`);
    }, renderTcpConfig);
    setTcpMessage($('tcp-enabled').checked
      ? 'Saved. The client now uses the new target.'
      : 'Saved. The client is disabled.', 'ok');
  } catch (error) {
    setTcpMessage(`Saving failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

function setFactoryResetMessage(text, severity) { setFormMessage('factory-reset-message', text, severity); }

function showFactoryResetConfirm(show) {
  const confirm = $('factory-reset-confirm');
  if (confirm) confirm.hidden = !show;
  const open = $('factory-reset-open');
  if (open) open.hidden = show;
}

async function handleFactoryReset() {
  configSaving = true;
  updateConfigControls();
  setFactoryResetMessage('Erasing stored settings…', 'warn');

  try {
    const r = await fetch('/api/config/reset', { method: 'POST' });
    const payload = await r.json().catch(() => null);
    if (!r.ok) {
      const errors = payload && payload.errors ? payload.errors : {};
      throw new Error(Object.values(errors)[0] || `HTTP ${r.status}`);
    }
    // A 200 without parseable JSON still means the reset happened; only
    // re-render the forms when real config data came back.
    if (payload) renderConfig(payload);
    else renderRebootRequired(true);
    showFactoryResetConfirm(false);
    setFactoryResetMessage('All stored settings erased. Reboot to return to the build-time defaults.', 'ok');
  } catch (error) {
    setFactoryResetMessage(`Factory reset failed: ${error.message}.`, 'bad');
  }
  configSaving = false;
  updateConfigControls();
}

async function handleReboot() {
  rebootInProgress = true;
  updateConfigControls();
  setStaMessage('Rebooting…', 'warn');

  try {
    const r = await fetch('/api/reboot', { method: 'POST' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    configRebootExpectedUntil = Date.now() + 60000;
    // Successful polls after this grace period mean the device is back up,
    // even if the reboot happened entirely between two polls.
    rebootGraceUntil = Date.now() + 5000;
    setStaMessage('Rebooting. This page reconnects automatically.', 'warn');
  } catch (error) {
    rebootInProgress = false;
    setStaMessage(`Reboot request failed: ${error.message}.`, 'bad');
  }
  updateConfigControls();
}

function wireConfigUi() {
  const form = $('ais-config-form');
  if (form) form.addEventListener('submit', handleConfigSubmit);
  const staForm = $('sta-config-form');
  if (staForm) staForm.addEventListener('submit', handleStaSubmit);
  const tcpForm = $('tcp-config-form');
  if (tcpForm) tcpForm.addEventListener('submit', handleTcpSubmit);
  const apForm = $('ap-config-form');
  if (apForm) apForm.addEventListener('submit', handleApSubmit);
  const systemForm = $('system-config-form');
  if (systemForm) systemForm.addEventListener('submit', handleSystemSubmit);
  const pskClear = $('sta-psk-clear');
  if (pskClear) pskClear.addEventListener('change', updateConfigControls);
  const apPskClear = $('ap-psk-clear');
  if (apPskClear) apPskClear.addEventListener('change', updateConfigControls);
  const reboot = $('reboot-button');
  if (reboot) reboot.addEventListener('click', handleReboot);
  const resetOpen = $('factory-reset-open');
  if (resetOpen) resetOpen.addEventListener('click', () => {
    showFactoryResetConfirm(true);
    setFactoryResetMessage('');
  });
  const resetCancel = $('factory-reset-cancel');
  if (resetCancel) resetCancel.addEventListener('click', () => showFactoryResetConfirm(false));
  const resetDo = $('factory-reset-do');
  if (resetDo) resetDo.addEventListener('click', handleFactoryReset);
  updateConfigControls();
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
  const expectedConfigReboot = Date.now() < configRebootExpectedUntil;
  const expectedReboot = expectedOtaReboot || expectedConfigReboot;
  statusOffline = true;

  for (const id of ['connection-state', 'input-state', 'wifi-state']) {
    const el = $(id);
    if (!el) continue;
    el.classList.remove('state-ok', 'state-warn');
    el.classList.add('state-bad');
    el.querySelector('b').textContent = expectedReboot ? 'REBOOTING' : 'OFFLINE';
  }

  setText('fw-version', '—');
  setText('connection-detail', '—');
  setText('wifi-detail', '—');
  for (const id of counters) setText(id, '—');

  const notice = $('offline-notice');
  if (notice) notice.hidden = false;
  setText('offline-title', expectedOtaReboot ? 'Rebooting for firmware update'
    : (expectedConfigReboot ? 'Rebooting' : 'Bridge unreachable'));
  setText('offline-reason', expectedOtaReboot
    ? 'OTA upload complete. The bridge is rebooting into the test image; this page reconnects automatically.'
    : (expectedConfigReboot
      ? 'The bridge is restarting to apply configuration changes; this page reconnects automatically.'
      : (error instanceof Error && error.message
        ? `No response from the bridge (${error.message}). Retrying every 2 s.`
        : 'No response from the bridge. Retrying every 2 s.')));
  setText('updated', expectedReboot ? 'Rebooting · retrying every 2 s' : 'Offline · retrying every 2 s');

  $('details').innerHTML = expectedOtaReboot
    ? rowItem('Status updates', 'paused during reboot', 'warn') +
      rowItem('Next retry', 'in 2 s') +
      rowItem('Expected result', 'test image comes online and self-confirms', 'ok')
    : (expectedConfigReboot
      ? rowItem('Status updates', 'paused during reboot', 'warn') +
        rowItem('Next retry', 'in 2 s') +
        rowItem('Expected result', 'bridge comes back with the saved configuration', 'ok')
      : rowItem('Status updates', 'unreachable', 'bad') +
        rowItem('Counters', 'unavailable', 'warn') +
        rowItem('Next retry', 'in 2 s'));

  updateOtaControls();
}

function clearOffline() {
  const wasOffline = statusOffline;
  const rebootOver = rebootInProgress && Date.now() > rebootGraceUntil;
  statusOffline = false;
  const notice = $('offline-notice');
  if (notice) notice.hidden = true;
  if (wasOffline || rebootOver) {
    // Back after a reboot or outage: re-fetch config so the
    // reboot-required banner reflects the device's fresh state.
    rebootStateCleared();
    loadConfig();
  }
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

  setText('fw-version', status.firmware_version ? `v${status.firmware_version}` : '—');

  const clients = status.tcp_server ? status.tcp_server.active_peers : null;
  setText('connection-detail', clients == null ? '—' : `${format(clients)} TCP client${clients === 1 ? '' : 's'}`);

  const wifiIp = status.wifi.ip || '—';
  const wifiRssi = status.wifi.rssi == null ? '—' : `${String(status.wifi.rssi).replace('-', '−')} dBm`;
  setText('wifi-detail', status.wifi.sta_ready ? `${wifiIp} · ${wifiRssi}` : '—');

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
wireConfigUi();
wireOtaUi();
loadConfig();
pollStatus();
setInterval(pollStatus, 2000);
