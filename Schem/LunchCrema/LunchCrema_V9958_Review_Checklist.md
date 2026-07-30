# pBITz V9958 Video Card — Schematic and BOM Review Checklist

Reusable per-revision review checklist for the **LunchCrema** V9958 RGBS video card on the pBITz backplane. This revision extends the supplied checklist with the VDP WAIT bridge and a systematic `Value ↔ MPN ↔ footprint` audit.

---

## Board under review

| Field | Value |
|---|---|
| Board name | **LunchCrema** — V9958 linear-RGB / RGBS video card |
| Platform | pBITz / Coffee Series — Zephyr-80 host |
| Review date | **2026-07-29** |
| Schematic source | `LunchCrema(3).kicad_sch`, `Video(3).kicad_sch`, `DeviceSelectDecode(6).kicad_sch`, `pBITzBusInterface(6).kicad_sch` |
| Review method | Hierarchy-aware S-expression net trace, direct schematic metadata parse, and manufacturer/distributor BOM verification |
| Trace coverage | Video 295/338; select decode 38/38; bus sheet 39/101 pin endpoints. The low bus-sheet percentage is dominated by intentionally unused backplane pins. |
| Connectivity result | **PASS** — no output-to-output conflicts and no newly identified functional blocker |
| BOM result | **CONDITIONAL PASS** — one definite package mismatch plus three metadata cleanups before automated ordering |
| PCB/layout scope | No `.kicad_pcb` was included in this upload; placement and routing were not re-reviewed here |

**Status legend:** ✅ pass · ❌ fail/blocker · ⚠️ verify/correct · ➖ n/a · 🔧 optional/polish

---

## 1. Power and decoupling

| # | Check | Status | Notes |
|---|---|---|---|
| 1.1 | Single 5 V supply architecture | ✅ | Only `+5V` and `GND` are used by the card circuitry. Unused backplane 3.3 V pins remain disconnected. |
| 1.2 | V9958 core supply | ✅ | U2 pin 58 is the core VCC; pin 1 is GND. |
| 1.3 | V9958 DAC supply | ✅ | U2 pin 21 is VCC/DAC; pin 20 is GND/DAC. |
| 1.4 | Core and DAC local bypass capacitors | ✅ | C10/C11 and the surrounding 100 nF network are present. The previously added capacitor near pin 58 is represented in the schematic. Physical placement was not rechecked without the PCB file. |
| 1.5 | Local bulk capacitance | ✅ | C12 = 10 µF, Panasonic `EEA-GA1E100H`, radial 5 mm footprint with 2.5 mm lead pitch. |
| 1.6 | Per-logic and per-DRAM bypassing | ✅ | 100 nF capacitors C8–C11 and C13–C18 cover the active IC groups. |
| 1.7 | WAIT MOSFET default state | ✅ | R6 = 47 kΩ pulls `WAIT_SINK`/Q1 gate low so Q1 remains off when U11 is unpowered or unprogrammed. |

## 2. Clock and oscillator

| # | Check | Status | Notes |
|---|---|---|---|
| 2.1 | VDP crystal frequency | ✅ | Y1 metadata identifies `HC-49/U-S21477270ABJB`: 21.47727 MHz, 18 pF load. |
| 2.2 | Crystal load network | ✅ | C6/C7 = 27 pF. With board/input stray capacitance, this is consistent with an 18 pF load crystal. Bench-confirm startup. |
| 2.3 | Alternate oscillator population | ✅ | Y2 is excluded from BOM and marked DNP. Y1 is the normal population. |
| 2.4 | V9958 CPUCLK use | ✅ | U2 pin 8 `CPUCLK/VDS` drives U11 pin 1, the WAIT-bridge register clock. |
| 2.5 | Firmware clock contract | ⚠️ | Normal operation requires R#25.VDS = 0 so pin 8 remains CPUCLK. This is an accepted software contract. |

## 3. CPU interface and address decode

| # | Check | Status | Notes |
|---|---|---|---|
| 3.1 | Host data-bit ordering | ✅ | U2 CD0–CD7 map straight to D0–D7. |
| 3.2 | VDP mode-address pins | ✅ | MODE0 → A0 and MODE1 → A1. |
| 3.3 | Read strobe generation | ✅ | U10A uses `/CS_VDP` and `/RD`; O0 drives `/CSR`. |
| 3.4 | Write and configuration decode | ✅ | U10B uses `/CS_VDP`, A2 and `/WR`; O0 drives `/CSW`, O1 drives `/INT_MODE`. |
| 3.5 | Decoder family in uploaded file | ⚠️ | The uploaded file contains **74AHC139 / SN74AHC139DR**, not AHCT. Value, MPN and SOIC-16 footprint are mutually consistent, but this does not reflect the previously stated AHCT change. Use `74AHCT139 / SN74AHCT139DR` if TTL-compatible inputs are the intended final configuration. |
| 3.6 | Reset to VDP | ✅ | U2 pin 9 connects to pBITz `/RESET`. |

## 4. V9958 WAIT-state bridge

| # | Check | Status | Notes |
|---|---|---|---|
| 4.1 | VDP request sensing | ✅ | U11 pins 2/3 sense the same `/CSR` and `/CSW` nets that feed U2 pins 31/30. |
| 4.2 | Native VDP WAIT input | ✅ | U2 pin 26 `/WAIT` feeds U11 pin 4; it is not directly tied to the shared bus WAIT net. |
| 4.3 | VDP-clock porch | ✅ | U11 pin 1 is driven from V9958 CPUCLK, allowing the PLD to enforce the complete VDP-clock porch before trusting native `/WAIT`. |
| 4.4 | Active-low enable and reset default | ✅ | U3 Q1 drives `/WS_EN`. U3 is cleared by `/RESET`, so `/WS_EN` defaults low and the external porch is enabled at reset. |
| 4.5 | Host WAIT output topology | ✅ | U11 pin 21 drives active-high `WAIT_SINK`; Q1 2N7002 then pulls shared pBITz `/WAIT` low. The motherboard supplies the WAIT pull-up. |
| 4.6 | MOSFET orientation | ✅ | Q1 gate → `WAIT_SINK`, source → GND, drain → bus `/WAIT`. |
| 4.7 | PLD unused inputs | ✅ | U11 pins 7–11 and 13 are tied to GND. Unused output macrocells are NC. |
| 4.8 | Firmware WTE contract | ⚠️ | Normal operation requires R#25.WTE = 1. Boot firmware must enable it using software-paced initial VDP writes. |
| 4.9 | Configuration-latch write discipline | ⚠️ | U3 captures D0 and D1 together on `/INT_MODE`; interrupt-routing writes must preserve the desired `/WS_EN` bit. |
| 4.10 | First-article timing proof | ⚠️ | Scope `/WAIT` at the CPU pin while hammering VDP ports. This is a bench item, not a schematic blocker. |

## 5. VRAM subsystem

| # | Check | Status | Notes |
|---|---|---|---|
| 5.1 | Organization and capacity | ✅ | Four 64K×4 DRAMs provide 128 KiB total. |
| 5.2 | Multiplexed address bus | ✅ | AD0–AD7 reach A0–A7 on U4–U7. |
| 5.3 | Data mapping | ✅ | U4/U6 carry RD0–RD3; U5/U7 carry RD4–RD7. |
| 5.4 | Bank strobes | ✅ | `/CAS0` selects U4/U5; `/CAS1` selects U6/U7. |
| 5.5 | Common strobes | ✅ | `/RAS` and R/W reach all four DRAMs. `/OE` is grounded on all four. |
| 5.6 | Expansion CAS | ✅ | `/CASX` is intentionally unused. |
| 5.7 | DRAM socket metadata | ✅ | MPN `1-2199298-5` is an 18-position, 7.62 mm-row DIP socket matching the DIP-18 socket footprint. |
| 5.8 | Installed DRAM Value metadata | ⚠️ | U4–U7 have blank Value fields. Set them to `41464-10` or the exact installed 100 ns DRAM type while retaining the socket MPN if the DRAMs come from inventory. |

## 6. V9958 sync/genlock and unused pins

| # | Check | Status | Notes |
|---|---|---|---|
| 6.1 | External-sync inputs | ✅ | `/VRESET` and `/HRESET` are tied inactive/high. The trace tool reports this as input-only because it does not reliably merge every graphical power symbol. |
| 6.2 | `/DLCLK` bias | ✅ | R20 = 4.7 kΩ pull-up. |
| 6.3 | VBB handling | ✅ | U2 pin 33 is intentionally NC. |
| 6.4 | Unused digital outputs/bus | ✅ | DHCLK, HSYNC, BLEO, YS, CDBR and C0–C7 are intentionally unused. |
| 6.5 | CPUCLK no longer NC | ✅ | The old checklist is superseded: CPUCLK is now used by U11. |

## 7. Interrupt routing

| # | Check | Status | Notes |
|---|---|---|---|
| 7.1 | VDP interrupt pull-up | ✅ | R5 = 4.7 kΩ on `/INT_i`. |
| 7.2 | Software-selectable route | ✅ | U3 Q0 drives U8 select; U8 is enabled by `/INT_i`. |
| 7.3 | Shared-bus isolation | ✅ | U8 outputs drive the cathodes of U9; U9 anodes connect to bus `/INT` and `/NMI`, preventing push-pull contention. |
| 7.4 | Diode-array MPN and footprint | ✅ | `BAT54JW-7-F` is a dual isolated Schottky array in SOT-363; the U9 symbol pin map and SC-70/SOT-363 footprint match. |
| 7.5 | U8 MPN and footprint | ✅ | `SN74LVC1G19DCKR` is the 6-pin SC-70/DCK package and supports 5 V operation. |

## 8. Reset and local configuration latch

| # | Check | Status | Notes |
|---|---|---|---|
| 8.1 | Latch reset | ✅ | U3 `/MR` connects to bus `/RESET`. |
| 8.2 | Defined unused inputs | ✅ | U3 D2–D7 are tied low. |
| 8.3 | U3 package metadata | ❌ | **Definite BOM mismatch:** footprint is wide SOIC-20, 1.27 mm pitch, but MPN `SN74HC273DBR` is SSOP-20, 0.65 mm pitch. Change the MPN to `SN74HC273DWR` for the current footprint, or change the footprint to SSOP-20 for DBR. |
| 8.4 | U3 datasheet/value consistency | ✅ | Value `74HC273` matches the intended octal D flip-flop function; only the package suffix is wrong. |

## 9. Video output

| # | Check | Status | Notes |
|---|---|---|---|
| 9.1 | Output format | ✅ | Linear RGB plus composite sync over a DE-15 connector; this is RGBS, not VGA signalling. |
| 9.2 | RGB buffers | ✅ | Three MMPQ3904 NPN sections are emitter followers; the fourth section supports the optional sync-buffer path. |
| 9.3 | RGB AC coupling | ✅ | C2/C3/C4 = 220 µF. |
| 9.4 | RGB bias/load/series network | ✅ | 1 kΩ base pulldowns, 220 Ω emitter loads and 22 Ω series resistors are present. |
| 9.5 | Sync population option | ✅ | R19 = 470 Ω direct DC-coupled sync path is populated; R7 is excluded/DNP for the alternate transistor path. |
| 9.6 | Connector pin assignment | ✅ | R/G/B reach pins 1/2/3, sync reaches pin 13, and signal/shell grounds are connected. |
| 9.7 | DE-15 MPN and footprint geometry | ✅ | `L77HDE15SD1CH4FVGA` is a right-angle 15-position HD socket with 2.29 × 2.54 mm signal spacing and 25 mm mounting-hole spacing, matching the selected KiCad footprint family. |
| 9.8 | Output levels | ⚠️ | Confirm RGB amplitude, sync polarity and sync level into the actual GBS receiver during bring-up. |

## 10. Device select and card addressing

| # | Check | Status | Notes |
|---|---|---|---|
| 10.1 | Card select comparator | ✅ | U1 SN74HC688 compares CS0–CS3 against the coded switch. |
| 10.2 | Configurable address switch | ✅ | SW1 is SH-7070MC, a 16-position complementary-code rotary switch. |
| 10.3 | Switch footprint | ✅ | The SH-7010C footprint represents the common SH-7000 family physical layout; complementary-code variants share the mechanical pattern. |
| 10.4 | Switch pull-ups | ✅ | R1–R4 = 4.7 kΩ. |
| 10.5 | U1 MPN and footprint | ✅ | `SN74HC688PWR` is TSSOP-20 and matches the TSSOP footprint. |

## 11. Bus and backplane interface

| # | Check | Status | Notes |
|---|---|---|---|
| 11.1 | Data bus | ✅ | D0–D7 map to the VDP CPU data bus. |
| 11.2 | Address/control inputs | ✅ | A0–A2, `/RESET`, `/RD` and `/WR` are correctly mapped from the pBITz connector. |
| 11.3 | Shared WAIT | ✅ | Q1 drain connects to pBITz CTRL_12 (`/WAIT`). No card-local bus pull-up is present. |
| 11.4 | Interrupt pins | ✅ | Bus `/INT` and `/NMI` are on CTRL_14/CTRL_15 in the uploaded symbol. |
| 11.5 | Unused backplane pins | ✅ | Unused clocks, high data/address bits, SPI and 3.3 V pins are intentionally NC. |
| 11.6 | Trace conflicts | ✅ | Nettrace reported no nets with two ordinary output pins. |

## 12. BOM and procurement metadata

| # | Check | Status | Notes |
|---|---|---|---|
| 12.1 | Passive package codes | ✅ | KEMET `C0603...` and Vishay `CRCW0603...` MPNs correspond to 0603 footprints and the schematic values. |
| 12.2 | Electrolytic packages | ✅ | `EEA-GA1E100H` matches 10 µF radial, 2.5 mm pitch; `25YXJ220M6.3X11` matches 220 µF, 6.3 mm diameter, 2.5 mm pitch. |
| 12.3 | V9958 procurement convention | ✅ | U2 Value identifies `V9958`; MPN `D8864-42` identifies the 64-pin 1.778 mm shrink-DIP socket. This is consistent with an inventory-supplied VDP. |
| 12.4 | DRAM procurement convention | ⚠️ | U4–U7 MPNs correctly identify sockets, but the installed DRAM Values are blank. Add the inventory device identity. |
| 12.5 | PLD procurement convention | ⚠️ | U11 Value correctly identifies `ATF22V10C-15PU`, but the MPN field is empty despite the socket footprint. If the PLD is inventory-supplied and the socket is purchased, use a 24-pin 7.62 mm socket MPN such as `1-2199298-8`. |
| 12.6 | PLD datasheet metadata | 🔧 | U11 still links to a PEEL22CV10 datasheet. Replace with the Microchip ATF22V10C datasheet for documentation accuracy. |
| 12.7 | Decoder intended family | ⚠️ | Current Value/MPN are AHC and match each other. Change both to AHCT if that is the intended final part. The SOIC-16 footprint supports either. |
| 12.8 | Configuration latch package | ❌ | Correct U3 DBR/DW footprint mismatch before ordering. |
| 12.9 | Generic semiconductor MPNs | 🔧 | `2N7002` and `MMPQ3904` are usable generic identifiers; choose manufacturer-qualified orderable suffixes if the BOM is intended for fully automated purchasing. |
| 12.10 | DNP/excluded parts | ✅ | R7 and Y2 are marked DNP and excluded from BOM. |

## 13. General housekeeping

| # | Check | Status | Notes |
|---|---|---|---|
| 13.1 | No floating logic inputs | ✅ | Unused PLD and latch inputs are tied to defined levels. |
| 13.2 | Intentional NC outputs | ✅ | V9958 unused outputs, unused 139 outputs and unused PLD macrocell pins are intentionally NC. |
| 13.3 | Trace-tool isolated power pins | ✅ | The tracer reports several VCC pins as isolated because its geometry/power-symbol model does not resolve all graphical power connections. Direct schematic inspection and the decoupling network show these are powered. |
| 13.4 | Native ERC/DRC | ➖ | Not rerun in this environment. The user previously reported release checks passed; no PCB file was included in this review set. |

---

## Required corrections before BOM order

1. **U3:** change `SN74HC273DBR` to `SN74HC273DWR`, or change the footprint to SSOP-20.
2. **U10:** confirm whether final part is AHC or AHCT. The uploaded file is AHC; update Value and MPN together if AHCT is intended.
3. **U4–U7:** populate the installed DRAM Value fields (`41464-10` or exact inventory MPN).
4. **U11:** add the socket MPN if the BOM is expected to order the PLD socket; update the stale datasheet link.

Only item 1 is a definite order/assembly failure. Items 2–4 are intent/documentation/procurement cleanups.

## First-article checks

1. Scope `/WAIT` at the Z80 pin during back-to-back VDP reads and writes.
2. Enable R#25.WTE early, retain R#25.VDS = 0, and test with the porch enabled by default.
3. Hammer VRAM in the busiest display mode intended for Zephyr.
4. Verify crystal startup over power cycles.
5. Confirm RGB and CSYNC levels into the GBS input.
6. Verify interrupt routing in both `/INT` and `/NMI` modes while preserving `/WS_EN`.

---

## BOM verification sources

- Microchip ATF22V10C: <https://www.microchip.com/en-us/product/ATF22V10C>
- TI SN74AHC139: <https://www.ti.com/product/SN74AHC139>
- TI SN74AHCT139: <https://www.ti.com/product/SN74AHCT139>
- TI SN74HC273 package options: <https://www.ti.com/product/SN74HC273>
- TI SN74LVC1G19: <https://www.ti.com/product/SN74LVC1G19>
- Diodes BAT54JW: <https://www.diodes.com/assets/Datasheets/ds30157.pdf>
- Harwin D8864-42: <https://www.harwin.com/products/D8864-42>
- TE 18-pin socket `1-2199298-5`: <https://www.digikey.ca/en/products/detail/te-connectivity-amp-connectors/1-2199298-5/5022042>
- TE 24-pin socket `1-2199298-8`: <https://www.te.com/en/product-1-2199298-8.html>
- Citizen 21.47727 MHz crystal: <https://www.digikey.ca/en/products/detail/citizen-finedevice-co-ltd/HC-49-U-S21477270ABJB/284231>
- Amphenol DE-15 connector drawing: <https://cdn.amphenol-cs.com/media/wysiwyg/files/drawing/l77hde15sd1ch4fvga.pdf>
- Panasonic EEA-GA1E100H: <https://industrial.panasonic.com/ww/products/pt/aluminum-cap-lead/models/EEAGA1E100H>

---

## Revision log

| Rev | Date | Reviewer | Result | Key notes |
|---|---|---|---|---|
| 1 | 2026-06-15 | Previous checklist | PASS | Baseline V9958/VRAM/RGB review; RGB/sync bench check remained open. |
| 2 | 2026-07-29 | OpenAI GPT-5.6 Thinking | **Electrical PASS / BOM conditional** | WAIT bridge validated. Found U3 DBR-versus-wide-SOIC mismatch; current U10 is AHC rather than previously stated AHCT; DRAM Values blank; U11 socket MPN absent. |

> **Per-revision workflow:** copy this file, update the Board block, rerun the hierarchy trace and metadata audit, resolve all ❌ items, then add a revision-log entry.
