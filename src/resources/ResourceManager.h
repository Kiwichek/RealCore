#pragma once
#include <resources/Mesh.h>
#include <resources/Texture.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;
struct aiNode;

using MeshHandle = uint32_t;
using TextureHandle = uint32_t;

class ResourceManager {
public:
    bool init();
    void shutdown();

    MeshHandle loadMesh(const std::string& path);
    MeshHandle createBoxMesh(const Vec3& halfExtent);
    MeshHandle createSphereMesh(float radius, int segments = 24);
    MeshHandle createCapsuleMesh(float halfHeight, float radius, int segments = 16);
    MeshHandle createCylinderMesh(float halfHeight, float radius, int segments = 24);
    TextureHandle loadTexture(const std::string& path);

    Mesh* getMesh(MeshHandle handle);
    Texture* getTexture(TextureHandle handle);
    const std::string& meshSourcePath(MeshHandle handle) const;
    const std::string& meshAssetKind(MeshHandle handle) const;

    void destroyMesh(MeshHandle handle);
    void destroyTexture(TextureHandle handle);

private:
    struct MeshAssetInfo {
        std::string sourcePath;
        std::string kind;
    };

    void setMeshAssetInfo(MeshHandle handle, const std::string& sourcePath, const std::string& kind);

    std::vector<std::unique_ptr<Mesh>> m_meshes;
    std::vector<MeshAssetInfo> m_meshAssetInfo;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::unordered_map<std::string, MeshHandle> m_meshCache;
    std::unordered_map<std::string, TextureHandle> m_textureCache;
    std::vector<std::string> m_zipExtractDirs;
    MeshHandle m_nextMeshHandle = 1;
    TextureHandle m_nextTextureHandle = 1;
};
