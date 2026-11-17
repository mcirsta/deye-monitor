# AM3 AC output and grid settings

The AM3 Telemetry view reads these settings through Modbus FC03. It does not write them. The configured output voltage is a menu choice, while the measured inverter output is a live reading. Grid high and low voltage limits govern grid protection/connection; they are not the output-voltage target.

| Telemetry row | Holding register | Decoding |
| --- | ---: | --- |
| Configured output voltage | 280, bits 0–3 | For Grid Type 0 (Single Phase): code 0 = 230 V, 1 = 220 V, 2 = 240 V, 3 = 200 V, 4 = 120 V, 5 = 127 V. Unrecognized codes are shown as codes. |
| Measured inverter output | 154 | Raw ÷ 10 V |
| Grid frequency setting | 285 | 0 = 50 Hz, 1 = 60 Hz |
| Grid type | 286 | 0 = Single Phase; other codes are shown without interpretation |
| Grid mode | 284 | 0 = General Standard; other codes are shown without interpretation |
| High grid-voltage limit | 287 | Raw ÷ 10 V |
| Low grid-voltage limit | 288 | Raw ÷ 10 V |

The output-voltage field is packed into register 280. Reading the whole register as a voltage is incorrect: the installed inverter returned `0x2000`, whose low nibble is code 0, or 230 V. Its concurrent settings read returned Grid Mode 0, Grid Frequency 0, Grid Type 0, high limit 265.0 V and low limit 185.0 V. A separate live read measured inverter output at 230.0 V. These are observations from 17 October 2026, not fixed defaults.

The [exact-family Deye manual](https://www.deyeinverter.com/deyeinverter/2026/05/19/BManualSUN-5-12K-SG02LP1-EU-AM320260519en.pdf) labels the control **Grid Setting → Grid Set1 → INV Output Voltage** and illustrates 200, 220, 230 and 240 V. The C37B HMI firmware menu and commit paths establish the low-nibble mapping and the additional 120 and 127 V labels. The public C37B image has the tested HMI's version label, but byte identity with the installed flash has not been established.
