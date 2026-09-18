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

Type2BP support targets the user's Rev.4.1 EVK and supplied
`Type2BP_SDK_UWBIOT_SR150_v04.08.01_MCUx.zip`.
`type2bp/build/prepare_sdk.py` applies the supplied `2bp_prebuild_v04.08.01.patch`
to an external SDK and disables accelerometer initialization and SE051W use.
The anchor app applies the vendor patch's per-board OTP TX/XTAL calibration values
to runtime registers. It does not write OTP. SDK sources, firmware source arrays and
vendor patches are not copied here.

The user-supplied `anchor1_controlee(2).hex` is the 1111 fixed node and is
byte-identical to the earlier inspected 1111 image (SHA-256
`e53fa7e3f5de98e07bffdc1d3fa9f7fb7ca37df84bf6d8dcc2aa2a6a05900d04`).
It identifies itself as an SR150 v04.06.05 ranging controlee. The image is not redistributed.

## 0.4.0 legacy compatibility

v0.4.0 leaves 1111..6666 unchanged and builds only moving A/B plus new 7777
(SR150 v04.08.01 SDK) and 8888 (SR040 v04.03.14 SDK).

The legacy radio profile uses controller 0x0000, session 0x11223344, Ch9,
SP3/SFD2/preamble9, 25 slots and 50 ms interval. New responders use slots 7 and 8.
A/B logical IDs 0050/0051 are host/log identities, not legacy UWB MAC addresses.
B startup is delayed 25 ms to reduce persistent collision risk. This simultaneous
two-controller arrangement remains hardware-unverified.
