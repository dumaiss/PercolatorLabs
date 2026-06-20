"""Generate a V9958 Text 1 mode test pattern stream.

Displays "V9958" in the center with white-on-blue colors.
V9958 mode bits: M1=R#1D4, M2=R#1D3, M3=R#0D1, M4=R#0D2, M5=R#0D3.
Text 1 = 0b00001: M1=1, others 0. BL=R#1D7=1 to unblank.
"""

from vdrip_packets import (
    PACKET_VDP_CTRL_WRITE,
    PACKET_VDP_DATA_WRITE,
    PACKET_FRAME_MARK,
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

    # R#0: M3=M4=M5=0
    pkts += vdp_set_register(0, 0x00)
    # R#1: BL=1(D7), M1=1(D4) -> Text 1, unblanked
    pkts += vdp_set_register(1, 0x50)
    # R#2: Name table at 0x0800
    pkts += vdp_set_register(2, 0x02)
    # R#4: Pattern table at 0x0000
    pkts += vdp_set_register(4, 0x00)
    # R#7: white(F) text, blue(4) backdrop
    pkts += vdp_set_register(7, 0xF4)

    # Color table at 0x2000 (32 bytes for Text 1): white on blue
    pkts += vdp_write_bytes(0x2000, bytes([0xF4] * 32))

    # Font patterns: "V", "9", "5", "8", space
    patterns = bytearray(2048)
    def glyph(code, pat):
        for i, b in enumerate(pat):
            patterns[code * 8 + i] = b
    glyph(ord('V'), [0x82,0x82,0x82,0x44,0x44,0x28,0x10,0x00])
    glyph(ord('9'), [0x38,0x44,0x44,0x3C,0x04,0x44,0x38,0x00])
    glyph(ord('5'), [0x78,0x40,0x78,0x04,0x04,0x44,0x38,0x00])
    glyph(ord('8'), [0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00])
    pkts += vdp_write_bytes(0x0000, bytes(patterns))

    # Name table: "V9958" at row 11, col 17
    name_tab = bytearray(40 * 24)
    for i, ch in enumerate(b"V9958"):
        name_tab[11 * 40 + 17 + i] = ch
    pkts += vdp_write_bytes(0x0800, bytes(name_tab))

    pkts.append(packet(PACKET_FRAME_MARK))
    return pkts

if __name__ == "__main__":
    write_packet_file(output_path("v9958_text1_hello.bin"), build_packets())
