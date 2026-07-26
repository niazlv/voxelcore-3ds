# Art sources for the 3DS presentation assets

Nothing here is fetched at build time — `make_assets.py` and `mkbanner.py` only read
files from this tree. This file records where the painted pieces came from so they can
be regenerated or replaced.

## Reused, not generated

| asset | file | used for |
|---|---|---|
| the official VoxelCore cube | `dev/VoxelCore.png` (128x128, alpha) | the icon, and the hero cube on the banner — pasted unmodified, only scaled |
| the engine's bitmap font | `res/fonts/font_0.png` (8x16 glyphs in 16x16 cells) | the `VoxelCore` wordmark on the banner, so the banner type matches in‑game type |

## Generated

Model: **Nano Banana 2** (`google/gemini-3.1-flash-image-preview`), via an
OpenAI-compatible `POST /v1/images/generations` (`image: [{"url": "data:image/png;base64,…"}]`
carries the reference image). The official cube was passed as the style reference for
every generation, so the palette and block texture stay on-brand.

### `bg.png` — banner backdrop (1456x716)

> Using the attached reference block as the exact style guide (its palette, its
> grass-green top and grey stone texture, its pixel-art block look), draw a WIDE 2:1
> landscape BACKGROUND for a game banner. Content: a voxel/cube world seen from a low
> angle — a strip of blocky terrain along the bottom third made of grass-topped cubes,
> grey stone cliff blocks, one small blocky tree on the far right, calm blue water on
> the far left; above it a big open sky with a soft gradient from warm light near the
> horizon to deeper blue at the top, a few simple clouds and three tiny floating cubes
> high up. IMPORTANT: leave the upper-left half of the image visually calm and
> uncluttered (plain sky), no large objects there. Absolutely NO text, NO letters, NO
> logo, NO single big hero cube in the center, no watermark, no frame, no border, no UI.

### `hero_island.png` — floating chunk (1024x1024, flat grey key colour)

> Using the attached block as the exact style and palette reference (its grass-green
> top, its grey stone, its crisp pixel-art block texture), render ONE floating voxel
> island chunk as a clean product shot: a square grass plateau on top about 12 blocks
> across, a jagged grey stone underside that tapers downward to a rough point, one small
> blocky tree with a dark green cube canopy standing on the plateau, a thin ribbon of
> blue water spilling off the left edge, two or three cube boulders on the grass.
> Three-quarter view from slightly above, the whole object centred and fully visible
> with a small margin, even neutral lighting with a soft warm key from the upper left
> and gentle ambient fill, no harsh shadows, no cast shadow. Background: completely flat
> uniform light grey (191,191,191), absolutely empty. No text, no logo, no ground plane,
> no vignette, no extra objects. Sharp clean edges.

Deliberately shot as an even-lit object on a flat key colour: that is what
image-to-3D services want, and it is also trivial to cut out for compositing.

### `hero_lowpoly.png` / `hero_lowpoly_iso.png` — inputs for image-to-3D

The first island reconstruction (from `hero_views.png`) came back as ~500k triangles of
mushy stone: the fine texture detail in the source art turned into surface noise, and
the flat grey background sat at the same value as the grey stone, which hurts matting.
These two are drawn for the reconstructor instead of for looks — big cubes, flat faces,
hard edges, thin dark seams, no surface detail at all, white background, no shadow, and
a flat bottom face instead of a fragile tapering spike.

`hero_lowpoly.png` keeps a perspective camera and a tree (recommended — perspective
gives the depth cues a single-image reconstructor needs). `hero_lowpoly_iso.png` is a
strictly isometric, even simpler variant kept as a fallback.

### `island_vox12.glb` → the rotating model on the menu's top screen

`hero_lowpoly.png` was sent through an image-to-3D service, which returned one
island as 18 meshes / 143k triangles: real voxels, but every block face was
subdivided into hundreds of noisy quads (406 distinct normals instead of 6).

`voxelize.py` snaps that back onto a voxel grid, fills the interior, keeps only
the faces between a filled cell and outside air, and greedy-merges same-colour
rectangles. At a 12-cell grid it lands on 2010 triangles, and mesh volume equals
voxel-count × cell³ to 0.0000% — the proof the shell is closed and wound outward.

`mkvoxmodel.py island_vox12.glb` then writes:

  3ds/romfs/models/island.vcm3ds      the port's asset (138 KiB, palette inside)
  3ds/tools/art/island_vox12_textured.glb   the same model for any glTF viewer

Colours go into a 64x64 padded palette texture and the vertex colour carries
baked per-face light, so `final = palette texel * vertex colour` — exactly what
the existing world shader computes. No new shader, no new vertex format, and the
GLB now shows colours in viewers that ignore `COLOR_0`.

### `hero_views.png` — 2x2 turntable sheet of the same island

Generated from `hero_island.png` as the reference (front / +90 / +180 / +270), flat
lighting, same framing, flat grey background — the input format multi-view image-to-3D
services ask for.
