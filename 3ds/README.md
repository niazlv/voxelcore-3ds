# VoxelCore — Nintendo 3DS port

Port of [VoxelCore](https://github.com/MihailRis/voxelcore) to the original
Nintendo 3DS (ARM11, 128 MB RAM, PICA200). The engine core is used as-is from
the repository root; everything 3DS-specific lives in this directory.

What works: world generation and loading, survival/creative worlds, the
engine's own GUI on the bottom screen (touch), inventory, hotbar tap-select,
block placing/breaking with vertex lighting, NDSP audio (ogg via stb_vorbis),
content packs from SD.

## Building

Requires Docker (devkitPro/devkitARM image is pulled automatically):

```
./3ds/build.sh
```

The result is `3ds/build/voxelcore3ds.3dsx` (run via Homebrew Launcher or
Azahar). `3ds/cia/build_cia.sh` packages a CIA (needs makerom).

## Layout

- `src/` — platform backends: PICA200 renderer (no citro3d), NDSP audio,
  touch input, HTC-less window/engine glue
- `shaders/` — PICA vertex shaders
- `romfs/` — resources baked into the 3dsx (`res` is a symlink to the
  engine's own `res/`)
- `tools/` — asset pipeline (textures to CTR tiling, banner, voxel models)
- `cia/` — CIA packaging

## Engine changes

Platform-independent fixes and mechanisms discovered during the port
(alignment UB in gzip, building without OpenGL/OpenAL, Lua without FFI,
memory-profile build knobs, small-screen UI) were submitted upstream:
[MihailRis/voxelcore#889](https://github.com/MihailRis/voxelcore/pull/889).
The port patches only 26 lines of engine code, guarded by `VC_PORT_3DS`.
