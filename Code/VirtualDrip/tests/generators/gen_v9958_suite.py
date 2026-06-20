"""V9958 test suite — equivalent of the TMS9918 test generators.

Uses correct V9958 register bit layout:
  Mode bits: M1=R#1D4, M2=R#1D3, M3=R#0D1, M4=R#0D2, M5=R#0D3
  BL (blank): R#1D6 = 0x40 (1 = unblanked)
"""

from vdrip_packets import (
    PACKET_VDP_CTRL_WRITE, PACKET_VDP_DATA_WRITE, PACKET_FRAME_MARK,
    PACKET_VDP_PALETTE_WRITE,
    packet, reset, output_path, write_packet_file,
    frame_mark,
    box_pattern, sprite_cross_pattern, sprite_diamond_pattern,
)

# ---------------------------------------------------------------------------
# V9958 register helpers
# ---------------------------------------------------------------------------

def vdp_set_register(reg, value):
    return [
        packet(PACKET_VDP_CTRL_WRITE, bytes([value & 0xFF])),
        packet(PACKET_VDP_CTRL_WRITE, bytes([0x80 | (reg & 0x3F)])),
    ]

def vdp_set_write_address(address):
    address &= 0x1FFFF
    pkts = [
        packet(PACKET_VDP_CTRL_WRITE, bytes([address & 0xFF])),
        packet(PACKET_VDP_CTRL_WRITE, bytes([0x40 | ((address >> 8) & 0x3F)])),
    ]
    if address > 0x3FFF:
        pkts.append(packet(PACKET_VDP_CTRL_WRITE, bytes([(address >> 14) & 0x07])))
    return pkts

def vdp_write_bytes(address, data):
    pkts = vdp_set_write_address(address)
    for b in data:
        pkts.append(packet(PACKET_VDP_DATA_WRITE, bytes([b])))
    return pkts

def sprite_attributes(entries):
    """Encode sprite attribute entries with V9958 terminator."""
    data = bytearray()
    for y, x, pattern_index, color in entries:
        data.extend([y & 0xFF, x & 0xFF, pattern_index & 0xFF, color & 0x0F])
    data.extend([0xD8, 0x00, 0x00, 0x00])  # V9958 terminator (Y=216)
    return bytes(data)

# ---------------------------------------------------------------------------
# V9958 mode register presets
#   R#0: M3(D1), M4(D2), M5(D3)
#   R#1: BL(D6=0x40), M1(D4=0x10), M2(D3=0x08)
# ---------------------------------------------------------------------------

def v9958_graphic1_registers(backdrop=0x04):
    """Graphic 1: bits=0x00. R#0=0x00, R#1=0x40 (BL=1)."""
    pkts = []
    pkts += vdp_set_register(0, 0x00)
    pkts += vdp_set_register(1, 0x40)  # BL=1, M1=0, M2=0
    pkts += vdp_set_register(2, 0x3800 >> 10)   # name table 0x3800
    pkts += vdp_set_register(3, 0x2000 >> 6)    # color table 0x2000
    pkts += vdp_set_register(4, 0x0000 >> 11)   # pattern table 0x0000
    pkts += vdp_set_register(5, 0x3B00 >> 7)    # sprite attr 0x3B00
    pkts += vdp_set_register(6, 0x1800 >> 11)   # sprite pattern 0x1800
    pkts += vdp_set_register(7, ((0x0F << 4) | (backdrop & 0x0F)))
    return pkts

def v9958_text1_registers(backdrop=0x04):
    """Text 1: bits=0x01. R#0=0x00, R#1=0x50 (BL=1, M1=1)."""
    pkts = []
    pkts += vdp_set_register(0, 0x00)
    pkts += vdp_set_register(1, 0x50)  # BL=1, M1=1
    pkts += vdp_set_register(2, 0x0800 >> 10)   # name table 0x0800
    pkts += vdp_set_register(4, 0x0000 >> 11)   # pattern table 0x0000
    pkts += vdp_set_register(7, ((0x0F << 4) | (backdrop & 0x0F)))
    return pkts

def v9958_multicolor_registers(backdrop=0x04):
    """Multicolor: bits=0x02. R#0=0x00, R#1=0x48 (BL=1, M2=1)."""
    pkts = []
    pkts += vdp_set_register(0, 0x00)
    pkts += vdp_set_register(1, 0x48)  # BL=1, M2=1
    pkts += vdp_set_register(2, 0x3800 >> 10)
    pkts += vdp_set_register(3, 0x2000 >> 6)
    pkts += vdp_set_register(4, 0x0000 >> 11)
    pkts += vdp_set_register(5, 0x3B00 >> 7)
    pkts += vdp_set_register(6, 0x1800 >> 11)
    pkts += vdp_set_register(7, ((0x0F << 4) | (backdrop & 0x0F)))
    return pkts

def v9958_graphic2_registers(backdrop=0x04):
    """Graphic 2: bits=0x04. R#0=0x02 (M3=1), R#1=0x40."""
    pkts = []
    pkts += vdp_set_register(0, 0x02)  # M3=1
    pkts += vdp_set_register(1, 0x40)  # BL=1
    pkts += vdp_set_register(2, 0x3800 >> 10)
    pkts += vdp_set_register(3, 0x2000 >> 6)
    pkts += vdp_set_register(4, 0x0000 >> 11)
    pkts += vdp_set_register(5, 0x3B00 >> 7)
    pkts += vdp_set_register(6, 0x1800 >> 11)
    pkts += vdp_set_register(7, ((0x0F << 4) | (backdrop & 0x0F)))
    return pkts

# ---------------------------------------------------------------------------
# Generators
# ---------------------------------------------------------------------------

# --- 01_reset ---
def gen_v9958_01_reset():
    return [reset()]

# --- 10_set_registers ---
def gen_v9958_10_set_registers():
    return [reset()] + v9958_graphic1_registers()

# --- 20_palette_all_colors_graphics1 ---
def gen_v9958_20_palette():
    pkts = [reset()]
    pkts += v9958_graphic1_registers(backdrop=0x01)
    patterns = bytearray()
    for tile in range(256):
        if tile < 16:
            patterns.extend([0xFF] * 8)
        else:
            patterns.extend([(0xAA if (row + tile) & 1 else 0x55) for row in range(8)])
    pkts += vdp_write_bytes(0x0000, patterns)
    colors = bytearray()
    for group in range(32):
        fg = group % 16
        bg = (group + 1) % 16
        colors.append((fg << 4) | bg)
    pkts += vdp_write_bytes(0x2000, colors)
    names = bytearray()
    for row in range(24):
        for col in range(32):
            names.append((row // 3) * 2 + (col // 4))
    pkts += vdp_write_bytes(0x3800, names)
    return pkts

# --- 30_text_mode_ascii_grid ---
def gen_v9958_30_text_grid():
    pkts = [reset()]
    pkts += v9958_text1_registers(backdrop=0x04)
    patterns = bytearray(2048)
    for code in range(256):
        off = code * 8
        if code == 32:
            continue
        top_bot = 0xFC if code & 1 else 0x78
        lr = 0x84 if code & 2 else 0x48
        mid = 0xFC if code & 4 else 0x30
        patterns[off:off+8] = bytes([top_bot, lr, lr, mid, lr, lr, top_bot, 0])
    pkts += vdp_write_bytes(0x0000, patterns)
    names = bytearray(40 * 24)
    for row in range(24):
        for col in range(40):
            if row in (0, 23) or col in (0, 39):
                names[row * 40 + col] = ord('#')
            else:
                names[row * 40 + col] = 32 + ((row * 40 + col) % 95)
    pkts += vdp_write_bytes(0x0800, names)
    return pkts

# --- 40_g1_name_table_grid ---
def gen_v9958_40_g1_grid():
    pkts = [reset()]
    pkts += v9958_graphic1_registers(backdrop=0x04)
    patterns = bytearray(256 * 8)
    for tile in range(256):
        patterns[tile * 8:(tile + 1) * 8] = box_pattern(tile)
    pkts += vdp_write_bytes(0x0000, patterns)
    pkts += vdp_write_bytes(0x2000, bytes([(0x0F << 4) | 0x04] * 32))
    names = bytearray(32 * 24)
    for row in range(24):
        for col in range(32):
            names[row * 32 + col] = row * 4 + (col // 8)
    pkts += vdp_write_bytes(0x3800, names)
    return pkts

# --- 50_g2_three_bands ---
def gen_v9958_50_g2_bands():
    pkts = [reset()]
    pkts += v9958_graphic2_registers(backdrop=0x01)
    patterns = bytearray(256 * 8)
    for tile in range(256):
        patterns[tile * 8:(tile + 1) * 8] = box_pattern(tile)
    pkts += vdp_write_bytes(0x0000, patterns)
    for col in range(256):
        g2_color = (col // 32) % 4
        pkts += vdp_write_bytes(0x2000 + col * 8, bytes([g2_color] * 8))
    names = bytearray(32 * 24)
    for row in range(24):
        for col in range(32):
            names[row * 32 + col] = (row // 8) * 32 + col
    pkts += vdp_write_bytes(0x3800, names)
    return pkts

# --- 60_multicolor_64x48_grid ---
def gen_v9958_60_multicolor():
    pkts = [reset()]
    pkts += v9958_multicolor_registers(backdrop=0x00)
    patterns = bytearray(256 * 8)
    for tile in range(256):
        if tile < 64:
            val = tile & 0x3F
            for r in range(8):
                patterns[tile * 8 + r] = (val | (val << 4)) & 0xFF
    pkts += vdp_write_bytes(0x0000, patterns)
    pkts += vdp_write_bytes(0x2000, bytes([0xF4] * 32))
    names = bytearray(32 * 24)
    for row in range(24):
        for col in range(32):
            names[row * 32 + col] = (row // 2) * 4 + (col // 4)
    pkts += vdp_write_bytes(0x3800, names)
    return pkts

# --- 70_sprite_8x8_basic ---
def gen_v9958_70_sprites():
    pkts = [reset()]
    pkts += v9958_graphic1_registers(backdrop=0x04)
    patterns = bytearray(256 * 8)
    for tile in range(256):
        patterns[tile * 8:(tile + 1) * 8] = box_pattern(tile)
    pkts += vdp_write_bytes(0x0000, patterns)
    pkts += vdp_write_bytes(0x2000, bytes([(0x0F << 4) | 0x04] * 32))
    names = bytearray((row + col) % 8 for row in range(24) for col in range(32))
    pkts += vdp_write_bytes(0x3800, names)
    sprites = bytearray(4 * 8)
    sprites[0:8] = sprite_cross_pattern()
    sprites[8:16] = sprite_diamond_pattern()
    sprites[16:24] = bytes([0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C])
    pkts += vdp_write_bytes(0x1800, sprites)
    pkts += vdp_write_bytes(0x3B00, sprite_attributes([
        (40, 40, 0, 0x08), (72, 96, 1, 0x0A), (112, 152, 2, 0x0F)]))
    return pkts

# --- 77_sprite_4_per_scanline_limit ---
def gen_v9958_77_sprite_limit():
    pkts = [reset()]
    pkts += v9958_graphic1_registers(backdrop=0x04)
    patterns = bytearray(256 * 8)
    patterns[0:8] = sprite_diamond_pattern()
    pkts += vdp_write_bytes(0x0000, patterns)
    pkts += vdp_write_bytes(0x2000, bytes([(0x0F << 4) | 0x04] * 32))
    pkts += vdp_write_bytes(0x3800, bytes(32 * 24))
    pkts += vdp_write_bytes(0x1800, sprite_diamond_pattern())
    attrs = [(80, 16 + i * 28, 0, 0x08 + (i % 8)) for i in range(8)]
    pkts += vdp_write_bytes(0x3B00, sprite_attributes(attrs))
    return pkts

# --- 91_frame_mark_sprite_motion ---
def gen_v9958_91_sprite_motion():
    pkts = [reset()]
    pkts += v9958_graphic1_registers(backdrop=0x04)
    patterns = bytearray(256 * 8)
    for tile in range(256):
        patterns[tile * 8:(tile + 1) * 8] = box_pattern(tile & 1)
    pkts += vdp_write_bytes(0x0000, patterns)
    pkts += vdp_write_bytes(0x2000, bytes([(0x0F << 4) | 0x04] * 32))
    names = bytearray((row + col) & 1 for row in range(24) for col in range(32))
    pkts += vdp_write_bytes(0x3800, names)
    pkts += vdp_write_bytes(0x1800, sprite_diamond_pattern())
    for frame in range(36):
        x = 16 + frame * 6
        y = 32 + ((frame % 12) * 4)
        pkts += vdp_write_bytes(0x3B00, sprite_attributes([(y, x, 0, 0x0F)]))
        pkts.append(frame_mark())
    return pkts

# ---------------------------------------------------------------------------
# Generate all
# ---------------------------------------------------------------------------

GENERATORS = {
    "v9958_01_reset":              gen_v9958_01_reset,
    "v9958_10_set_registers":      gen_v9958_10_set_registers,
    "v9958_20_palette":            gen_v9958_20_palette,
    "v9958_30_text_grid":          gen_v9958_30_text_grid,
    "v9958_40_g1_grid":            gen_v9958_40_g1_grid,
    "v9958_50_g2_bands":           gen_v9958_50_g2_bands,
    "v9958_60_multicolor":         gen_v9958_60_multicolor,
    "v9958_70_sprites":            gen_v9958_70_sprites,
    "v9958_77_sprite_limit":       gen_v9958_77_sprite_limit,
    "v9958_91_sprite_motion":      gen_v9958_91_sprite_motion,
}

if __name__ == "__main__":
    for name, gen in GENERATORS.items():
        write_packet_file(output_path(f"{name}.bin"), gen())
        print(f"  {name}.bin")
