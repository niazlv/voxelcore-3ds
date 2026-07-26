#!/usr/bin/env python3
"""3DS tiled RGBA4444 texture codec: 8x8 tiles, Morton order inside a tile.

Verified against the banner texture already in the tree: decode(raw) reproduces
cia/banner.png and encode(decode(raw), dither=False) == raw byte for byte.
"""
from PIL import Image


def _morton(i):
    x = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4)
    y = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4)
    return x, y


def decode_rgba4444(data, w, h, flip_y=False):
    img = Image.new("RGBA", (w, h))
    px = img.load()
    i = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for k in range(64):
                dx, dy = _morton(k)
                v = data[i] | (data[i + 1] << 8)
                i += 2
                a = (v & 0xF) * 17
                b = ((v >> 4) & 0xF) * 17
                g = ((v >> 8) & 0xF) * 17
                r = ((v >> 12) & 0xF) * 17
                x, y = tx + dx, ty + dy
                px[x, (h - 1 - y) if flip_y else y] = (r, g, b, a)
    return img


# 4x4 ordered dither: 4 bits per channel is only 16 levels, and a smooth sky
# banded visibly without it. Deterministic, so builds stay reproducible.
_BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))


def encode_rgba4444(img, flip_y=False, dither=True):
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for k in range(64):
                dx, dy = _morton(k)
                x, y = tx + dx, ty + dy
                sy = (h - 1 - y) if flip_y else y
                r, g, b, a = px[x, sy]
                if dither:
                    # spread one quantisation step (17) over the 4x4 threshold matrix
                    t = (_BAYER[sy & 3][x & 3] - 7.5) * (17.0 / 16.0)
                    r = min(255, max(0, int(r + t)))
                    g = min(255, max(0, int(g + t)))
                    b = min(255, max(0, int(b + t)))
                v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4)
                out += bytes((v & 0xFF, v >> 8))
    return bytes(out)


if __name__ == "__main__":
    import sys
    mode, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    if mode == "decode":
        raw = open(src, "rb").read()
        decode_rgba4444(raw, 256, 128).save(dst)
        print("decoded ->", dst)
    else:
        open(dst, "wb").write(encode_rgba4444(Image.open(src)))
        print("encoded ->", dst)
