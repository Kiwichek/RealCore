#pragma once
#include <core/math.h>
#include <sokol_gfx.h>
#include <cstdint>
#include <vector>

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float tu, tv;
};

static_assert(sizeof(Vertex) == 32, "Vertex must be 32 bytes");

struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct MeshBounds {
    Vec3 min;
    Vec3 max;
    Vec3 center;
    float radius = 0.0f;
    bool valid = false;
};

struct MaterialData {
    float baseColor[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.5f;
    sg_view textureView = {};
    sg_sampler sampler = {};
    sg_view normalTextureView = {};
    sg_sampler normalSampler = {};
    sg_view metallicTextureView = {};
    sg_sampler metallicSampler = {};
    sg_view roughnessTextureView = {};
    sg_sampler roughnessSampler = {};
    sg_view metallicRoughnessTextureView = {};
    sg_sampler metallicRoughnessSampler = {};
    bool hasTexture = false;
    bool hasNormalTexture = false;
    bool hasMetallicTexture = false;
    bool hasRoughnessTexture = false;
    bool hasMetallicRoughnessTexture = false;
};

class Mesh {
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();

    bool load(const std::vector<Vertex>& vertices,
              const std::vector<uint32_t>& indices,
              const std::vector<SubMesh>& submeshes);
    void destroy();

    void setMaterials(std::vector<MaterialData> materials) { m_materials = std::move(materials); }
    std::vector<MaterialData>& materials() { return m_materials; }
    const std::vector<MaterialData>& materials() const { return m_materials; }

    sg_buffer vertexBuffer() const { return m_vbuf; }
    sg_buffer indexBuffer() const { return m_ibuf; }
    int vertexCount() const { return m_vertexCount; }
    int indexCount() const { return m_indexCount; }
    const std::vector<SubMesh>& submeshes() const { return m_submeshes; }
    const MeshBounds& bounds() const { return m_bounds; }

private:
    sg_buffer m_vbuf = {};
    sg_buffer m_ibuf = {};
    int m_vertexCount = 0;
    int m_indexCount = 0;
    std::vector<SubMesh> m_submeshes;
    std::vector<MaterialData> m_materials;
    MeshBounds m_bounds;
};
