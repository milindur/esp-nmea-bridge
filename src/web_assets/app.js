const $ = (id) => document.getElementById(id);
const fmt = new Intl.NumberFormat('en-US');
const format = (v) => fmt.format(v ?? 0);
const metrics = ['frames', 'bytes', 'clients', 'drops', 'warn-count'];

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

function setOffline(error) {
  for (const id of ['connection-state', 'input-state', 'wifi-state']) {
    const el = $(id);
    if (!el) continue;
    el.classList.remove('state-ok', 'state-warn');
    el.classList.add('state-bad');
    el.querySelector('b').textContent = 'OFFLINE';
  }

  for (const id of metrics) setText(id, '—');

  const reason = error instanceof Error && error.message ? `Reason: ${error.message}` : 'No response from /api/status';
  const notice = $('offline-notice');
  if (notice) notice.hidden = false;
  setText('offline-reason', reason);
  setText('poll-label', 'Offline watch');
  setText('updated', `Offline ${new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'})}`);
  setText('app-health', 'ESP NMEA Bridge · offline / API unreachable');

  $('details').innerHTML =
    rowItem('Status API', 'unreachable', 'bad') +
    rowItem('Displayed counters', 'unavailable', 'warn') +
    rowItem('Next retry', 'in 2 s');
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

function render(status) {
  clearOffline();
  setState('connection-state', status.connection_state, 'connected');
  setState('input-state', status.input_state, 'active');
  setState('wifi-state', status.wifi.sta_ready ? 'linked' : 'idle', 'linked');

  setText('frames', format(status.bridge.frames_in));
  setText('bytes', format(status.uart.bytes_rx));
  setText('clients', `${format(status.tcp_server.active_clients)} / ${format(status.tcp_server.max_clients)}`);
  setText('drops', format(status.bridge.sink_dropped_oldest));

  const warnCount = (status.warnings.data_quality ? 1 : 0) + (status.warnings.frame_loss ? 1 : 0);
  setText('warn-count', String(warnCount));

  $('details').innerHTML =
    rowItem('Active TCP NMEA sessions', format(status.tcp.active_sessions), status.tcp.active_sessions > 0 ? 'ok' : null) +
    rowItem('Inbound TCP clients', `${format(status.tcp_server.active_clients)} / ${format(status.tcp_server.max_clients)}`) +
    rowItem('UART frames received', format(status.uart.frames_rx)) +
    rowItem('AIS self-MMSI filtered', format(status.uart.ais_self_mmsi_filtered), status.uart.ais_self_mmsi_filtered > 0 ? 'ok' : null) +
    rowItem('Overlong UART frames', format(status.uart.overlong_frames), status.uart.overlong_frames > 0 ? 'warn' : null) +
    rowItem('Ingest dropped (oldest)', format(status.bridge.ingest_dropped_oldest), status.bridge.ingest_dropped_oldest > 0 ? 'warn' : null) +
    rowItem('No-sink publishes', format(status.bridge.publish_no_sinks)) +
    rowItem('Invalid / oversize', `${format(status.bridge.publish_invalid)} · ${format(status.bridge.publish_oversize)}`) +
    rowItem('STA IPv4 ready', status.wifi.sta_ready ? 'yes' : 'no', status.wifi.sta_ready ? 'ok' : null) +
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

pollStatus();
setInterval(pollStatus, 2000);
