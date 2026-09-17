# Source provenance

Type2DK build integration, SR040 session recovery and GPIO13 software UART are derived from
https://github.com/temesotejam/type2dk-uwb-uart at b1dd848f5d3aa19790b0b449e6767e31beb4b58e.
The previous project remains unchanged. Wire protocol in this repository is incompatible
with the old snapshot protocol and uses a distinct magic/version.
NXP SDK v04.03.14, firmware blobs and Raspberry Pi SDK are external dependencies.
Do not upload the NXP SDK to this repository. Obtain it through the vendor distribution.
