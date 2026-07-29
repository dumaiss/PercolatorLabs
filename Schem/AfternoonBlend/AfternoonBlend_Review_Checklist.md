# pBITz SN76489 Sound Card — Schematic Review Checklist

Reusable per-revision review template for the **AfternoonBlend** four-PSG stereo sound card on the pBITz backplane.
The structure follows the LunchCrema review checklist so both cards can be reviewed and diffed consistently.

---

## Board under review

| Field | Value |
|---|---|
| Board name | **AfternoonBlend** — 4× SN76489AN PSG + AD7801 PCM DAC, stereo mixer and headphone/line output |
| Platform | pBITz / Coffee Series — Zephyr-80 host at 10 MHz |
| Revision | Current uploaded schematic; no revision value is encoded in the root title block |
| Schematic source | `AfternoonBlend(1).kicad_sch` + `Sound(1)` / `DeviceSelectDecode(5)` / `pBITzBusInterface(5)` sheets — KiCad 10 |
| Review method | Full hierarchy-aware S-expression net trace, plus manual logic, timing and analogue-path review |
| Review date | 2026-07-29 |
| Net-trace result | **182 nets / 414 placed pins; no output-to-output conflicts found** |
| Review result | **CONDITIONAL PASS** — functional design and connectivity are sound; one timing-margin BOM change is strongly recommended before fabrication: U8 `74HC138` → `74AHCT138` or equivalent fast pin-compatible decoder |
| Review scope | Schematic only. No `.kicad_pcb` was supplied, so placement, return paths, routing, clearances and physical connector orientation were not reviewed |

**Status legend:** ✅ pass · ❌ fail · ⚠️ verify / open · ➖ n/a · 🔧 optional / documentation polish

---

## Executive findings

1. **No functional topology or connectivity blocker was found.** The four PSGs, PCM DAC, stereo summing network, output amplifier and new READY-based wait-state generator are connected coherently.
2. **The WAIT-state state machine is correct.** A PSG select transition sets the immediate WAIT latch; the selected SN76489AN's open-collector READY line then controls completion; READY returning high clears the latch and releases the pBITz `/WAIT` line through a 2N7002.
3. **U8 is the only pre-fabrication timing concern.** Its current `74HC138` value can take up to 39 ns from an enable input to an output at 4.5 V over temperature. It is followed by U14, U5, Q1 and the backplane `/WAIT` RC path. At a 10 MHz Z80 this leaves essentially no guaranteed worst-case margin. A pin-compatible `SN74AHCT138` reduces the decoder contribution to about 11 ns and is TTL-input compatible.
4. **R15 = 2.2 kΩ is appropriate.** It matches the class of pull-up used for TI's READY timing characterization and gives a substantially cleaner READY rising edge than the earlier 10 kΩ value.
5. Remaining open items are **first-article measurements**, chiefly `/WAIT` timing at the CPU pin, READY edge quality, analogue headroom, output level, noise and stereo separation.

---

## 1. Power and decoupling

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 1.1 | Single card supply rail; no stray −5 V / +12 V dependencies | ✅ | Card logic and analogue circuitry use `+5V` and `GND` only |
| 1.2 | PSG supplies correct | ✅ | U6/U7/U11/U12 pin 16 → +5 V; pin 8 → GND |
| 1.3 | Logic supplies correct | ✅ | U1, U4, U5, U8, U9 and U14 use +5 V / GND |
| 1.4 | Analogue-device supplies correct | ✅ | TLV2372 U2, AD7801 U3 and LM4880 U10 are operated from +5 V; analogue and digital grounds are intentionally common |
| 1.5 | One local 100 nF provision per active IC / oscillator | ✅ | Fourteen 100 nF capacitors are present for fourteen active devices including the oscillator: C1, C20, C2/C3/C5/C6/C7/C8/C15/C16/C19/C21/C22/C23 |
| 1.6 | Local bulk / low-frequency bypassing | ✅ | C17 = 10 µF supply bulk; C10 = 10 µF VREF filtering; LM4880 and output stages have their required bypass/coupling capacitors |
| 1.7 | Physical decoupler placement and plane-via geometry | ➖ | Cannot be checked without the PCB file; place each 100 nF part at its device supply pins with a short ground-plane return |

## 2. PSG master clock

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 2.1 | Clock frequency within SN76489AN rating | ✅ | Y1 = ECS-2100A-035, **3.579545 MHz**; SN76489AN maximum is 4 MHz |
| 2.2 | Clock voltage / output family compatible | ✅ | Y1 is a 5 V HCMOS/TTL oscillator |
| 2.3 | All PSGs use the same clock | ✅ | `CLK` connects Y1 output to pin 14 of U6/U7/U11/U12 |
| 2.4 | Clock is not used as an estimated transfer timer | ✅ | WAIT duration follows actual shared `READY_ALL`; no oscillator/counter timing approximation is used |
| 2.5 | Clock routing and oscillator decoupling | ➖ | Schematic provision is correct; PCB routing cannot be assessed from the uploaded files |

## 3. Host data-bus interface

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 3.1 | Host D0–D7 buffered before local fan-out | ✅ | U4 `74HCT541` buffers D0–D7 to local DB0–DB7; both enables are permanently active |
| 3.2 | No read-back contention | ✅ | Card is write-only; local DB bus is driven only from the host-side buffer and is not returned to pBITz |
| 3.3 | SN76489AN unusual data-bit convention handled | ✅ | Correct reversal: host DB7 → PSG D0/MSB, DB6 → D1 … DB0 → D7/LSB on all four PSGs |
| 3.4 | AD7801 data bus numbering | ✅ | DAC is conventional: DB7 → DB7/MSB through DB0 → DB0/LSB |
| 3.5 | Data stable for extended PSG write | ✅ | The Z80 retains address, data and write controls while `/WAIT` is low; U4 remains enabled throughout |

## 4. Device select and address decoding

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 4.1 | Card-level select generation | ✅ | U1 `74HC688` compares pBITz CS0–CS3 against the coded switch; comparator enable is grounded |
| 4.2 | Card address configurable | ✅ | SW1 coded switch with R1–R4 = 4.7 kΩ pull-ups |
| 4.3 | High comparator bits defined | ✅ | P4–P7 and R4–R7 are grounded; no floating comparator inputs |
| 4.4 | Local address decode | ✅ | U8 uses A2:A0, `/CS_SOUND` and `/WR`; only write cycles generate outputs |
| 4.5 | Local I/O map | ✅ | `000` PSG0, `001` PSG1, `010` PSG2, `011` PSG3, `100` PCM DAC; Y5–Y7 unused |
| 4.6 | Exactly one PSG selected per write | ✅ | 74x138 one-of-eight active-low decode guarantees mutual exclusion under stable inputs |
| 4.7 | Decoder speed in the WAIT assertion path | ⚠️ | **Pre-fab recommendation:** change U8 from `74HC138` to pin-compatible `SN74AHCT138` (preferred) or `SN74AHC138`. HC138 enable→Y is specified up to 39 ns at 4.5 V over temperature; AHCT138 is about 11 ns. The remaining path still includes U14, U5, Q1 and the bus pull-up/capacitance |

## 5. SN76489AN PSG subsystem

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 5.1 | Four independent PSGs present | ✅ | U6, U7, U11 and U12 are SN76489AN devices |
| 5.2 | Per-chip `/CE` decode | ✅ | U8 Y0–Y3 connect individually to `/PSG0`–`/PSG3` and each PSG pin 6 |
| 5.3 | `/WE` routed | ✅ | All four PSG pin-5 `/WE` inputs connect directly to pBITz `/WR` |
| 5.4 | CE/WE/data relationship | ✅ | TI specifies zero setup time for CE and data relative to WE; selected CE and WE remain asserted while WAIT stretches the cycle |
| 5.5 | READY outputs combined legally | ✅ | All four pin-4 READY outputs are open-collector and share `READY_ALL` |
| 5.6 | READY pull-up | ✅ | R15 = **2.2 kΩ** to +5 V; close to TI's characterized 2 kΩ test pull-up |
| 5.7 | Audio-output loading | ✅ | Each PSG audio output has a 1 kΩ load to ground before the stereo matrix |
| 5.8 | NC pins handled | ✅ | Pin 9 on each SN76489AN is intentionally NC |
| 5.9 | Power-on state handled in software | ⚠️ | SN76489AN has no external reset input in this design; boot software should explicitly mute/initialize all channels before enabling normal audio use |

## 6. READY-based WAIT-state generator

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 6.1 | Combined PSG-write indication | ✅ | U14A `74AHC20` NANDs `/PSG0`–`/PSG3`; `CS_SOUND` rises whenever any PSG CE falls |
| 6.2 | Immediate WAIT latch | ✅ | U5A `74AHCT74`: D and `/PRE` high; rising `CS_SOUND` sets Q → `WAIT_GATE` |
| 6.3 | Native completion detector | ✅ | U5B: D and `/PRE` high; `READY_ALL` rising clocks completion and drives `/FF2_Q` low |
| 6.4 | FF2 rearmed between cycles | ✅ | U5B `/CLR` is driven by active-high `CS_SOUND`; it is low while idle and released throughout the selected PSG write |
| 6.5 | FF1 clear equation | ✅ | U9A + U9C implement `/FF1_CLR = /RESET AND /FF2_Q`; either reset or completed READY handshake clears U5A |
| 6.6 | Shared-bus WAIT drive | ✅ | U5A Q drives Q1 gate; Q1 2N7002 pulls pBITz `/WAIT` low and otherwise disconnects from the line |
| 6.7 | No card-local `/WAIT` pull-up | ✅ | Correct for the shared pBITz line; motherboard supplies the pull-up |
| 6.8 | MOSFET default-off behavior | ✅ | R36 = 47 kΩ gate pulldown keeps Q1 off while U5 is absent, unpowered or starting |
| 6.9 | Reset behavior | ✅ | `/RESET` forces U5A clear and releases bus `/WAIT`; decode inactivity holds U5B cleared |
| 6.10 | State-machine deadlock / retrigger review | ✅ | READY low does not trigger U5B; READY rising clears FF1; the CPU then ends the write and falling `CS_SOUND` rearms FF2. No circular wait was found |
| 6.11 | READY edge as an AHCT clock | ⚠️ | First-article check: scope `READY_ALL` at U5 pin 11 and verify a clean monotonic rising edge. R15 = 2.2 kΩ is the right starting value; add Schmitt conditioning only if bench evidence shows edge/noise trouble |
| 6.12 | End-to-end assertion timing | ⚠️ | Scope `/WAIT` at the **CPU pin**, not only Q1. Stress all four PSG ports at 10 MHz. This bench item should close comfortably after the U8 AHCT138 change |

### WAIT transaction sequence

```text
Z80 write starts
    ↓
U8 asserts one /PSGn
    ↓
U14 raises CS_SOUND
    ↓
U5A sets WAIT_GATE
    ↓
Q1 pulls pBITz /WAIT low
    ↓
selected PSG pulls READY_ALL low and consumes the byte
    ↓
READY_ALL rises after the actual transfer completes
    ↓
U5B /Q falls; U9 clears U5A
    ↓
Q1 releases /WAIT
    ↓
Z80 completes the write; CS_SOUND falls and rearms U5B
```

## 7. AD7801 PCM DAC

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 7.1 | Supply and grounds correct | ✅ | VDD → +5 V; AGND and both DGND pins → GND |
| 7.2 | Data bus mapping | ✅ | DB7–DB0 connect straight to the DAC's DB7–DB0 inputs |
| 7.3 | Chip select and write | ✅ | `/PCM` from U8 Y4 drives CS; pBITz `/WR` drives WR |
| 7.4 | Automatic update mode | ✅ | LDAC is grounded; DAC output updates on the rising edge of WR |
| 7.5 | Reset / clear | ✅ | DAC `/CLR` connects to pBITz `/RESET` |
| 7.6 | Power-down disabled | ✅ | `/PD` tied high keeps the DAC active |
| 7.7 | Reference selection | ✅ | REFIN tied to VDD selects the AD7801 internal VDD/2 reference and gives a nominal 0–VDD output span |
| 7.8 | WAIT generator scope | ✅ | PCM decode is deliberately excluded from the PSG WAIT generator; AD7801 has a fast parallel microprocessor interface and does not require the 32-PSG-clock hold |
| 7.9 | PCM mixed to stereo | ✅ | DAC VOUT enters left and right summing nodes through R30/R35 = 47 kΩ |

## 8. Stereo summing and analogue filtering

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 8.1 | Midrail reference | ✅ | R21/R22 = 100 kΩ divider; C10 = 10 µF; VREF drives both TLV2372 non-inverting inputs |
| 8.2 | Mixer amplifier suitable for 5 V single supply | ✅ | TLV2372 is rail-to-rail input/output and supports 2.7–16 V operation |
| 8.3 | Left/right summing topology | ✅ | U2A/U2B are inverting summing amplifiers referenced to VREF |
| 8.4 | Feedback networks | ✅ | R23/R24 = 10 kΩ with C11/C12 = 1 nF in parallel for high-frequency roll-off/stability |
| 8.5 | PSG panning matrix | ✅ | U6/U7 centred at 33 kΩ/33 kΩ; U11 biased left 22 kΩ/47 kΩ; U12 biased right 47 kΩ/22 kΩ |
| 8.6 | PCM centre mix | ✅ | R30/R35 = 47 kΩ to left/right |
| 8.7 | DC isolation before volume controls | ✅ | C13/C14 = 10 µF couple the VREF-biased op-amp outputs into RV2/RV3 |
| 8.8 | Headroom / summed full-scale level | ⚠️ | Bench item: exercise all PSGs at maximum level plus PCM extrema and confirm U2 outputs do not clip. Adjust mixer resistor values if the intended simultaneous worst case needs more headroom |
| 8.9 | Noise and channel separation | ⚠️ | Bench item: measure idle noise, digital-clock feedthrough and left/right crosstalk; PCB placement/grounding is not available for review |

## 9. Output amplifier and connectors

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 9.1 | Stereo output amplifier present | ✅ | U10 = LM4880 dual Class-AB headphone/audio amplifier |
| 9.2 | Amplifier enabled | ✅ | U10 shutdown pin is grounded; LM4880 shutdown is active high |
| 9.3 | Bypass capacitor | ✅ | C15 = 100 nF on BYPASS |
| 9.4 | Input level controls | ✅ | RV2/RV3 provide independent left/right level trim after AC coupling |
| 9.5 | Output coupling | ✅ | C4/C9 = 220 µF between LM4880 outputs and external LEFT_OUT/RIGHT_OUT nets |
| 9.6 | Output bleeders | ✅ | R19/R20 = 100 kΩ to ground on external outputs |
| 9.7 | Connector mapping | ✅ | J4: pin 1 GND, pin 2 RIGHT_OUT, pin 5 LEFT_OUT; J5: right / ground / left |
| 9.8 | Mechanical jack pinout / capacitor polarity | ⚠️ | Confirm STX-3120-3B footprint numbering and polarized C4/C9 orientation during final PCB/Gerber review; no PCB was supplied here |
| 9.9 | Output-level and load test | ⚠️ | First article: verify headphone/line levels, THD, turn-on/off pop and thermal behavior into the intended load |

## 10. Reset and initialization

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 10.1 | pBITz `/RESET` reaches local circuits | ✅ | J1 B11 / CTRL_0 reaches AD7801 CLR and U9 WAIT-latch reset logic |
| 10.2 | WAIT cannot remain asserted during reset | ✅ | Reset forces U5A clear, turning Q1 off |
| 10.3 | PCM deterministic after reset | ✅ | AD7801 asynchronous CLR is driven by `/RESET`; AD7801 also includes power-on reset behavior |
| 10.4 | PSG initialization contract | ⚠️ | Firmware should explicitly program attenuation OFF on all four PSGs at boot because the PSGs have no reset connection |

## 11. pBITz bus interface

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 11.1 | Data bus mapping | ✅ | J1 B23–B30 = D0–D7 → U4 inputs |
| 11.2 | Local address lines | ✅ | J1 B32/B33/B34 = A0/A1/A2 → U8 |
| 11.3 | Card-select code | ✅ | CS0/1 on J1 B21/B22; CS2/3 on J1 A21/A22 → U1 |
| 11.4 | Write control | ✅ | J1 B13 / CTRL_2 → `/WR` |
| 11.5 | Reset control | ✅ | J1 B11 / CTRL_0 → `/RESET` |
| 11.6 | WAIT connection | ✅ | Q1 drain → J1 A13 / CTRL_12 `/WAIT`; motherboard pull-up assumed |
| 11.7 | Interrupt lines | ➖ | Card does not generate an interrupt; no interrupt circuitry required |
| 11.8 | Unused bus pins | ✅ | Unused pBITz functions intentionally left unconnected |

## 12. General and housekeeping

| # | Check | Status | Notes — current revision |
|---|---|---|---|
| 12.1 | No floating logic inputs | ✅ | U5 D/PRE inputs tied high; unused U9 and U14 gate inputs grounded; U8 active-high enable tied high; comparator high bits grounded |
| 12.2 | Unused outputs handled | ✅ | U8 Y5–Y7, unused U9/U14 outputs and unused U5 Q outputs are left NC intentionally |
| 12.3 | No output-to-output contention found | ✅ | Hierarchy-aware trace reported no nets with multiple push-pull outputs |
| 12.4 | 2N7002 symbol/footprint consistency | ✅ | Schematic mapping is gate 1, source 2, drain 3; verify the selected SOT-23 footprint uses the same convention |
| 12.5 | Component-value vs datasheet metadata | 🔧 | Several embedded datasheet URLs refer to sibling/older families rather than the displayed value: U14 points to LS20, U8 to HC238, U2 to TLV2375, U9 to HCT00 and U5 to HC/HCT74. Values/pinouts used in the design are coherent, but updating links will make later reviews safer |
| 12.6 | Native KiCad ERC / PCB DRC | ➖ | Not run in this environment. The supplied Python tracer is geometry/net based and does not interpret no-connect markers or expand bus syntax |

---

## Net-trace notes and limitations

The supplied hierarchy-aware tracer was used as the connectivity baseline. Its coverage report was:

```text
Device Select Decode      38 / 38   pin endpoints on wires
pBITz Bus Interface       60 / 125
Sound                    296 / 312
Total                     182 nets / 414 placed pins
Driver-driver conflicts   none
```

The low apparent pBITz-sheet percentage is primarily due to intentionally unused connector pins and the tracer's stated limitations. It also does not expand KiCad bus notation such as `DB[0..7]`; bus continuity and individual DB members were therefore checked manually in addition to the trace.

Power-symbol connections can appear as isolated unnamed pins in the raw tracer report. The schematic source was manually checked for the +5 V and GND connections used in this checklist.

---

## Required action before fabrication

### U8 timing-family change

Change:

```text
U8  74HC138
```

to:

```text
U8  SN74AHCT138D / SN74AHCT138PWR
```

or another pin-compatible AHCT138 part matching the footprint.

Reason:

```text
/WR
  → U8 decoder
  → U14 4-input NAND
  → U5A CLK→Q
  → Q1 2N7002
  → pBITz /WAIT RC path
  → Z80 /WAIT pin
```

TI specifies up to 39 ns for an HC138 enable-to-output transition at 4.5 V over temperature, before the remaining stages and bus settling are included. The AHCT138 reduces that decoder delay to roughly 11 ns and supplies TTL-compatible inputs. The change is pin-compatible and affects BOM/value only, not copper.

Once U8 is changed, the schematic review status becomes:

> **PASS for fabrication, with normal first-article timing and analogue-output validation.**

---

## First-article validation plan

1. Scope `/PSG0`–`/PSG3`, `CS_SOUND`, `WAIT_GATE`, `READY_ALL` and pBITz `/WAIT` for a write to each PSG.
2. Measure `/WAIT` at the **Z80 pin** and confirm it is low before the first relevant WAIT sample at 10 MHz.
3. Confirm READY goes low after CE, remains low for the transfer, and produces exactly one clean rising completion edge.
4. Hammer all four PSG ports with back-to-back writes; verify no lost writes and no permanent WAIT lock.
5. Verify reset always releases `/WAIT`.
6. Initialize all PSG channels muted, then test every tone/noise register path and the reversed data mapping.
7. Sweep PCM DAC code 00h–FFh and confirm monotonic 0–5 V nominal behavior before the mix resistors.
8. Run four PSGs at maximum level plus PCM full-scale patterns; inspect both TLV2372 outputs for clipping.
9. Measure left/right output level, frequency response, noise floor, crosstalk, pop behavior and LM4880 temperature into the intended headphones/load.

---

## Reference facts and primary sources

- **SN76489AN application manual:** D0 is the most-significant data bit; READY is open-collector; CE-to-READY-low is 90 ns typical / 150 ns maximum under the published test load; data loading takes approximately 32 PSG clocks; maximum input clock is 4 MHz.  
  <https://map.grauw.nl/resources/sound/texas_instruments_sn76489an.pdf>
- **SN74HC138:** enable-to-output delay is specified up to 39 ns at 4.5 V over the SN74 temperature range and 50 pF load.  
  <https://www.ti.com/lit/gpn/sn74hc138>
- **SN74AHCT138:** pin-compatible high-speed decoder with TTL-compatible inputs; approximately 11 ns maximum enable-to-output delay under the published conditions.  
  <https://www.ti.com/lit/gpn/sn74ahct138>
- **SN74AHCT74:** dual positive-edge-triggered D flip-flop with asynchronous clear/preset; approximately 10 ns maximum CLK-to-Q at 50 pF.  
  <https://www.ti.com/lit/gpn/sn74ahct74>
- **AD7801:** 2.7–5.5 V 8-bit DAC; data is loaded on the rising edge of CS or WR; LDAC low selects automatic update; REFIN tied to VDD selects the internal VDD/2 reference.  
  <https://www.analog.com/media/en/technical-documentation/data-sheets/AD7801.pdf>
- **TLV2372:** 2.7–16 V rail-to-rail input/output dual op-amp, 3 MHz bandwidth.  
  <https://www.ti.com/lit/ds/symlink/tlv2372.pdf>
- **LM4880:** dual 250 mW Class-AB audio power amplifier with active-high shutdown.  
  <https://www.ti.com/lit/ds/symlink/lm4880.pdf>
- **ECS-2100A-035:** 5 V, 3.579545 MHz HCMOS/TTL oscillator.  
  <https://www.digikey.ca/en/products/detail/ecs-inc/ECS-2100A-035/31990>

---

## Revision log

| Review rev | Date | Reviewer | Result | Key notes |
|---|---|---|---|---|
| 1 | 2026-07-29 | OpenAI GPT-5.6 Thinking | CONDITIONAL PASS | Four-PSG/PCM/audio topology and READY-based WAIT state machine validated. No net conflicts. Recommend U8 HC138 → AHCT138 before fab; remaining items are first-article timing and analogue measurements. |

> **Per-revision workflow:** copy this file → identify the schematic revision → rerun the hierarchy trace → recheck the WAIT assertion chain and analogue BOM → update changed rows and the revision log. Only ❌ items block a spin; ⚠️ items may be pre-fab recommendations or explicit first-article measurements as stated in the notes.
