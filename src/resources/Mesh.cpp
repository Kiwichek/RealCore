#include <resources/Mesh.h>
#include <algorithm>

Mesh::Mesh(Mesh&& other) noexcept
    : m_vbuf(other.m_vbuf)
    , m_ibuf(other.m_ibuf)
    , m_vertexCount(other.m_vertexCount)
    , m_indexCount(other.m_indexCount)
    , m_submeshes(std::move(other.m_submeshes))
    , m_materials(std::move(other.m_materials))
    , m_bounds(other.m_bounds)
{
    other.m_vbuf = {};
    other.m_ibuf = {};
    other.m_vertexCount = 0;
    other.m_indexCount = 0;
    other.m_bounds = {};
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        m_vbuf = other.m_vbuf;
        m_ibuf = other.m_ibuf;
        m_vertexCount = other.m_vertexCount;
        m_indexCount = other.m_indexCount;
        m_submeshes = std::move(other.m_submeshes);
        m_materials = std::move(other.m_materials);
        m_bounds = other.m_bounds;
        other.m_vbuf = {};
        other.m_ibuf = {};
        other.m_vertexCount = 0;
        other.m_indexCount = 0;
        other.m_bounds = {};
    }
    return *this;
}

Mesh::~Mesh() {
    destroy();
}

bool Mesh::load(const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices,
                const std::vector<SubMesh>& submeshes)
{
    destroy();

    if (vertices.empty()) return false;

    m_vertexCount = (int)vertices.size();
    m_indexCount = (int)indices.size();
    m_submeshes = submeshes;
    m_bounds = {};
    m_bounds.min = { vertices[0].px, vertices[0].py, vertices[0].pz };
    m_bounds.max = m_bounds.min;

    for (const auto& v : vertices) {
        m_bounds.min.x = std::min(m_bounds.min.x, v.px);
        m_bounds.min.y = std::min(m_bounds.min.y, v.py);
        m_bounds.min.z = std::min(m_bounds.min.z, v.pz);
        m_bounds.max.x = std::max(m_bounds.max.x, v.px);
        m_bounds.max.y = std::max(m_bounds.max.y, v.py);
        m_bounds.max.z = std::max(m_bounds.max.z, v.pz);
    }

    m_bounds.center = (m_bounds.min + m_bounds.max) * 0.5f;
    Vec3 extents = (m_bounds.max - m_bounds.min) * 0.5f;
    m_bounds.radius = std::max(extents.length(), 0.001f);
    m_bounds.valid = true;

    sg_buffer_desc vbufDesc = {};
    vbufDesc.usage.vertex_buffer = true;
    vbufDesc.usage.immutable = true;
    vbufDesc.data.ptr = vertices.data();
    vbufDesc.data.size = vertices.size() * sizeof(Vertex);
    vbufDesc.label = "mesh-vertices";
    m_vbuf = sg_make_buffer(&vbufDesc);

    if (!indices.empty()) {
        sg_buffer_desc ibufDesc = {};
        ibufDesc.usage.index_buffer = true;
        ibufDesc.usage.immutable = true;
        ibufDesc.data.ptr = indices.data();
        ibufDesc.data.size = indices.size() * sizeof(uint32_t);
        ibufDesc.label = "mesh-indices";
        m_ibuf = sg_make_buffer(&ibufDesc);
    }

    return m_vbuf.id != SG_INVALID_ID;
}

void Mesh::destroy() {
    if (m_vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vbuf);
        m_vbuf = {};
    }
    if (m_ibuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_ibuf);
        m_ibuf = {};
    }
    for (auto& mat : m_materials) {
        if (mat.textureView.id != SG_INVALID_ID) {
            sg_destroy_view(mat.textureView);
        }
        if (mat.sampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(mat.sampler);
        }
        if (mat.normalTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(mat.normalTextureView);
        }
        if (mat.normalSampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(mat.normalSampler);
        }
        if (mat.metallicTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(mat.metallicTextureView);
        }
        if (mat.metallicSampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(mat.metallicSampler);
        }
        if (mat.roughnessTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(mat.roughnessTextureView);
        }
        if (mat.roughnessSampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(mat.roughnessSampler);
        }
        if (mat.metallicRoughnessTextureView.id != SG_INVALID_ID) {
            sg_destroy_view(mat.metallicRoughnessTextureView);
        }
        if (mat.metallicRoughnessSampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(mat.metallicRoughnessSampler);
        }
    }
    m_materials.clear();
    m_vertexCount = 0;
    m_indexCount = 0;
    m_submeshes.clear();
    m_bounds = {};
}
