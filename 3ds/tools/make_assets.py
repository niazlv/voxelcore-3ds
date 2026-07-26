#!/usr/bin/env python3
"""Build the 3DS presentation assets for VoxelCore.

Outputs:
  3ds/icon.png       48x48  - SMDH icon (smdhtool derives the 24x24 itself)
  3ds/cia/banner.png 256x128 - banner artwork (fed to mkbanner.py)

Sources (all kept in-tree, nothing is fetched at build time):
  dev/VoxelCore.png       - the official VoxelCore cube, used as-is
  res/fonts/font_0.png    - the engine's own bitmap font, used for the wordmark
  3ds/tools/art/bg.png    - painted backdrop (see art/README.md)
"""
from PIL import Image, ImageFilter, ImageChops
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CUBE = os.path.join(ROOT, "dev", "VoxelCore.png")
FONT = os.path.join(ROOT, "res", "fonts", "font_0.png")
BG = os.path.join(ROOT, "3ds", "tools", "art", "bg.png")
HERO_ISLAND = os.path.join(ROOT, "3ds", "tools", "art", "hero_island.png")

# which subject carries the banner: "cube" (the logo block) or "island" (a world chunk)
HERO = os.environ.get("VC_BANNER_HERO", "island")

GLYPH_W, GLYPH_H, CELL = 8, 16, 16


# ---------------------------------------------------------------- bitmap font
def glyph_mask(ch):
    """1-bit mask of one glyph from the engine font atlas."""
    atlas = glyph_mask.atlas
    c = ord(ch)
    col, row = c % 16, c // 16
    return atlas.crop((col * CELL, row * CELL, col * CELL + GLYPH_W, row * CELL + GLYPH_H))


glyph_mask.atlas = None


def text_mask(text, scale=1, bold=True, tracking=0):
    """Render text to an L-mode mask, optionally emboldened, integer-scaled."""
    adv = GLYPH_W + (1 if bold else 0) + tracking
    m = Image.new("L", (adv * len(text) + 2, GLYPH_H), 0)
    for i, ch in enumerate(text):
        if ch == " ":  # atlas cell 0x20 holds a visible "SP" control glyph
            continue
        g = glyph_mask(ch)
        m.paste(ImageChops.lighter(m.crop((i * adv, 0, i * adv + GLYPH_W, GLYPH_H)), g),
                (i * adv, 0))
    if bold:  # thicken by OR-ing a 1px right shift
        m = ImageChops.lighter(m, ImageChops.offset(m, 1, 0))
    if scale != 1:
        m = m.resize((m.width * scale, m.height * scale), Image.NEAREST)
    return m


def dilate(mask, r=1):
    return mask.filter(ImageFilter.MaxFilter(2 * r + 1))


def draw_text(dst, text, xy, scale=2, fill=(255, 255, 255), outline=(20, 28, 20),
              bold=True, tracking=0):
    """Outlined + drop-shadowed text, pixel-exact (no resampling)."""
    m = text_mask(text, scale, bold, tracking)
    x, y = xy
    o = dilate(m, max(1, scale // 2))
    shadow = Image.new("RGBA", dst.size, (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", m.size, outline + (110,)), (x + scale, y + scale), o)
    dst.alpha_composite(shadow)
    layer = Image.new("RGBA", dst.size, (0, 0, 0, 0))
    layer.paste(Image.new("RGBA", m.size, outline + (255,)), (x, y), o)
    layer.paste(Image.new("RGBA", m.size, fill + (255,)), (x, y), m)
    dst.alpha_composite(layer)
    return m.width, m.height


def cutout(path, thresh=34):
    """Lift a sprite off its flat key-colour background (border flood fill)."""
    from PIL import ImageDraw
    src = Image.open(path).convert("RGB")
    work = src.copy()
    key = (255, 0, 255)
    for corner in ((0, 0), (work.width - 1, 0), (0, work.height - 1),
                   (work.width - 1, work.height - 1)):
        ImageDraw.floodfill(work, corner, key, thresh=thresh)
    out = src.convert("RGBA")
    alpha = Image.new("L", src.size, 255)
    ap, wp = alpha.load(), work.load()
    for y in range(src.height):
        for x in range(src.width):
            if wp[x, y] == key:
                ap[x, y] = 0
    out.putalpha(alpha)
    return out.crop(out.getbbox())


def drop_shadow(rgba, blur=3, offset=(2, 3), alpha=120):
    """Soft shadow under an RGBA sprite; returns a same-size RGBA layer."""
    pad = blur * 3
    lay = Image.new("RGBA", (rgba.width + pad * 2, rgba.height + pad * 2), (0, 0, 0, 0))
    sh = Image.new("RGBA", rgba.size, (0, 0, 0, alpha))
    lay.paste(sh, (pad + offset[0], pad + offset[1]), rgba.getchannel("A"))
    lay = lay.filter(ImageFilter.GaussianBlur(blur))
    lay.alpha_composite(rgba, (pad, pad))
    return lay, pad


# ---------------------------------------------------------------- assets
def build_banner(out, hero=HERO):
    bg = Image.open(BG).convert("RGBA")
    tw, th = 256, 128
    # the island needs sky around it, the cube sits happily on the horizon line:
    # zoom in and lift the crop for "island", cover-fit the whole backdrop for "cube"
    zoom = 1.35 if hero in ("island", "none") else 1.0
    s = max(tw / bg.width, th / bg.height) * zoom
    bg = bg.resize((round(bg.width * s), round(bg.height * s)), Image.LANCZOS)
    # shift left of centre for "island": keeps the backdrop tree out of the frame,
    # where it would otherwise poke out from behind the hero
    left = round((bg.width - tw) * (0.40 if hero in ("island", "none") else 0.5))
    top = round((bg.height - th) * (0.18 if hero in ("island", "none") else 0.0))
    bg = bg.crop((left, top, left + tw, top + th))

    # soft dark wash on the left so the wordmark always reads
    wash = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    px = wash.load()
    for x in range(tw):
        a = int(96 * max(0.0, 1.0 - x / 150.0))
        for y in range(th):
            px[x, y] = (8, 14, 22, a)
    bg.alpha_composite(wash)

    if hero == "none":
        pass          # backdrop only: the 3D banner supplies the island itself
    elif hero == "island":
        sprite = cutout(HERO_ISLAND)
        k = 116 / sprite.height
        sprite = sprite.resize((round(sprite.width * k), 116), Image.LANCZOS)
        lay, pad = drop_shadow(sprite, blur=4, offset=(3, 4), alpha=110)
        bg.alpha_composite(lay, (tw - sprite.width - 6 - pad, (th - 116) // 2 - pad - 4))
    else:
        # the original cube, unmodified art, on the right
        cube = Image.open(CUBE).convert("RGBA").resize((74, 74), Image.LANCZOS)
        lay, pad = drop_shadow(cube, blur=3, offset=(2, 4), alpha=130)
        bg.alpha_composite(lay, (tw - 74 - 18 - pad, (th - 74) // 2 - pad + 2))

    draw_text(bg, "VoxelCore", (13, 38), scale=2, tracking=-1)
    draw_text(bg, "3DS homebrew port", (15, 76), scale=1, bold=False,
              fill=(198, 234, 170), outline=(12, 20, 14))
    bg.convert("RGB").save(out)
    return bg


def build_icon(out48):
    bg = Image.open(BG).convert("RGB")
    # a 1px column of pure sky stretched to a square: same palette as the banner,
    # smooth gradient, none of the terrain fragments a crop would drag in
    w, h = bg.size
    sky = bg.crop((int(w * 0.30), int(h * 0.04), int(w * 0.30) + 1, int(h * 0.62)))
    tile = sky.resize((48, 48), Image.LANCZOS).convert("RGBA")

    cube = Image.open(CUBE).convert("RGBA").resize((42, 42), Image.LANCZOS)
    lay, pad = drop_shadow(cube, blur=2, offset=(1, 2), alpha=130)
    tile.alpha_composite(lay, ((48 - 42) // 2 - pad, (48 - 42) // 2 - pad))
    icon48 = tile.convert("RGB")
    icon48.save(out48)
    return icon48


if __name__ == "__main__":
    glyph_mask.atlas = Image.open(FONT).convert("RGBA").getchannel("A")
    b = build_banner(os.path.join(ROOT, "3ds", "cia", "banner.png"))
    # backdrop without the painted island: mkbanner3d.py puts a real model there
    build_banner(os.path.join(ROOT, "3ds", "tools", "art", "banner_backdrop.png"),
                 hero="none")
    i = build_icon(os.path.join(ROOT, "3ds", "icon.png"))
    print("banner 256x128 ->", os.path.join("3ds", "cia", "banner.png"))
    print("icon   48x48   ->", os.path.join("3ds", "icon.png"))
