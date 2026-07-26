// Texture implementation for the 3DS port: the engine's Texture class
// backed by citro3d C3D_Tex. `id` stores the C3D_Tex pointer (32-bit).
#include "graphics/core/Texture.hpp"

#include <3ds.h>
#include <citro3d.h>

#include <cstring>
#include <vector>
#include <cstdio>

#include "debug/Logger.hpp"

static debug::Logger logger("texture-3ds");

uint Texture::MAX_RESOLUTION = 1024;

namespace {
    inline C3D_Tex* tex_of(uint id) {
        return reinterpret_cast<C3D_Tex*>(id);
    }

    inline bool is_pow2(uint v) {
        return v && (v & (v - 1)) == 0;
    }

    // Morton (Z-order) swizzle within 8x8 tiles, as PICA200 expects.
    // Input: RGBA8 (engine ImageData layout, row 0 = top).
    // PICA texture coordinates: (0,0) is bottom-left, so flip Y.
    void upload_rgba8(C3D_Tex* tex, const ubyte* data, uint width, uint height) {
        u32* out = reinterpret_cast<u32*>(tex->data);
        // standard PICA200 8x8 tile Z-order: bit-interleave of (x, y)
        static const u8 morton[64] = {
             0,  1,  4,  5, 16, 17, 20, 21,
             2,  3,  6,  7, 18, 19, 22, 23,
             8,  9, 12, 13, 24, 25, 28, 29,
            10, 11, 14, 15, 26, 27, 30, 31,
            32, 33, 36, 37, 48, 49, 52, 53,
            34, 35, 38, 39, 50, 51, 54, 55,
            40, 41, 44, 45, 56, 57, 60, 61,
            42, 43, 46, 47, 58, 59, 62, 63
        };
        for (uint ty = 0; ty < height; ty += 8) {
            for (uint tx = 0; tx < width; tx += 8) {
                u32* tileOut =
                    out + (ty / 8) * (width / 8) * 64 + (tx / 8) * 64;
                for (uint py = 0; py < 8; py++) {
                    for (uint px = 0; px < 8; px++) {
                        uint sx = tx + px;
                        uint sy = height - 1 - (ty + py);
                        const ubyte* src = data + (sy * width + sx) * 4;
                        // PICA RGBA8 stored as ABGR bytes
                        u32 abgr = (u32(src[0]) << 24) | (u32(src[1]) << 16) |
                                   (u32(src[2]) << 8) | u32(src[3]);
                        tileOut[morton[py * 8 + px]] = abgr;
                    }
                }
            }
        }
    }
}

Texture::Texture(uint id, uint width, uint height)
    : id(id), width(width), height(height) {
}

Texture::Texture(const ubyte* data, uint width, uint height, ImageFormat) {
    this->width = width;
    this->height = height;
    auto tex = new C3D_Tex();
    // pad tiny textures (e.g. Batch2D blank) up to the PICA minimum 8x8
    std::vector<ubyte> padded;
    const ubyte* uploadData = data;
    uint texW = width, texH = height;
    if (data && (width < 8 || height < 8)) {
        texW = 8; texH = 8;
        padded.resize(8 * 8 * 4);
        for (uint y = 0; y < 8; y++) {
            for (uint x = 0; x < 8; x++) {
                const ubyte* src =
                    data + ((y % height) * width + (x % width)) * 4;
                ubyte* dst = padded.data() + (y * 8 + x) * 4;
                dst[0] = src[0]; dst[1] = src[1];
                dst[2] = src[2]; dst[3] = src[3];
            }
        }
        uploadData = padded.data();
    }
    if (!is_pow2(texW) || !is_pow2(texH) || texW < 8 || texH < 8 ||
        texW > 1024 || texH > 1024) {
        logger.error() << "unsupported texture size " << texW << "x" << texH;
        delete tex;
        id = 0;
        return;
    }
    if (!C3D_TexInit(tex, texW, texH, GPU_RGBA8)) {
        logger.error() << "C3D_TexInit failed " << texW << "x" << texH;
        delete tex;
        id = 0;
        return;
    }
    if (uploadData) {
        upload_rgba8(tex, uploadData, texW, texH);
    }
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    // REPEAT, not CLAMP: the font renderer addresses unicode codepages with
    // out-of-range UVs and relies on wrapping (textures are power-of-2)
    C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
    id = reinterpret_cast<uint>(tex);
}

Texture::~Texture() {
    if (id) {
        C3D_TexDelete(tex_of(id));
        delete tex_of(id);
    }
}

void Texture::bind() const {
    if (id) {
        C3D_TexBind(0, tex_of(id));
    }
}

void Texture::unbind() const {
}

void Texture::reload(const ubyte* data, uint w, uint h) {
    if (id && w == width && h == height) {
        upload_rgba8(tex_of(id), data, w, h);
    }
}

void Texture::reload(const ImageData& image) {
    reload(image.getData(), image.getWidth(), image.getHeight());
}

void Texture::reloadPartial(const ImageData&, uint, uint, uint, uint) {
    // not needed on 3DS (texture animations disabled)
}

void Texture::setNearestFilter() {
    if (id) {
        C3D_TexSetFilter(tex_of(id), GPU_NEAREST, GPU_NEAREST);
    }
}

void Texture::setMipMapping(bool, bool) {
}

std::unique_ptr<ImageData> Texture::readData() {
    return nullptr;
}

uint Texture::getId() const {
    return id;
}

std::unique_ptr<Texture> Texture::from(const ImageData* image) {
    return std::make_unique<Texture>(
        image->getData(), image->getWidth(), image->getHeight(),
        image->getFormat());
}
