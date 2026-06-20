"""Generate a V9958 GRAPHIC 6 test pattern with colored vertical bars.

G6 mode: bits=0x14 (M3=1,M5=1). 512 pixels wide, 4-bit packed pixels.
R#0=0x0A (M3=1,M5=1). R#1=0x40 (BL=1).
"""

from vdrip_packets import (
    PACKET_VDP_CTRL_WRITE, PACKET_VDP_DATA_WRITE, PACKET_FRAME_MARK,
    PACKET_VDP_PALETTE_WRITE,
    packet, reset, output_path, write_packet_file,
)

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

def build_packets():
    pkts = [reset()]

    # G6 mode: R#0 D1(M3)=1, D3(M5)=1 → 0x0A
    pkts += vdp_set_register(0, 0x0A)
    # R#1: BL(D6)=1 → 0x40
    pkts += vdp_set_register(1, 0x40)
    # R#2: pattern table at 0x0000
    pkts += vdp_set_register(2, 0x00)
    # R#4: pattern table high bits
    pkts += vdp_set_register(4, 0x00)
    # R#7: backdrop = blue (4)
    pkts += vdp_set_register(7, 0x04)
    # R#8: 512-wide mode (bit0=0 for G6? Actually G6 is always 512)
    # R#9: 212 lines, no interlace → 0x80 (LN=1 for 212)
    pkts += vdp_set_register(9, 0x80)

    # Palette: 16 vivid colors
    palette_rgb = [
        (0,0,0), (0,0,0), (0,0x80,0), (0,0xC0,0),
        (0x80,0,0x80), (0x80,0,0xC0), (0x80,0,0), (0,0xFF,0xFF),
        (0xFF,0,0), (0xFF,0,0xC0), (0xC0,0xC0,0), (0,0xC0,0),
        (0,0x80,0), (0xC0,0,0xC0), (0xC0,0xC0,0xC0), (0xFF,0xFF,0xFF),
    ]
    for i, (r, g, b) in enumerate(palette_rgb):
        # V9958 palette: byte0 = R(high nibble)|B(high nibble), byte1 = G(high nibble)
        pkts += vdp_set_register(16, i)  # select palette entry i via R#16
        pkts.append(packet(PACKET_VDP_PALETTE_WRITE, bytes([(r >> 4) | (b & 0xF0)])))
        pkts.append(packet(PACKET_VDP_PALETTE_WRITE, bytes([g >> 4])))

    # G6 pixel data: 16 vertical color bars, each 32 pixels wide
    # 512 pixels / 16 bars = 32 pixels per bar = 16 bytes per scanline per bar
    # Each byte = 2 pixels (high nibble left, low nibble right)
    scanline = bytearray(256)  # 512 pixels / 2 = 256 bytes
    for bar in range(16):
        color = bar  # color index 0-15
        packed = (color << 4) | color  # both pixels same color
        start = bar * 16
        for i in range(16):
            scanline[start + i] = packed

    # Write the same scanline for all 212 lines
    vram = bytearray()
    for line in range(212):
        vram.extend(scanline)
    pkts += vdp_write_bytes(0x0000, bytes(vram))

    pkts.append(packet(PACKET_FRAME_MARK))
    return pkts

if __name__ == "__main__":
    write_packet_file(output_path("v9958_g6_bars.bin"), build_packets())
