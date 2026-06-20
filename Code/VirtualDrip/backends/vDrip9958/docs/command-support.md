# Command Support

Status legend: **ST** = implemented + smoke-tested, **MR** = implemented +
manually reviewed. Commands are started by writing R#46; CE/TR live in S#2
(D0/D7). One `vDrip9958StepCommand()` performs one natural unit. Functional
timing only — there is no VDP cycle clock.

## Commands

| Code | Command | Natural unit | Status | Evidence |
|---|---|---|---|---|
| 0xF | HMMC | packed byte (CPU → VRAM via R#44/TR) | ST | `test_commands` HMMC |
| 0xE | YMMM | packed byte (VRAM row copy) | MR | rectangle handlers |
| 0xD | HMMM | packed byte (VRAM → VRAM) | MR | rectangle handlers |
| 0xC | HMMV | packed byte fill | ST | `test_commands` HMMV |
| 0xB | LMMC | pixel (CPU → VRAM) | MR | rectangle handlers |
| 0xA | LMCM | pixel (VRAM → CPU via S#7/TR) | ST | `test_commands` LMCM |
| 0x9 | LMMM | pixel (VRAM → VRAM, logical op) | MR | rectangle handlers |
| 0x8 | LMMV | pixel fill (logical op) | ST | `test_commands` LMMV |
| 0x7 | LINE | one plotted point (Bresenham) | ST | `test_commands` LINE |
| 0x6 | SRCH | one compared point (BD, S#8/S#9) | ST | `test_commands` SRCH |
| 0x5 | PSET | one point, complete | ST | `test_commands` PSET |
| 0x4 | POINT | read point → S#7, complete | ST | `test_commands` POINT |
| 0x0 | STOP | abort active command | ST | `test_commands` STOP |
| 0x1–0x3 | reserved | inert completion | MR | start path |

## CE/TR and transfers

| Aspect | Behavior | Status | Evidence |
|---|---|---|---|
| CE / TR | S#2 D0 / D7; set at start, cleared at completion | ST | `test_commands` |
| CPU input | HMMC/LMMC: R#44 first value, then TR + R#44 handshake | ST | `test_commands` HMMC |
| CPU output | LMCM: S#7 publish, S#7 read clears TR; final value persists | ST | `test_commands` LMCM |
| Replacement / STOP | New R#46 or STOP cancels active command | ST | `test_commands` STOP |

## Logical operations and mode

| Aspect | Behavior | Status | Evidence |
|---|---|---|---|
| Logical ops | IMP/AND/OR/XOR/NOT + transparent forms; reserved = no-op | ST | `test_commands` transparent |
| Mode surface | Native G4–G7 packing/masks | ST | `test_commands` (G4) |
| CMD expansion | R#25 CMD → Graphic 7 interpretation in other modes | ST | `test_commands` CMD-expanded |
| Expansion VRAM | Absent: reads 0 / writes discarded, progress deterministic | MR | access helpers |

See [deviations.md](deviations.md) for overlap exactness, the partial
post-command register table, and ARG/SRCH detail notes.
