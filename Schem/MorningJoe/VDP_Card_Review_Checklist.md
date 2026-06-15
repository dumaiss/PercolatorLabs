# pBITz VDP Video Card — Schematic Review Checklist

Reusable per-revision review template for TMS9928A / V9958-class video cards on the pBITz backplane.
Phrasing in the **Check** column is generic (applies to any VDP card / future rev); the **Status / Notes** columns are filled for the current revision under review.

---

## Board under review

| Field | Value |
|---|---|
| Board name | **MorningJoe** (TMS9928A component-video card) |
| Platform | pBITz / Coffee Series (Zephyr-80 host) |
| Revision | **Rev 1** |
| Schematic source | `MorningJoe.kicad_sch` + `Video` / `DeviceSelectDecode` / `pBITzBusInterface` sheets (KiCad 10, fmt 20260306) |
| Review method | Full S-expression net trace (pin-transform calibrated, 270/290 pin-endpoint hits) |
| Review date | 2026-06-15 |
| Net result | **PASS** — no hard bugs; all open items confirmed intentional |

**Status legend:** ✅ pass · ❌ fail · ⚠️ verify / open · ➖ n/a · 🔧 optional/SI

---

## 1. Power & decoupling

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 1.1 | Single supply rail confirmed — no stray −5V/+12V nets | ✅ | Only `+5V` / `GND` symbols on every sheet. The −5V/+12V triple-rail need was always the **4116 DRAM**, never the VDP. |
| 1.2 | VDP Vcc/Vss on correct rails | ✅ | pin 33 → +5V, pin 12 → GND |
| 1.3 | All VRAM Vcc/Vss on correct rails | ✅ | 8× MK4164N, every Vcc(8)→+5V, Vss(16)→GND |
| 1.4 | Per-IC decoupling (≈0.1µF each) | ✅ | 100nF distributed across all ICs |
| 1.5 | Bulk decoupling present | ✅ | 220µF on video rail + 10µF; caps on decode sheet |
| 1.6 | Separate filtered analog supply (if applicable) | ➖ | Single-rail design; THS7314 runs off the common +5V |

## 2. Clock / oscillator

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 2.1 | Crystal frequency correct for VDP variant | ✅ | Y1 = MP107-E = **10.738635 MHz** (3× NTSC colorburst). *(V9958 variant would be 21.477 MHz.)* |
| 2.2 | Crystal across XTAL1 / XTAL2 | ✅ | pins 40 / 39 |
| 2.3 | Two load caps to GND | ✅ | C2 / C5 = **47pF** each |
| 2.4 | Load cap value vs crystal CL spec | ✅ | CL = C/2 + C_stray = 23.5 + ~8 ≈ **32pF** ✓ (matches MP107-E 32pF on a 4L board). Validate empirically via CPUCLK÷ tap vs 3.579545 MHz if needed. |
| 2.5 | GROMCLK / CPUCLK handled (NC if unused) | ✅ | GROMCLK (37) → NC |

## 3. CPU (host) interface

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 3.1 | Host data bus bit-order convention resolved | ✅ | Symbol renames pins to **Z80 numbering** (pin 17 = "CD0" = LSB → D0). Deliberate escape from TI's CD0-is-MSB convention; value alignment LSB↔LSB / MSB↔MSB. |
| 3.2 | MODE → register/data select | ✅ | pin 13 → A0 |
| 3.3 | /CSR generated correctly | ✅ | U1a (139): CS_VDP & /RD |
| 3.4 | /CSW generated correctly | ✅ | U1b (139): CS_VDP & /WR & (A1=0) |
| 3.5 | R/W (or /RD,/WR) to VDP | ✅ | pin 11 R/W̅ driven |

## 4. VRAM (DRAM path)

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 4.1 | DRAM single-supply type | ✅ | MK4164N (single +5V, successor to triple-rail 4116) |
| 4.2 | Chip count matches data width | ✅ | 8× 1-bit = 8-bit VRAM (U7/8/9/10/12/13/14/15) |
| 4.3 | Address mapping AD→A correct | ✅ | 7-bit row/col on **AD1–AD7** → A0–A6 (scrambled order, harmless for DRAM). AD0 = write-data only, not an address bit. |
| 4.4 | Extra high address line tied off (4164 A7) | ✅ | **A7 (pin 9) → GND on all 8** → constrains to 128 rows × 128 cols = 16KB |
| 4.5 | Refresh coverage (used rows ⊆ refreshed rows) | ✅ | A7=GND keeps usage to 128 rows; VDP's native 4116-style 7-bit refresh covers all used rows |
| 4.6 | Write-data path AD(N) → DIN(N) | ✅ | Clean, one bit per chip |
| 4.7 | Read-data path DOUT(N) → RD(N) | ✅ | Self-consistent, no byte reversal |
| 4.8 | RAS / CAS / WE common to all chips | ✅ | Shared; R/W̅ → /WRITE |
| 4.9 | Address latch / CAS-delay hack | ➖ | Not needed — native DRAM latches row/col internally (SRAM-adapter problem eliminated) |
| 4.10 | RAS/CAS fan-out damping | 🔧 | No series damping present on 8-chip RAS/CAS net. Acceptable at this geometry; add ~22–33Ω if scope shows ringing at DRAM inputs. |

## 5. Interrupt

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 5.1 | /INT pulled up | ✅ | R2 4K7 → +5V (local), bus line also pulled up on backplane |
| 5.2 | Interrupt reaches CPU/bus | ✅ | Routed via demux + diode-OR to bus /INT or /NMI |
| 5.3 | Diode/open-drain isolation for shared bus | ✅ | U4 BAT54 wired-OR; Vf ≈0.3–0.4V < Z80 VIL 0.8V |
| 5.4 | INT/NMI select logic (if present) | ✅ | U3 (273) latches D0 on /INT_MODE strobe (reset by /RESET) → U5 (1G19) demux enabled by /INT → software-selectable INT vs NMI |

## 6. Reset

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 6.1 | VDP reset input driven | ✅ | pin 34 (RESET̅/SYNC) → /RESET |
| 6.2 | Local latches reset with system reset | ✅ | 273 /MR → /RESET |
| 6.3 | /SYNC genlock handled | ➖ | Not used (pin 34 used as reset input only) |

## 7. Video output

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 7.1 | Output type matches intent | ✅ | Component Y / R-Y / B-Y (TMS9928A) |
| 7.2 | Output buffer/amp present | ✅ | U6 THS7314 (3-ch video amp) |
| 7.3 | Source termination (75Ω series) | ✅ | R6 / R7 / R8 |
| 7.4 | AC coupling on outputs | ✅ | C15 / C16 / C17 = 220µF; inputs also AC-coupled (C12/13/14) |
| 7.5 | Output pulldown/bias resistors | ✅ | R3 / R4 / R5 = 470Ω |
| 7.6 | Level-adjust pots if required | ✅ | RV1 (R-Y), RV2 (B-Y) |
| 7.7 | Connector pinout matches cable/display | ✅ | Component over DE15 (Y=pin2, R-Y=pin1, B-Y=pin3) via standard DE15→component dongle |
| 7.8 | Sync handling | ✅ | Sync-on-Y; HSync/VSync (pins 13/14) intentionally NC |

## 8. Device select / addressing

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 8.1 | Card chip-select generation | ✅ | U11 74HC688 compares backplane CS0–3 vs switch code → /CS_VDP |
| 8.2 | Card address configurable | ✅ | SW1 SH-7070 coded switch |
| 8.3 | Pull-ups on switch / compare inputs | ✅ | R9–R12 4K7 |

## 9. Bus / backplane interface

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 9.1 | Data bus D0–D7 mapped | ✅ | J5 B23–B30 |
| 9.2 | Used address lines mapped | ✅ | A0 (B32), A1 (B33) |
| 9.3 | Control lines /RD /WR /RESET mapped | ✅ | CTRL_1/2/0 |
| 9.4 | Interrupt lines /INT /NMI mapped | ✅ | CTRL_14 / CTRL_15 |
| 9.5 | Unused bus pins intentionally NC | ✅ | 64 NCs = D8–D15, A2–A23, CLK/SPI/cart/+3V3 — all unused-by-8-bit-card features |
| 9.6 | Bus pull-up / termination location confirmed | ✅ | Control lines (incl. /INT, /NMI) pulled up on backplane; no per-card termination needed at this geometry |

## 10. General / housekeeping

| # | Check | Status | Notes (Rev 1) |
|---|---|---|---|
| 10.1 | No floating logic inputs | ✅ | 273 unused D inputs → GND; unused decoder outputs NC |
| 10.2 | No-connect markers all intentional | ✅ | 28 (Video) + 64 (bus) reviewed, all legit |
| 10.3 | Reference / value sanity | 🔧 | Cosmetic: U1 value field "CD74HC139M96" against `74LS139` library symbol — electrically a '139 |

---

## Reference facts (verified, with sources)

These are the spec anchors the checklist leans on; re-verify only if a part changes.

- **VDP is single +5V** (Vcc pin 33 / Vss pin 12). The −5V/+12V triple rail belongs to the **4116 DRAM**, not the VDP — Wikipedia *TMS9918* <https://en.wikipedia.org/wiki/TMS9918>; Hackaday *TMS9918-SRAM* <https://hackaday.io/project/160851-tms9918-vdp-with-sram-video-memory>; DigicoolThings <https://digicoolthings.com/tms9929a-vdp-rediscovery-and-alternative-vram-solution/>
- **7-bit row + 7-bit column address on AD1–AD7**; AD0 not an address bit; native 4164 viable (separate DIN/DOUT, no tristate buffer needed) — RC2014 thread <https://groups.google.com/g/rc2014-z80/c/XhnmGEvABTE>; Nials Moseley *SRAM Replacement for TMS99x8 VRAM* <https://cdn.hackaday.io/files/5789247676576/9918-SRAM.pdf>
- **TI CD0-is-MSB data-bus convention** (the bit-reversal lore the Z80-numbered symbol escapes) — <https://leonardomiliani.com/?p=1631&lang=en>; pinout (pin 17 = CD7) <http://bifi.msxnet.org/msxnet/tech/tms9918a.txt>
- **MP107-E = CTS 10.738635 MHz, 32pF, for TMS9918/9928** (verified as of 2026-06-14) — DigiKey <https://www.digikey.co.uk/en/products/detail/cts-frequency-controls/MP107-E/2637557>; Richardson RFPD <https://shop.richardsonrfpd.com/Products/Product/MP107-E>

### Load-cap formula (for §2.4 on any rev)

```
CL = (Ca·Cb)/(Ca+Cb) + C_stray = C/2 + C_stray      (equal caps)
C_stray_required(for CL target) = CL − C/2
```

For a 32pF crystal: 47pF→needs 8.5pF stray (good on 4L), 33pF→15.5pF, 27pF→18.5pF (too light), 56pF→4pF.

---

## Revision log

| Rev | Date | Reviewer | Result | Key changes from prior rev |
|---|---|---|---|---|
| 1 | 2026-06-15 | Claude (net-trace review) | PASS | Baseline. Single-rail + native 4164 DRAM (A7→GND), Z80-numbered data bus, sw-selectable INT/NMI, component-over-DE15. |
| 2 |  |  |  |  |
| 3 |  |  |  |  |

> **Per-rev workflow:** copy this file → bump the Board block → re-run the trace → flip any changed rows to ⚠️/❌ and annotate → add a revision-log line. Items marked ➖/🔧 are design choices, not failures; only ❌ blocks a spin.
