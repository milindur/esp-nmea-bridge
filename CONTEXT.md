# ESP Serial Bridge Context

This context names the NMEA-over-UART and NMEA-over-TCP concepts used by the ESP serial bridge app.

## Language

**NMEA frame**:
A single newline-terminated NMEA-0183 message as carried through the bridge after UART framing.
_Avoid_: packet, line, message

**NMEA bridge**:
The in-process fan-out point that accepts NMEA frames from producers and queues them for registered sinks.
_Avoid_: bus, broker, router

**TCP NMEA session**:
One connected TCP socket carrying NMEA frames from the NMEA bridge to a peer until the peer closes or sending fails.
_Avoid_: connection handler, client worker, socket loop

**TCP NMEA server**:
The listener that accepts peers and creates one TCP NMEA session per peer.
_Avoid_: server session

**TCP NMEA client**:
The outbound connector that creates a TCP NMEA session to a configured host or STA gateway.
_Avoid_: upstream session

**NMEA connection state**:
The aggregate availability of active TCP NMEA sessions for carrying NMEA frames. A listening TCP NMEA server without an active session is not connected.
_Avoid_: Wi-Fi state, server state

**NMEA forwarding activity**:
A short-lived occurrence where an NMEA frame is received from UART or successfully sent by a TCP NMEA session.
_Avoid_: packet activity, data event

## Example dialogue

Dev: "When the TCP NMEA server accepts a peer, should it publish frames directly?"
Domain expert: "No. The TCP NMEA server only accepts peers. Each accepted peer becomes a TCP NMEA session."
Dev: "Does the TCP NMEA client use a different sender?"
Domain expert: "No. Once connected, it is also a TCP NMEA session carrying NMEA frames from the NMEA bridge."
