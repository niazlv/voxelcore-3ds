// Mesh<T> template implementation for the 3DS port (replaces Mesh.inl).
// Vertex/index buffers live in the linear heap for GPU access; 32-bit engine
// indices are converted to 16-bit (PICA limitation).
#include "graphics/core/Mesh.hpp"

#include <3ds.h>
#include <citro3d.h>

#include <cstring>
#include <vector>

#include "graphics/render/commons.hpp"
#include "graphics/core/Batch2D.hpp"
#include "graphics/core/Batch3D.hpp"
#include "graphics/render/MainBatch.hpp"
#include "debug/Logger.hpp"

static debug::Logger logger("mesh-3ds");

// deferred linear-heap frees: the GPU may still read buffers freed
// mid-frame (Batch2D reloads its mesh on every flush)
namespace {
    struct Retired {
        void* ptr;
        int frame;
    };
    std::vector<Retired> retired;
    int currentFrame = 0;
}

static void retire(void* ptr) {
    if (ptr) retired.push_back({ptr, currentFrame});
}

extern "C" void vc3ds_mesh_collect() {
    currentFrame++;
    size_t kept = 0;
    for (auto& r : retired) {
        if (currentFrame - r.frame >= 2) {
            linearFree(r.ptr);
        } else {
            retired[kept++] = r;
        }
    }
    retired.resize(kept);
}

int MeshStats::meshesCount = 0;
int MeshStats::drawCalls = 0;

// 3DS-side storage, referenced by Mesh through the (repurposed) vao field.
struct Mesh3DSData {
    void* vbo = nullptr;          // linearAlloc'd vertices
    size_t vertexCount = 0;
    size_t stride = 0;
    int attrCount = 0;
    u64 permutation = 0;
    struct Ibo {
        u16* data = nullptr;      // linearAlloc'd 16-bit indices
        size_t count = 0;
    };
    std::vector<Ibo> ibos;

    ~Mesh3DSData() {
        retire(vbo);
        for (auto& ibo : ibos) {
            retire(ibo.data);
        }
    }
};

template <typename T>
Mesh<T>::Mesh(const MeshData<T>& data)
    : Mesh(
          data.vertices.data(),
          data.vertices.size(),
          [&data]() {
              std::vector<IndexBufferData> indices;
              for (const auto& buffer : data.indices) {
                  indices.push_back(IndexBufferData {buffer.data(), buffer.size()});
              }
              return indices;
          }()
      ) {
}

template <typename T>
Mesh<T>::Mesh(
    const T* vertexBuffer, size_t vertices, std::vector<IndexBufferData> indices
)
    : vao(0), vbo(0), ibos(), vertexCount(0) {
    MeshStats::meshesCount++;
    auto* d = new Mesh3DSData();
    d->stride = sizeof(T);
    d->attrCount = 0;
    d->permutation = 0;
    for (int i = 0; T::ATTRIBUTES[i].count; i++) {
        d->permutation |= static_cast<u64>(i) << (i * 4);
        d->attrCount++;
    }
    vao = reinterpret_cast<unsigned int>(d);
    reload(vertexBuffer, vertices, indices);
}

template <typename T>
Mesh<T>::~Mesh() {
    MeshStats::meshesCount--;
    delete reinterpret_cast<Mesh3DSData*>(vao);
}

template <typename T>
void Mesh<T>::reload(
    const T* vertexBuffer,
    size_t vertices,
    const std::vector<IndexBufferData>& indices
) {
    auto* d = reinterpret_cast<Mesh3DSData*>(vao);
    if (d->vbo) {
        retire(d->vbo);
        d->vbo = nullptr;
    }
    for (auto& ibo : d->ibos) {
        retire(ibo.data);
    }
    d->ibos.clear();
    d->vertexCount = 0;

    if (vertexBuffer && vertices) {
        // PICA indices are 16-bit
        size_t count = vertices > 65535 ? 65535 : vertices;
        if (count != vertices) {
            logger.warning() << "mesh truncated: " << vertices << " vertices";
        }
        d->vbo = linearAlloc(count * sizeof(T));
        if (!d->vbo) {
            logger.error() << "linearAlloc failed for " << count << " vertices";
            return;
        }
        std::memcpy(d->vbo, vertexBuffer, count * sizeof(T));
        d->vertexCount = count;
    }
    vertexCount = d->vertexCount;

    for (const auto& src : indices) {
        Mesh3DSData::Ibo ibo;
        ibo.count = src.indicesCount;
        ibo.data = reinterpret_cast<u16*>(linearAlloc(ibo.count * sizeof(u16)));
        if (!ibo.data) {
            logger.error() << "linearAlloc failed for " << ibo.count << " indices";
            continue;
        }
        size_t kept = 0;
        for (size_t i = 0; i < src.indicesCount; i++) {
            uint32_t v = src.indices[i];
            if (v > 65535) {
                v = 0;  // truncated vertex; degenerate triangle
            }
            ibo.data[kept++] = static_cast<u16>(v);
        }
        ibo.count = kept;
        d->ibos.push_back(ibo);
    }
}

template <typename T>
void Mesh<T>::draw(unsigned int primitive, int iboIndex) const {
    auto* d = reinterpret_cast<Mesh3DSData*>(vao);
    if (!d->vbo || d->vertexCount == 0) {
        return;
    }
    // PICA has no line/point primitives (DrawPrimitive 0/1, GL_LINES=1)
    if (primitive <= 1) {
        return;
    }
    MeshStats::drawCalls++;

    C3D_BufInfo* bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, d->vbo, d->stride, d->attrCount, d->permutation);

    if (!d->ibos.empty()) {
        if (iboIndex >= 0 && iboIndex < (int)d->ibos.size() &&
            d->ibos[iboIndex].data && d->ibos[iboIndex].count) {
            C3D_DrawElements(
                GPU_TRIANGLES,
                d->ibos[iboIndex].count,
                C3D_UNSIGNED_SHORT,
                d->ibos[iboIndex].data);
        }
    } else {
        C3D_DrawArrays(GPU_TRIANGLES, 0, d->vertexCount);
    }
}

template <typename T>
void Mesh<T>::draw() const {
    draw(4 /* GL_TRIANGLES */, 0);
}

template class Mesh<ChunkVertex>;
template class Mesh<Batch2DVertex>;
template class Mesh<Batch3DVertex>;
template class Mesh<MainBatchVertex>;
