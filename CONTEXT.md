# ESP NMEA Bridge Context

This context names the NMEA-over-UART and NMEA-over-TCP concepts used by the ESP NMEA bridge app.

## Language

**NMEA frame**:
A single newline-terminated NMEA-0183 message as carried through the bridge after UART framing. It must remain complete; truncating a NMEA frame is invalid because the trailing checksum would no longer describe the carried bytes.
_Avoid_: packet, line, message

**NMEA bridge**:
The in-process fan-out point that accepts NMEA frames from producers and queues them for registered sinks.
_Avoid_: bus, broker, router

**AIS self-MMSI filter**:
An optional input-side filter that examines complete AIS VDM NMEA frames and drops only frames whose decoded AIS MMSI is confirmed to match the configured own MMSI. It accepts VDM from any NMEA talker ID, leaves VDO pass-through, requires a valid NMEA checksum before dropping, and tracks multipart VDM sequences when a sequence ID safely identifies following fragments. Unknown, malformed, unsupported, checksum-missing, checksum-invalid, non-VDM, or ambiguously correlated NMEA frames remain pass-through.
_Avoid_: AIS decoder, NMEA validator, generic frame gate

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

**NMEA input state**:
The current availability of NMEA frames at the UART input, evaluated over a short time window. The ESP NMEA bridge treats input as active when NMEA bridge accepted-frame counts increase during that window.
_Avoid_: NMEA health, data health, UART health

**NMEA forwarding activity**:
A short-lived occurrence where a UART NMEA frame is accepted by the NMEA bridge or an NMEA frame is successfully sent by a TCP NMEA session.
_Avoid_: packet activity, data event

**Bridge telemetry**:
Observational status about the ESP NMEA bridge, including counters, NMEA input state, NMEA connection state, and warnings for local display or diagnostics. It is not a control plane; modules that change bridge behaviour should use separate control concepts.
_Avoid_: status service, control state, management API

## Example dialogue

Dev: "When the TCP NMEA server accepts a peer, should it publish frames directly?"
Domain expert: "No. The TCP NMEA server only accepts peers. Each accepted peer becomes a TCP NMEA session."
Dev: "Does the TCP NMEA client use a different sender?"
Domain expert: "No. Once connected, it is also a TCP NMEA session carrying NMEA frames from the NMEA bridge."
