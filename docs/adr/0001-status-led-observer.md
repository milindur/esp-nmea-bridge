# Status LED observer for NMEA activity

The board has a single NeoPixel RGB LED on GPIO8, so connection state and NMEA forwarding activity must share one visual channel. We drive the LED as a WS2812 LED strip through Zephyr's I2S LED-strip support and keep status rendering in a separate `status_led` observer module: application code reports semantic events such as active TCP NMEA sessions, outbound TCP NMEA client connecting, UART NMEA frame receipt, successful TCP NMEA session send, and send failure. The LED module owns colors, priorities, pulse timing, and brightness so NMEA bridge, UART, and TCP modules stay free of LED-specific behavior.

Connected state wins over connecting, short-lived forwarding flashes override the base state, sink TX/forwarded wins over UART RX when they overlap, and hardware LED failure is non-fatal.
