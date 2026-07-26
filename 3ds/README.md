# VoxelCore 3DS port — internals

Everything 3DS-specific lives in this directory; the engine core is used
as-is from the repository root. For the overview, feature status and
downloads see the [root README](../README.md).

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
memory-profile build knobs, small-screen UI) were submitted upstream as
[MihailRis/voxelcore#889](https://github.com/MihailRis/voxelcore/pull/889)
(declined; they live in this repo's history instead).
The port itself patches only 26 lines of engine code, guarded by
`VC_PORT_3DS` (touch-input frame routing in `src/frontend/hud.cpp`,
path logging in `src/engine/EnginePaths.cpp`).

Build-time defines used by the port (see `CMakeLists.txt`): `VC_NO_GL`,
`VC_NO_AL`, `VC_AUDIO_CUSTOM_BACKEND`, `VC_CHUNK_H=128`,
`VC_ITEM_ICON_SIZE=24`, `VC_CHUNKS_POOL_SIZE=0`, `VC_PORT_3DS`.

## LuaJIT configuration

LuaJIT runs as a plain interpreter: the JIT needs writable+executable
pages, which the 3DS doesn't grant. The FFI is present but `ffi.C` cannot
resolve symbols in a statically linked homebrew binary, so the engine's
pure-Lua fallbacks kick in. `build_luajit.sh` reproduces the exact library
configuration (`TARGET_SYS=Other`, `LUAJIT_DISABLE_JIT`,
`LUAJIT_USE_SYSMALLOC`, `LUAJIT_SECURITY_PRNG=0`, armv6k hard-float,
32-bit host buildvm).
