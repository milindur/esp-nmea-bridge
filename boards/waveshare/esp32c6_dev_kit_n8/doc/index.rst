.. zephyr:board:: esp32c6_dev_kit_n8

Overview
********

The Waveshare ESP32-C6-DEV-KIT-N8 is a compact development board based on the
ESP32-C6-WROOM-1 module with 8 MB SPI flash. The board provides 2.4 GHz Wi-Fi 6,
Bluetooth LE, IEEE 802.15.4, Zigbee, and Thread support through the ESP32-C6 SoC.

The USB-C connector is connected to onboard CH343 and CH334 chips for UART and USB
development through a single connector. The pinout is compatible with the
Espressif ESP32-C6-DevKitC-1 board, so this board definition follows the
ESP32-C6-DevKitC peripheral pin assignments.

Hardware
********

The board exposes most ESP32-C6 GPIOs on two pin headers and provides a BOOT button
connected to GPIO9. Zephyr's default console uses UART0 on GPIO16/GPIO17 at
115200 baud. The board exposes two USB CDC ACM tty devices through the USB-C
connector: the ESP32-C6 native USB Serial/JTAG interface for flashing/debugging
and the CH343 USB UART connected to UART0 for the Zephyr console. The kernel
assigned ``/dev/ttyACM*`` numbers are not stable; prefer ``/dev/serial/by-id/*``
links or install the udev rules from ``support/99-waveshare-esp32c6-dev-kit-n8.rules``
to create deterministic role-based links.

.. include:: ../../../espressif/common/soc-esp32c6-features.rst
   :start-after: espressif-soc-esp32c6-features

Supported Features
==================

.. zephyr:board-supported-hw::

System Requirements
*******************

.. include:: ../../../espressif/common/system-requirements.rst
   :start-after: espressif-system-requirements

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Deterministic serial device names
=================================

On Linux, install the provided udev rules to create stable device links:

.. code-block:: console

   sudo cp $ZEPHYR_BASE/boards/waveshare/esp32c6_dev_kit_n8/support/99-waveshare-esp32c6-dev-kit-n8.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules
   sudo udevadm trigger -s tty

After reconnecting the board, the following symlinks are available:

* ``/dev/waveshare/esp32c6-dev-kit-n8/jtag`` for the ESP32-C6 native USB Serial/JTAG flashing/debugging interface
* ``/dev/waveshare/esp32c6-dev-kit-n8/uart`` for the CH343 UART0 console

Serial-qualified symlinks are also created below ``/dev/waveshare/esp32c6-dev-kit-n8/``
and are preferable when multiple matching devices are connected.

For example, flash over the USB Serial/JTAG interface with:

.. code-block:: console

   west flash --esp-device /dev/waveshare/esp32c6-dev-kit-n8/jtag

.. include:: ../../../espressif/common/building-flashing.rst
   :start-after: espressif-building-flashing

.. include:: ../../../espressif/common/board-variants.rst
   :start-after: espressif-board-variants

Debugging
=========

.. include:: ../../../espressif/common/openocd-debugging.rst
   :start-after: espressif-openocd-debugging

References
**********

.. target-notes::

.. _`Waveshare ESP32-C6-DEV-KIT-N8 wiki`: https://docs.waveshare.com/ESP32-C6-DEV-KIT-N8
.. _`Waveshare ESP32-C6-DEV-KIT-N8 schematic`: https://files.waveshare.com/wiki/ESP32-C6-DEV-KIT-N8/ESP32-C6-DEV-KIT-N8-Schematic.pdf
