# Percolator Labs

**Homebrew Multimedia Cards for Retro Coffee-Themed Computers**


## About Percolator Labs

Percolator Labs is a collection of custom multimedia expansion cards (graphics and audio) designed for the **pBITz** series of homebrew retro computers.

Just like a perfect cup of coffee, these cards deliver rich visuals, deep sound, and satisfying performance — freshly brewed on the hardware bus.

**Tagline:**  
**"Brewed Pixels. Brewed Beats. One Perfect Cup."**

## Philosophy

- **Thematic Consistency**: Every card name follows a coffee hierarchy, from simple drip to complex crafted experiences.
- **Progressive Enhancement**: Cards scale in capability matching the host machines — from Z80 classics to 68k powerhouses.
- **Homebrew Spirit**: Designed for fun, experimentation, and maximum retro enjoyment.

---

These cards are designed specifically for the **pBITz** series of coffee-themed homebrew machines.

## Percolator Pixel — Graphics Cards

**Series Tagline:** "Freshly Rendered Graphics"

| Card | Name                        | Hardware Highlights                                      | Recommended For          | Tagline                                      |
|------|-----------------------------|----------------------------------------------------------|--------------------------|----------------------------------------------|
| 0    | **Virtual Drip**            |  Virtual TMS9928A via serial + RFB server                | Development / All        | "All the flavor, zero hardware."             |
| 1    | **Morning Joe**             | TMS9928A base implementation                             | Zephyr-80                | "The honest classic. No froth, just pure video." |
| 2    | **Lunch Crema**             | V9958 + 192KiB RAM                                       | Zephyr-80 / Espresso-09  | "Rich. Smooth. Upgraded."                    |
| 3    | **Afternoon Latte**         | Custom tile engine, 512KiB RAM, RX660 coprocessor, HD6445 CRTC, 640×480 256c | Espresso-09     | "Precision-crafted visuals."                 |
| 4    | **Nightly Double Shot**     | Framebuffer VDP, 2MiB RAM, RX660 coprocessor, HD63484 ACRTC, 1024×768 256c | Ristretto-68 / Nitro-30 | "Heavy caffeine for your framebuffer."       |

## Percolator Audio — Sound Cards

**Series Tagline:** "Brewed Beats"

| Card | Name                   | Hardware Highlights                                           | Recommended For          | Tagline                                           |
|------|------------------------|---------------------------------------------------------------|--------------------------|---------------------------------------------------|
| 0    | **Virtual Drip**       | Virtual SN76489 (3 voices + noise) via serial proxy          | Development / All        | "All the tone, zero silicon."                     |
| 1    | **Early Roast**        | 4× real SN76489 chips + PCM channel                          | Zephyr-80 / Espresso-09  | "Four classic chips, one pure shot."              |
| 2    | **Midday Blend**       | Yamaha real chip?                                            | Mid-range machines       | "Thick, rich, and layered."                       |
| 3    | **Twilight Mocha Mix** | Coprocessor-backed engine: up to 8 channels / 3-op FM + PCM  | Ristretto-68 / Nitro-30  | "Coprocessor-crafted audio art."                  |

## Roadmap

- **Phase 1 (Current)**: Core card designs (Virtual Drip, Original Joe, Crema/Latte Art, Double Shot / Quad Crema / Latte Symphony)
- **Phase 2**: Complete schematics, KiCad files, and firmware for all cards
- **Phase 3**: Driver development for Nitros-9, custom OS on 68k machines, and demo software
- **Phase 4**: Full documentation, programming manuals, and artwork (stickers, faceplates)
- **Phase 5**: Community releases, possible physical runs, and expansion ideas (e.g. higher-res cards, MIDI, etc.)

## Project Goals

- Create a cohesive multimedia ecosystem for homebrew 8/16-bit machines.
- Maintain strong coffee + retro computing theming.
- Provide high-quality, well-documented hardware and software interfaces.
- Encourage community contributions and forks.

## Repository Structure (Planned)

```
Percolator-Labs/
├── docs/                  # Schematics, manuals, programming guides
├── hardware/
│   ├── pixel/             # Graphics card designs
│   └── audio/             # Sound card designs
├── firmware/              # FPGA code, drivers
├── software/              # OS drivers (Nitros-9, custom OS, etc.)
├── artwork/               # Logos, stickers, silkscreens
└── README.md
```

## Getting Started

1. Browse the `hardware/` directory for schematics and KiCad files.
2. Check `docs/` for card-specific programming manuals.
3. Join the discussion on your favorite retro forum (or open an Issue here).

## License

This is a homebrew / open hardware project. All designs are released under the **Creative Commons Attribution-ShareAlike 4.0** license unless otherwise noted.

---

**Brewed with passion in the home lab.**  
☕ **Percolator Labs** — Making retro computing taste better since 2026.

---

*Last updated: June 2026*  
*Made for the pBITz family of homebrew machines.*
