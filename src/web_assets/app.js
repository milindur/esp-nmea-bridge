const $ = (id) => document.getElementById(id);
const numberFormat = new Intl.NumberFormat('en-US');

function format(value) {
  return numberFormat.format(value ?? 0);
}

function row(label, value) {
  return `<div class="row"><span>${label}</span><b>${value}</b></div>`;
}

function setHealth(health) {
  const className = health === 'ok' ? 'ok' : health === 'degraded' ? 'warn' : 'bad';
  const healthEl = $('health');

  healthEl.className = `health ${className}`;
  healthEl.querySelector('b').textContent = health.toUpperCase();
}

async function pollStatus() {
  try {
    const response = await fetch('/api/status', { cache: 'no-store' });

    if (!response.ok) {
      throw new Error(response.status);
    }

    const status = await response.json();

    setHealth(status.health);
    $('frames').textContent = format(status.bridge.frames_in);
    $('bytes').textContent = format(status.uart.bytes_rx);
    $('clients').textContent = `${format(status.tcp_server.active_clients)}/${format(status.tcp_server.max_clients)}`;
    $('drops').textContent = format(status.bridge.sink_dropped_oldest);
    $('details').innerHTML =
      row('UART lines', format(status.uart.lines_rx)) +
      row('Overlong lines', format(status.uart.overlong_lines)) +
      row('Ingest drops', format(status.bridge.ingest_dropped_oldest)) +
      row('No-sink publishes', format(status.bridge.publish_no_sinks)) +
      row('Invalid / oversize', `${format(status.bridge.publish_invalid)} / ${format(status.bridge.publish_oversize)}`) +
      row('STA IPv4 ready', status.wifi.sta_ready ? 'yes' : 'no');
    $('updated').textContent = `Updated ${new Date().toLocaleTimeString()}`;
  } catch (error) {
    setHealth('offline');
    $('updated').textContent = `Polling failed: ${error.message}`;
  }
}

pollStatus();
setInterval(pollStatus, 2000);
