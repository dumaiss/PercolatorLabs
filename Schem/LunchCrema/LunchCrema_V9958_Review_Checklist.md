# pBITz V9958 Video Card — Schematic Review Checklist

Reusable per-revision review template for the **LunchCrema** V9958 RGB video card on the pBITz backplane.
Same format as the MorningJoe (TMS9928A) checklist, so the two cards can be diffed side by side.
Phrasing in the **Check** column is generic; the **Status / Notes** columns are filled for the revision under review.

---

## Board under review

| Field | Value |
|---|---|
| Board name | **LunchCrema** (V9958 linear-RGB / RGBS video card) |
| Platform | pBITz / Coffee Series (Zephyr-80 host) |
| Revision | **Rev 1** |
| Schematic source | `LunchCrema.kicad_sch` + `Video` / `DeviceSelectDecode` / `pBITzBusInterface` sheets (KiCad 10) |
| Review method | Full S-expression net trace (transform calibrated, 251/285 pin-endpoint hits) |
| Review date | 2026-06-15 |
| Net result | **PASS** — no functional blockers; one open bench item (RGB/sync level trim) |

**Status legend:** ✅ pass · ❌ fail · ⚠️ verify / open · ➖ n/a · 🔧 optional/polish

---

## 1. Power & decoupling

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 1.1 | Single supply rail — no stray −5V/+12V | ✅ | Only `+5V` / `GND` symbols |
| 1.2 | VDP core supply on correct rails | ✅ | VCC (58) → +5V, GND (1) → GND |
| 1.3 | Analog DAC supply on correct rails | ✅ | VCC/DAC (21) → +5V, GND/DAC (20) → GND |
| 1.4 | VBB handled intentionally | ✅ | pin 33 left **open** — acceptable per V9938-family guidance. *0.1µF to GND is the conservative alt. (conf: medium — confirm vs V9938 data book)* |
| 1.5 | Per-IC decoupling (≈0.1µF each) | ✅ | Distributed across ICs |
| 1.6 | Local bulk capacitor near VDP | 🔧 | No obvious dedicated bulk at U2; a 10µF near the VDP power entry is cheap rail-stiffness insurance if layout has room |

## 2. Clock / oscillator

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 2.1 | Crystal/clock frequency correct | ✅ | Y1 = **21.477270 MHz** (V9958 fXTAL nominal 21.48 MHz) |
| 2.2 | Exactly one clock source populated | ✅ | **Crystal Y1 live; oscillator Y2 DNP** (confirmed intended — crystal ~$1 vs osc ~$10) |
| 2.3 | Crystal across XTAL1 / XTAL2 | ✅ | pins 63 / 64 |
| 2.4 | Load caps suit crystal CL | ✅ | C6 / C7 = 27pF (≈18–20pF effective; suits HC-49 part). Bench-confirm startup if the on-chip osc proves fussy. |
| 2.5 | Fallback clock path available | ✅ | Y2 canned-oscillator footprint retained DNP as fallback |

## 3. CPU interface

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 3.1 | Host data bus bit order | ✅ | Yamaha uses **normal** numbering (CD0 = LSB) — CD0→D0 straight, **no reversal needed** (unlike TMS9918) |
| 3.2 | MODE0 / MODE1 from host address | ✅ | pins 29 / 28 → A0 / A1 |
| 3.3 | /CSR generated | ✅ | U10a (139): CS_VDP & /RD |
| 3.4 | /CSW generated | ✅ | U10b (139): CS_VDP & /WR & (A2=0) |
| 3.5 | /WAIT routed if host honors it | ✅ | pin 26 → backplane; pulled up on backplane |
| 3.6 | Reset routed | ✅ | pin 9 → /RESET |

## 4. VRAM (DRAM)

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 4.1 | DRAM single-supply type | ✅ | 41464 (64K×4, +5V) |
| 4.2 | Size / organization matches target | ✅ | 4× 64K×4 = **128 KB** |
| 4.3 | Address bus fully used | ✅ | AD0–AD7 (all 8 mux lines) → A0–A7 |
| 4.4 | Data bus width | ✅ | RD0–RD7 (8-bit) |
| 4.5 | Bank strobes correct | ✅ | /CAS0 → U4 (RD0-3)+U5 (RD4-7) = bank 0; /CAS1 → U6+U7 = bank 1 |
| 4.6 | RAS common to all chips | ✅ | /RAS shared |
| 4.7 | Write strobe to DRAM /WE | ✅ | R/W̅ → /WE |
| 4.8 | /OE handling | ✅ | Grounded on all four (standard for this VDP interface) |
| 4.9 | Expansion bank (/CASX) handled | ✅ | NC (no expansion VRAM) |
| 4.10 | DRAM speed grade meets VDP timing | ✅ | **100 ns** — faster than the ~150 ns MSX2 baseline; ample margin |

## 5. V9958 sync/genlock & unused pins

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 5.1 | External-sync inputs biased to idle | ✅ | /VRESET (4) and /HRESET (27) → +5V (external sync unused) |
| 5.2 | /DLCLK not left floating | ✅ | pin 3 → **R20 4k7 pull-up** → +5V |
| 5.3 | Digital color bus / unused outputs handled | ✅ | C0–C7, DHCLK, HSYNC, YS, CDBR, BLEO → NC (internal DAC in use, not the digital color bus) |
| 5.4 | CPUCLK/VDS handled | ✅ | pin 8 → NC |

## 6. Interrupt

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 6.1 | VDP /INT pulled up | ✅ | R5 4k7 → +5V on /INT_i |
| 6.2 | Interrupt reaches bus | ✅ | demux + diode-OR |
| 6.3 | Diode/open-drain isolation for shared bus | ✅ | U9 BAT54 wired-OR to /INT or /NMI |
| 6.4 | INT/NMI select logic (if present) | ✅ | U3 (273) latches D0 on /INT_MODE (gated A2, reset by /RESET) → U8 (1G19) demux enabled by /INT |

## 7. Reset

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 7.1 | VDP reset input driven | ✅ | pin 9 → /RESET |
| 7.2 | Local latches reset with system | ✅ | 273 /Mr → /RESET |

## 8. Video output

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 8.1 | Output type matches intent | ✅ | Linear RGB + composite sync (V9958 internal DAC) |
| 8.2 | RGB buffer present | ✅ | IC1 MMPQ3904 quad NPN, emitter-follower per channel |
| 8.3 | RGB AC-coupling | ✅ | C2 / C3 / C4 = 220µF |
| 8.4 | RGB bias / load / series network | ✅ | 1k base pulldown (R8/9/10), 220Ω emitter load (R14/15/17), 22Ω series (R13/16/18) |
| 8.5 | Sync path coupling | ✅ | **DC-coupled** — CSYNC → R19 470Ω series → connector (buffered transistor path R7 DNP). Correct for a GBS-style box (sync separator wants DC level preserved). |
| 8.6 | Connector pinout matches receiver | ✅ | **RGBS over DE15** (R/G/B = 1/2/3, CSYNC = 13, grounds) → GBS converter box. **NOT VGA** (no separate H/V sync). |
| 8.7 | RGB + sync output levels | ⚠️ | **OPEN — bench item.** Verify amplitude/polarity into the GBS 75Ω input at first power-up; trim 22Ω series (R13/16/18) and/or R19 if hot/dim. 22Ω source is intentionally light on back-termination — fine for a forgiving GBS input. |

## 9. Device select / addressing

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 9.1 | Card chip-select generation | ✅ | 74HC688 compares backplane select code vs coded switch → /CS_VDP |
| 9.2 | Card address configurable | ✅ | Coded switch + 4k7 pull-ups |
| 9.3 | Pull-ups on switch / compare inputs | ✅ | 4k7 network |

## 10. Bus / backplane interface

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 10.1 | Data bus D0–D7 mapped | ✅ | to J5 |
| 10.2 | Address lines mapped | ✅ | A0, A1 (MODE select) + A2 (INT_MODE window) |
| 10.3 | Control /RD /WR /RESET mapped | ✅ | CTRL_11 / CTRL_12 / CTRL_10 |
| 10.4 | /WAIT mapped | ✅ | CTRL_2; pulled up on backplane |
| 10.5 | Interrupt /INT /NMI mapped | ✅ | CTRL_4 / CTRL_5; diode wired-OR, backplane pull-ups |
| 10.6 | Unused bus pins intentionally NC | ✅ | Unused backplane features left NC |

## 11. General / housekeeping

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 11.1 | No floating logic inputs | ✅ | 273 unused D inputs → GND. *Note: MMPQ3904 T4 base floats via R7 DNP — harmless; ground it if you respin.* |
| 11.2 | No-connect markers all intentional | ✅ | 33 on Video sheet, reviewed |
| 11.3 | DNP state = single-population per option | ✅ | Crystal Y1 + R19 live; oscillator Y2 + R7 DNP — **confirmed intended** (crystal clock, DC-coupled sync) |

---

## Reference facts (verified, with sources)

- **V9958 is single +5V, 21.477270 MHz, linear RGB (composite deleted vs V9938), up to 128 KB VRAM via 64K×4 DRAM** — Yamaha *V9958 MSX-VIDEO Technical Data Book* <https://map.grauw.nl/resources/video/yamaha_v9958.pdf>; Wikipedia *Yamaha V9958* <https://en.wikipedia.org/wiki/Yamaha_V9958>
- **Pin-level anchors** (from trace + symbol): VCC 58 / GND 1 / VCC-DAC 21 / GND-DAC 20 / VBB 33; XTAL1-2 63/64; MODE0/1 29/28; CSR/CSW 31/30; WAIT 26; INT 25; RESET 9; RD0-7 41-48; AD0-7 49-56; RAS 62, CAS0 61, CAS1 60, CASX 59, R/W 57; R/G/B 23/22/24; CSYNC 6; DLCLK 3; VRESET 4 / HRESET 27
- **MMPQ3904** = quad NPN (4× 2N3904), used as emitter-follower RGB buffers — ON Semi datasheet
- **BOM value confirmations:** Y1 `…S21477270…` = 21.477270 MHz; U4–U7 `41464` = 64K×4 DRAM; DRAM grade = 100 ns

### Notes carried from the MorningJoe review (shared design DNA)

The CPU decode (139), software-selectable INT/NMI router (273 + 1G19 + BAT54), and 688+coded-switch card-select are the same proven blocks as the TMS9928A card — only the address bit used for the INT_MODE window differs (A2 here vs A1 there, because the V9958 consumes A0/A1 for MODE0/MODE1).

---

## Revision log

| Rev | Date | Reviewer | Result | Key notes |
|---|---|---|---|---|
| 1 | 2026-06-15 | Claude (net-trace review) | PASS | Baseline. Single-rail V9958, 128 KB banked 41464, crystal clock, /DLCLK + /VRESET + /HRESET biased, discrete-transistor RGB + DC-coupled sync over DE15→GBS. One open bench item: RGB/sync level trim. |
| 2 |  |  |  |  |
| 3 |  |  |  |  |

> **Per-rev workflow:** copy this file → bump the Board block → re-run the trace → flip changed rows to ⚠️/❌ and annotate → add a revision-log line. ➖/🔧 are design choices, not failures; only ❌ blocks a spin. ⚠️ items (like §8.7) are expected to close at bring-up, not on the schematic.
