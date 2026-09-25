#!/usr/bin/env python3
# Generator for the LOCAL, original-art emoji overlay. This is the committed
# replacement for the EmojiMania-based pipeline (build_emoji_png.py), which needs
# a purchased bitmap sheet. It writes the SAME public surface -- EmojiMap.inc
# with the identical Unicode key -> atlas-glyph mapping, the identical picker
# catalog order, and an mf_bwfont Emoji.c atlas -- but the glyph art is an
# original, hand-authored 9x9 1-bit set, so it is safe to run on a fresh clone.
#
#   python3 build_emoji_local.py           # -> emoji-local/{Emoji.c, EmojiMap.inc, Emoji.cpp}
#   python3 build_emoji_local.py --preview # also writes a PNG montage for review
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).parent            # emoji-tools/ (committed)
OUT = HERE.parent / "emoji-local"       # gitignored generated output
SIZE = 9                                # glyph size (px)
HEIGHT_BYTES = (SIZE + 7) // 8          # 2 bytes per column

# (atlas_cp, [unicode codepoints it renders])
# Identical table (keys + catalog order) to build_emoji_png.py so previously
# stored messages render with the same mapping. Glyphs are original art.
EMOJI = [
    (0xE100, [0x1F600, 0x1F603, 0x1F601]),  # grinning (+ open, beaming)
    (0xE101, [0x1F604, 0x1F606, 0x1F605]),  # big grin (+ squint laugh, sweat)
    (0xE102, [0x1F602, 0x1F923]),           # tears of joy (+ rofl)
    (0xE103, [0x1F642, 0x1F643]),           # slight smile (+ upside-down)
    (0xE104, [0x1F60A, 0x1F60C, 0x263A]),   # smiling/blush (+ relieved, white)
    (0xE105, [0x1F609]),                    # wink
    (0xE106, [0x1F60D, 0x1F970]),           # heart eyes (+ smiling hearts)
    (0xE107, [0x1F618, 0x1F617, 0x1F619, 0x1F61A]),  # kiss (+ kissing variants)
    (0xE108, [0x1F61C]),                    # winking tongue
    (0xE109, [0x1F61B, 0x1F61D]),           # tongue (+ squinting tongue)
    (0xE10A, [0x1F60E]),                    # sunglasses
    (0xE10B, [0x1F914]),                    # thinking
    (0xE10C, [0x1F610, 0x1F611, 0x1F636]),  # neutral (+ expressionless, no-mouth)
    (0xE10D, [0x1F641, 0x1F61E, 0x1F614]),  # sad (+ disappointed, pensive)
    (0xE10E, [0x1F622, 0x1F625]),           # crying (+ sad-but-relieved)
    (0xE10F, [0x1F62D]),                    # loudly crying
    (0xE110, [0x1F621, 0x1F620, 0x1F624]),  # angry (+ pouting, huffing)
    (0xE111, [0x1F97A, 0x1F979]),           # pleading (+ holding back tears)
    (0xE112, [0x1F634, 0x1F62A]),           # sleeping (+ sleepy)
    (0xE113, [0x1F44D]),                    # thumbs up
    (0xE114, [0x1F44E]),                    # thumbs down
    (0xE115, [0x1F44F]),                    # clapping
    (0xE116, [0x1F64F]),                    # folded hands
    (0xE117, [0x1F44B]),                    # waving hand
    (0xE118, [0x1F4AA]),                    # flexed biceps
    (0xE119, [0x2764, 0x2665, 0x1F9E1, 0x1F49B, 0x1F49A, 0x1F499, 0x1F49C,
              0x1F5A4, 0x1F90D, 0x1F90E, 0x1F493, 0x1F495, 0x1F496, 0x1F497]),  # heart
    (0xE11A, [0x1F525]),                    # fire
    (0xE11B, [0x2B50, 0x1F31F]),            # star (+ glowing star)
    (0xE11C, [0x1F389, 0x1F38A]),           # party popper (+ confetti ball)
    (0xE11D, [0x1F4AF]),                    # hundred
    (0xE11E, [0x1F62E, 0x1F62F, 0x1F632]),  # surprised/open mouth (+ hushed, astonished)
    (0xE11F, [0x1F64B]),                    # person raising hand
]

# Rendered as nothing (advance 0): variation selectors, ZWJ, skin tones, and
# gender signs that tail gendered ZWJ sequences. Same list as the EmojiMania tool.
ZERO_WIDTH = [0x200D, 0xFE0E, 0xFE0F, 0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF,
              0x2640, 0x2642, 0x26A7]

# Original 9x9 1-bit art, index-aligned with EMOJI. '#' = on, '.' = off.
# Row order is top-to-bottom; column-major data is derived at build time.
ART = [
    # 0xE100 grinning -- round eyes, teeth row, wide open smile
    ["..#####..", "##.....##", "##.#.#.##", "##.#.#.##", "#########",
     "##.....##", "##.....##", ".##...##.", "........."],
    # 0xE101 big grin -- closed happy eyes, open smile
    ["..#####..", ".#.....#.", ".###.###.", "##.....##", "#########",
     "##.....##", "##.....##", ".##...##.", "........."],
    # 0xE102 tears of joy -- squeezed eyes, tear drops, open smile
    ["..#####..", ".#.....#.", ".###.###.", "#.#...#.#", "#########",
     "##.....##", "##.....##", ".##...##.", "........."],
    # 0xE103 slight smile -- open eyes, upturned smile
    ["..#####..", "##.....##", "##.#.#.##", "##.....##", "##.....##",
     "##.###.##", "##.....##", "##.....##", ".#######."],
    # 0xE104 smiling/blush -- closed happy eyes, smile
    ["..#####..", ".#.....#.", ".###.###.", "##.....##", "##.....##",
     "##.###.##", "##.....##", "##.....##", ".#######."],
    # 0xE105 wink -- open left eye, closed smiling right eye
    ["..#####..", "##.....##", ".#.#.###.", "##.....##", "##.....##",
     "##.###.##", "##.....##", "##.....##", ".#######."],
    # 0xE106 heart eyes -- little hearts for eyes, smile
    ["..#####..", ".#.#.#.#.", ".###.###.", "..#...#..", "##.....##",
     "##.###.##", "##.....##", "##.....##", ".#######."],
    # 0xE107 kiss -- wink plus puckered lips
    ["..#####..", "##.....##", ".#.#.###.", "##.....##", "##.....##",
     "###...###", "..###....", "##.....##", ".#######."],
    # 0xE108 winking tongue -- wink, open mouth with tongue
    ["..#####..", "##.....##", ".#.#.###.", "##.....##", "##.....##",
     "##.#####.", "##...#...", "##...#...", ".#######."],
    # 0xE109 tongue -- open eyes, tongue out
    ["..#####..", "##.....##", "##.#.#.##", "##.....##", "##.....##",
     "##.#####.", "##...#...", "##...#...", ".#######."],
    # 0xE10A sunglasses -- dark bar over the eyes, smile
    ["..#####..", "##.....##", ".#######.", "##.....##", "##.....##",
     "##.###.##", "##.....##", "##.....##", ".#######."],
    # 0xE10B thinking -- raised brow, flat tilted mouth, chin-rest hand
    ["..#####..", "##.....##", "##.#.#.##", "##.....##", "##.....##",
     "##.#####.", "##.....##", "##..###..", ".#######."],
    # 0xE10C neutral -- open eyes, flat mouth
    ["..#####..", "##.....##", "##.#.#.##", "##.....##", "##.....##",
     "##.....##", "##.###.##", "##.....##", ".#######."],
    # 0xE10D sad -- drooping mouth
    ["..#####..", "##.....##", "##.#.#.##", "##.....##", "##.....##",
     "##.###.##", "##...#...", "##.....##", ".#######."],
    # 0xE10E crying -- squeezed eyes, tear trails, drooping mouth
    ["..#####..", ".#.....#.", ".###.###.", ".##...##.", "##.....##",
     "##.###.##", "##...#...", "##.....##", ".#######."],
    # 0xE10F loudly crying -- shut eyes, big tears, wailing open mouth
    ["..#####..", ".#.....#.", ".#######.", "#.#...#.#", "#.#...#.#",
     "##.....##", "##.....##", ".#######.", "........."],
    # 0xE110 angry -- furrowed eyes, wide frown
    ["..#####..", "##.#...##", "##.#.#.##", "##.....##", "##.....##",
     "##.#####.", "##...#...", "##.....##", ".#######."],
    # 0xE111 pleading -- tall watery eyes, tiny raised mouth
    ["..#####..", "##.....##", "##.#.#.##", "##.#.#.##", "##.....##",
     "##.....##", "##.###.##", "##.....##", ".#######."],
    # 0xE112 sleeping -- closed lids, Z above
    [".....###.", "......##.", ".....###.", "..#####..", ".#######.",
     "##.....##", "##.....##", "##.....##", ".#######."],
    # 0xE113 thumbs up -- upright thumb, fist below
    [".....###.", ".....###.", ".....###.", ".....###.", "..######.",
     "..#####..", "..#####..", "..#####..", "..#####.."],
    # 0xE114 thumbs down -- fist above, thumb below
    ["..#####..", "..#####..", "..#####..", "..#####..", "..######.",
     ".....###.", ".....###.", ".....###.", ".....###."],
    # 0xE115 clapping -- two raised palms
    [".........", ".###.###.", ".###.###.", ".###.###.", ".###.###.",
     ".##...##.", ".##...##.", ".##...##.", "..##.##.."],
    # 0xE116 folded hands -- praying palms, fingertips together
    ["....#....", "...###...", "..#####..", ".##..##..", ".##..##..",
     ".##..##..", ".##..##..", ".#####...", "..####..."],
    # 0xE117 waving hand -- open palm with fingers, motion above
    ["......##.", ".....###.", "....####.", "...#####.", "....####.",
     "...#####.", "..######.", "..######.", "..######."],
    # 0xE118 flexed biceps -- bent arm with a bulge
    ["..###....", ".####....", ".###.....", ".###.....", "####.....",
     "####.....", ".###.....", ".####....", "..####..."],
    # 0xE119 heart
    [".##...##.", "####.####", "#########", "#########", ".#######.",
     "..#####..", "...###...", "....#....", "........."],
    # 0xE11A fire -- flame with inner cut
    ["....#....", "....#....", "...###...", "..####...", "..#####..",
     ".#######.", ".#######.", ".######..", "..####..."],
    # 0xE11B star -- five-point star
    ["....#....", "...###...", "...###...", ".#######.", "#########",
     "..#####..", "..#####..", ".#...#...", "........."],
    # 0xE11C party popper -- horn with confetti bursts
    [".#....#..", "..####.##", "#####..##", "..####..#", ".#####..#",
     ".##....#.", ".........", ".........", "........."],
    # 0xE11D hundred -- stacked blocks
    [".#######.", ".......#.", "......#..", "..#####..", "......#..",
     "......#..", "..#####..", ".##....#.", "........."],
    # 0xE11E surprised -- ring eyes, big open mouth
    ["..#####..", "##.....##", "..##.##..", "..##.##..", "##.....##",
     "##.....##", "##.....##", ".#######.", "........."],
    # 0xE11F person raising hand -- stick figure with a raised arm
    ["...##....", "...##..##", "..###....", "..###....", "..###....",
     "..###....", "..###....", "..###....", ".#####..."],
]


def art_to_bytes(cp, art):
    # Column-major: 2 bytes per column, LSB of first byte = top-left pixel.
    for i, row in enumerate(art):
        assert len(row) == SIZE, f"{cp:#x} row {i} width {len(row)}"
    out = []
    for x in range(SIZE):
        b0 = sum((1 << r) for r in range(8) if art[r][x] == '#')
        b1 = 1 if art[8][x] == '#' else 0
        out.extend([b0, b1])
    return out


def write_emoji_c():
    n = len(ART)
    lines = []
    lines.append("/* Generated by build_emoji_local.py -- LOCAL original art, do not commit. */")
    lines.append("#ifndef MF_BWFONT_INTERNALS")
    lines.append("#define MF_BWFONT_INTERNALS")
    lines.append("#endif")
    lines.append('#include "mf_bwfont.h"')
    lines.append("")
    lines.append("#ifndef MF_BWFONT_VERSION_4_SUPPORTED")
    lines.append("#error The font file is not compatible with this version of mcufont.")
    lines.append("#endif")
    lines.append("")
    lines.append(f"static const uint8_t mf_bwfont_Emoji_glyph_data_0[{n * SIZE * HEIGHT_BYTES}] PROGMEM = {{")
    for i, (cp, art) in enumerate(zip([e[0] for e in EMOJI], ART)):
        b = art_to_bytes(cp, art)
        row = f"    /* 0x{cp:04X} */ " + ", ".join("0x%02x" % v for v in b) + ","
        lines.append(row)
    lines.append("};")
    lines.append("")
    lines.append("static const struct mf_bwfont_char_range_s mf_bwfont_Emoji_char_ranges[] = {")
    lines.append("    {")
    lines.append(f"        {EMOJI[0][0]}, /* first_char */")
    lines.append(f"        {n}, /* char_count */")
    lines.append("        0, /* offset_x */")
    lines.append("        0, /* offset_y */")
    lines.append(f"        {HEIGHT_BYTES}, /* height_bytes */")
    lines.append(f"        {SIZE}, /* height_pixels */")
    lines.append(f"        {SIZE}, /* width */")
    lines.append("        0, /* glyph_widths */")
    lines.append("        0, /* glyph_offsets */")
    lines.append("        mf_bwfont_Emoji_glyph_data_0, /* glyph_data */")
    lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("const struct mf_bwfont_s mf_bwfont_Emoji = {")
    lines.append("    {")
    lines.append('        "emoji-local",')
    lines.append('        "Emoji",')
    lines.append(f"        {SIZE}, /* width */")
    lines.append(f"        {SIZE}, /* height */")
    lines.append(f"        {SIZE}, /* min_x_advance */")
    lines.append(f"        {SIZE}, /* max_x_advance */")
    lines.append("        0, /* baseline_x */")
    lines.append(f"        {SIZE}, /* baseline_y */")
    lines.append(f"        {SIZE}, /* line_height */")
    lines.append("        3, /* flags: monospace + bw */")
    lines.append("        32, /* fallback_character */")
    lines.append("        &mf_bwfont_character_width,")
    lines.append("        &mf_bwfont_render_character,")
    lines.append("    },")
    lines.append("    4, /* version */")
    lines.append("    1, /* char_range_count */")
    lines.append("    mf_bwfont_Emoji_char_ranges,")
    lines.append("};")
    lines.append("")
    (OUT / "Emoji.c").write_text("\n".join(lines) + "\n")


def write_emoji_map():
    entries, seen = [], {}
    for cp, keys in ((c[0], c[1]) for c in EMOJI):
        for u in keys:
            k = u & 0xFFFF
            assert k not in seen, f"key collision 0x{k:04X}: {hex(u)} vs {hex(seen[k])}"
            seen[k] = u
            entries.append((k, cp))
    entries.sort()
    zw = sorted({u & 0xFFFF for u in ZERO_WIDTH})
    out = ["// Generated by build_emoji_local.py -- LOCAL, do not commit.",
           "struct EmojiEntry { uint16_t key; uint16_t glyph; };",
           "static const EmojiEntry kEmojiMap[] = {"]
    out += [f"  {{0x{k:04X}, 0x{a:04X}}}," for k, a in entries]
    out += ["};", "static const uint16_t kEmojiZeroWidth[] = {",
            "  " + ", ".join(f"0x{z:04X}" for z in zw) + ",", "};"]
    catalog = [keys[0] for _cp, keys in ((c[0], c[1]) for c in EMOJI)]
    out += ["static const uint32_t kEmojiCatalog[] = {",
            "  " + ", ".join(f"0x{c:X}" for c in catalog) + ",", "};"]
    (OUT / "EmojiMap.inc").write_text("\n".join(out) + "\n")


def preview():
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("PIL not available; skipping preview")
        return
    scale = 9
    gap = 2
    cols = 8
    rows = (len(ART) + cols - 1) // cols
    cell = SIZE * scale + gap
    img = Image.new("L", (cols * cell + gap, rows * cell + gap), 255)
    d = ImageDraw.Draw(img)
    for i, art in enumerate(ART):
        cx, cy = i % cols, i // cols
        x0 = gap + cx * cell
        y0 = gap + cy * cell
        for r in range(SIZE):
            for c in range(SIZE):
                if art[r][c] == '#':
                    d.rectangle([x0 + c * scale, y0 + r * scale,
                                 x0 + c * scale + scale - 1, y0 + r * scale + scale - 1],
                                fill=0)
        d.text((x0, y0 + SIZE * scale + 2), "0x%04X" % (0xE100 + i), fill=64)
    path = "/var/folders/2w/_gq90qbn5dg_6zyszyqxj72c0000gn/T/opencode/emoji_preview.png"
    img.save(path)
    print("wrote", path)


def main():
    OUT.mkdir(exist_ok=True)
    assert len(ART) == len(EMOJI), "ART/EMOJI length mismatch"
    for i, art in enumerate(ART):
        for r, row in enumerate(art):
            assert len(row) == SIZE, f"{EMOJI[i][0]:#x} row {r} width {len(row)}"
    write_emoji_c()
    write_emoji_map()
    shutil.copy(HERE / "Emoji.cpp", OUT / "Emoji.cpp")
    if "--preview" in sys.argv:
        preview()
    allkeys = {k for _c, keys in EMOJI for k in keys}
    print(f"wrote emoji-local/{{Emoji.c, EmojiMap.inc, Emoji.cpp}} "
          f"({len(ART)} glyphs, {len(allkeys)} keys)")


main()