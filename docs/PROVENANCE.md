# Source provenance

Type2DK build integration, SR040 session recovery and GPIO13 software UART are derived from
https://github.com/temesotejam/type2dk-uwb-uart at b1dd848f5d3aa19790b0b449e6767e31beb4b58e.
The previous project remains unchanged. Wire protocol in this repository is incompatible
with the old snapshot protocol and uses a distinct magic/version.
NXP SDK v04.03.14, firmware blobs and Raspberry Pi SDK are external dependencies.
Do not upload the NXP SDK to this repository. Obtain it through the vendor distribution.

`central/cores3/usb_log_driver.c` is derived from Espressif ESP-IDF 4.4.7's
[USB Serial/JTAG driver](https://github.com/espressif/esp-idf/blob/v4.4.7/components/driver/usb_serial_jtag.c),
under Apache-2.0 (copyright Espressif Systems 2021-2023).
The TX idle scheduling is adapted from the official
[ESP-IDF 5.5 driver](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_driver_usb_serial_jtag/src/usb_serial_jtag.c)
(copyright Espressif Systems 2021-2025, Apache-2.0).
Changes dated 2026-09-18: private function names, preserve/prime initial TX,
idle ZLP, queue race recheck, overlap-safe partial-write stash, allocation checks,
and a FIFO-written byte counter. Arduino HWCDC and the SDK USB driver are not installed.
The corresponding license is `licenses/Apache-2.0.txt`, also bundled in firmware downloads.


Type2BP support (0.2.0) targets the user's Rev.4.1 EVK and supplied
`Type2BP_SDK_UWBIOT_SR150_v04.08.01_MCUx.zip`.
`type2bp/build/prepare_sdk.py` applies the supplied `2bp_prebuild_v04.08.01.patch`
to an external SDK and disables accelerometer initialization and SE051W use.
The new anchor app uses the SR150 API's session handles, derives peer sessions
from the shared JSON, and applies the vendor patch's per-board OTP TX/XTAL
calibration values to runtime registers. It does not write OTP.
SDK sources, firmware source arrays and vendor patches are not copied here.
`type2bp/licenses/` contains the SR150 release's EULA and Software Content Register;
common runtime/BSD/FreeRTOS notices remain bundled alongside them.

The SR150 stated ROM image area is 0x60000 bytes (no OTA use) to fit its embedded
SR150 firmware; the actual flash image and ROM trailer are independently checked.
All firmware is experimental until validated on the user's hardware.
