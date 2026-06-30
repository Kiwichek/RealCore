#include <resources/ResourceManager.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/matrix4x4.h>
#include <assimp/postprocess.h>
#include <assimp/texture.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>
#include <zlib.h>
namespace fs = std::filesystem;

static std::string lowerCopy(std::string value);

namespace {

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localHeaderOffset = 0;
};

uint16_t readLe16(const std::vector<unsigned char>& data, size_t offset) {
    return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

uint32_t readLe32(const std::vector<unsigned char>& data, size_t offset) {
    return (uint32_t)data[offset] |
        ((uint32_t)data[offset + 1] << 8) |
        ((uint32_t)data[offset + 2] << 16) |
        ((uint32_t)data[offset + 3] << 24);
}

bool hasModelExtension(const fs::path& path) {
    std::string ext = lowerCopy(path.extension().string());
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

int modelExtensionScore(const fs::path& path) {
    std::string ext = lowerCopy(path.extension().string());
    if (ext == ".fbx") return 40;
    if (ext == ".gltf") return 35;
    if (ext == ".glb") return 30;
    if (ext == ".obj") return 25;
    return 0;
}

bool isSafeZipPath(const std::string& name, fs::path& outPath) {
    std::string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    fs::path path(normalized);
    if (path.empty() || path.is_absolute()) {
        return false;
    }

    for (const auto& part : path) {
        if (part == "..") {
            return false;
        }
    }

    outPath = path.lexically_normal();
    return !outPath.empty();
}

bool inflateRawDeflate(const unsigned char* input, size_t inputSize, std::vector<unsigned char>& output) {
    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }

    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = (uInt)inputSize;
    stream.next_out = output.data();
    stream.avail_out = (uInt)output.size();

    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return result == Z_STREAM_END;
}

bool readFileBytes(const fs::path& path, std::vector<unsigned char>& outBytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    outBytes.resize((size_t)size);
    file.read((char*)outBytes.data(), size);
    return file.good();
}

bool parseZipEntries(const std::vector<unsigned char>& data, std::vector<ZipEntry>& entries) {
    if (data.size() < 22) {
        return false;
    }

    std::optional<size_t> eocdOffset;
    size_t searchStart = data.size() > 0x10000 + 22 ? data.size() - (0x10000 + 22) : 0;
    for (size_t i = data.size() - 22; i + 1 > searchStart; i--) {
        if (readLe32(data, i) == 0x06054b50u) {
            eocdOffset = i;
            break;
        }
        if (i == 0) {
            break;
        }
    }

    if (!eocdOffset) {
        return false;
    }

    size_t eocd = *eocdOffset;
    uint16_t entryCount = readLe16(data, eocd + 10);
    uint32_t centralSize = readLe32(data, eocd + 12);
    uint32_t centralOffset = readLe32(data, eocd + 16);
    if ((uint64_t)centralOffset + centralSize > data.size()) {
        return false;
    }

    size_t offset = centralOffset;
    for (uint16_t i = 0; i < entryCount; i++) {
        if (offset + 46 > data.size() || readLe32(data, offset) != 0x02014b50u) {
            return false;
        }

        uint16_t nameLen = readLe16(data, offset + 28);
        uint16_t extraLen = readLe16(data, offset + 30);
        uint16_t commentLen = readLe16(data, offset + 32);
        if (offset + 46u + nameLen + extraLen + commentLen > data.size()) {
            return false;
        }

        ZipEntry entry;
        entry.flags = readLe16(data, offset + 8);
        entry.method = readLe16(data, offset + 10);
        entry.compressedSize = readLe32(data, offset + 20);
        entry.uncompressedSize = readLe32(data, offset + 24);
        entry.localHeaderOffset = readLe32(data, offset + 42);
        entry.name.assign((const char*)data.data() + offset + 46, nameLen);
        entries.push_back(std::move(entry));

        offset += 46u + nameLen + extraLen + commentLen;
    }

    return true;
}

bool extractZipEntry(const std::vector<unsigned char>& zipData, const ZipEntry& entry, const fs::path& outputRoot) {
    if ((entry.flags & 0x1) != 0) {
        return true;
    }

    fs::path safePath;
    if (!isSafeZipPath(entry.name, safePath)) {
        return true;
    }

    std::string entryName = entry.name;
    if (!entryName.empty() && (entryName.back() == '/' || entryName.back() == '\\')) {
        fs::create_directories(outputRoot / safePath);
        return true;
    }

    size_t local = entry.localHeaderOffset;
    if (local + 30 > zipData.size() || readLe32(zipData, local) != 0x04034b50u) {
        return false;
    }

    uint16_t nameLen = readLe16(zipData, local + 26);
    uint16_t extraLen = readLe16(zipData, local + 28);
    size_t dataOffset = local + 30u + nameLen + extraLen;
    if (dataOffset + entry.compressedSize > zipData.size()) {
        return false;
    }

    std::vector<unsigned char> output;
    if (entry.method == 0) {
        output.assign(zipData.begin() + dataOffset, zipData.begin() + dataOffset + entry.compressedSize);
    } else if (entry.method == 8) {
        output.resize(entry.uncompressedSize);
        if (!inflateRawDeflate(zipData.data() + dataOffset, entry.compressedSize, output)) {
            return false;
        }
    } else {
        return true;
    }

    fs::path outputPath = outputRoot / safePath;
    fs::create_directories(outputPath.parent_path());
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write((const char*)output.data(), (std::streamsize)output.size());
    return out.good();
}

std::optional<fs::path> selectModelFromExtractedZip(const std::vector<ZipEntry>& entries, const fs::path& outputRoot) {
    fs::path bestPath;
    int bestScore = 0;

    for (const ZipEntry& entry : entries) {
        fs::path safePath;
        if (!isSafeZipPath(entry.name, safePath) || !hasModelExtension(safePath)) {
            continue;
        }

        std::string lowerName = lowerCopy(safePath.generic_string());
        if (lowerName.find("__macosx") != std::string::npos) {
            continue;
        }

        int depthPenalty = (int)std::distance(safePath.begin(), safePath.end());
        int score = modelExtensionScore(safePath) - depthPenalty;
        std::string stem = lowerCopy(safePath.stem().string());
        if (stem.find("scene") != std::string::npos || stem.find("model") != std::string::npos) {
            score += 5;
        }

        if (score > bestScore) {
            bestScore = score;
            bestPath = outputRoot / safePath;
        }
    }

    if (bestScore <= 0) {
        return std::nullopt;
    }
    return bestPath.lexically_normal();
}

} // namespace

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](char c) { return (char)std::tolower((unsigned char)c); });
    return value;
}

static bool containsAnyToken(const std::string& value, const std::vector<std::string>& tokens) {
    for (const std::string& token : tokens) {
        if (value.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

static bool isSupportedTextureExtension(const fs::path& path) {
    std::string ext = lowerCopy(path.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".tga" || ext == ".bmp" || ext == ".hdr";
}

static bool looksLikeAlbedoTextureName(const std::string& name) {
    std::string lower = lowerCopy(name);
    static const std::vector<std::string> colorTokens = {
        "albedo", "basecolor", "base_color", "base-color", "diffuse", "_diff", "-diff", "color", "colour"
    };
    static const std::vector<std::string> nonColorTokens = {
        "normal", "_nrm", "-nrm", "rough", "metal", "metallic", "spec", "ao", "ambient",
        "height", "bump", "disp", "emiss", "opacity", "alpha", "mask"
    };
    return containsAnyToken(lower, colorTokens) && !containsAnyToken(lower, nonColorTokens);
}

static bool looksLikeNormalTextureName(const std::string& name) {
    std::string lower = lowerCopy(name);
    static const std::vector<std::string> normalTokens = {
        "normal", "_norm", "-norm", "_nrm", "-nrm", "normalgl", "normaldx", "bump"
    };
    static const std::vector<std::string> nonNormalTokens = {
        "albedo", "basecolor", "base_color", "diffuse", "color", "colour",
        "rough", "metal", "metallic", "spec", "ao", "height", "disp",
        "emiss", "opacity", "alpha", "mask"
    };
    return containsAnyToken(lower, normalTokens) && !containsAnyToken(lower, nonNormalTokens);
}

static bool looksLikeMetallicTextureName(const std::string& name) {
    std::string lower = lowerCopy(name);
    static const std::vector<std::string> metallicTokens = {
        "metallic", "metalness", "_metal", "-metal", "_met", "-met"
    };
    static const std::vector<std::string> nonMetallicTokens = {
        "rough", "normal", "albedo", "basecolor", "diffuse", "color", "ao",
        "height", "bump", "emiss", "opacity", "alpha", "mask"
    };
    return containsAnyToken(lower, metallicTokens) && !containsAnyToken(lower, nonMetallicTokens);
}

static bool looksLikeRoughnessTextureName(const std::string& name) {
    std::string lower = lowerCopy(name);
    static const std::vector<std::string> roughnessTokens = {
        "roughness", "_rough", "-rough", "_rgh", "-rgh"
    };
    static const std::vector<std::string> nonRoughnessTokens = {
        "metal", "normal", "albedo", "basecolor", "diffuse", "color", "ao",
        "height", "bump", "emiss", "opacity", "alpha", "mask"
    };
    return containsAnyToken(lower, roughnessTokens) && !containsAnyToken(lower, nonRoughnessTokens);
}

static bool looksLikeMetallicRoughnessTextureName(const std::string& name) {
    std::string lower = lowerCopy(name);
    static const std::vector<std::string> combinedTokens = {
        "metallicroughness", "metallic_roughness", "metallic-roughness",
        "metalrough", "metal_rough", "orm", "_rma", "-rma"
    };
    static const std::vector<std::string> nonCombinedTokens = {
        "normal", "albedo", "basecolor", "diffuse", "height", "bump",
        "emiss", "opacity", "alpha"
    };
    return containsAnyToken(lower, combinedTokens) && !containsAnyToken(lower, nonCombinedTokens);
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static std::string decodeTextureRef(const char* value) {
    std::string ref = value ? value : "";
    const std::string filePrefix = "file://";
    if (ref.rfind(filePrefix, 0) == 0) {
        ref.erase(0, filePrefix.size());
    }

    std::string decoded;
    decoded.reserve(ref.size());
    for (size_t i = 0; i < ref.size(); i++) {
        if (ref[i] == '%' && i + 2 < ref.size()) {
            int hi = hexDigit(ref[i + 1]);
            int lo = hexDigit(ref[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(ref[i]);
    }
    return decoded;
}

static aiVector3D transformNormal(const aiMatrix4x4& m, const aiVector3D& n) {
    aiVector3D out(
        m.a1 * n.x + m.a2 * n.y + m.a3 * n.z,
        m.b1 * n.x + m.b2 * n.y + m.b3 * n.z,
        m.c1 * n.x + m.c2 * n.y + m.c3 * n.z);
    out.NormalizeSafe();
    return out;
}

static void processAssimpNode(
    const aiScene* scene,
    const aiNode* node,
    const aiMatrix4x4& transform,
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<SubMesh>& submeshes)
{
    aiMatrix4x4 nodeTransform = transform * node->mTransformation;

    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        const aiMesh* aimesh = scene->mMeshes[node->mMeshes[i]];
        if (!aimesh->HasPositions()) continue;

        unsigned int vertexOffset = (unsigned int)vertices.size();

        for (unsigned int v = 0; v < aimesh->mNumVertices; v++) {
            Vertex vert = {};

            aiVector3D pos = nodeTransform * aimesh->mVertices[v];
            vert.px = pos.x;
            vert.py = pos.y;
            vert.pz = pos.z;

            if (aimesh->HasNormals()) {
                aiVector3D normal = transformNormal(nodeTransform, aimesh->mNormals[v]);
                vert.nx = normal.x;
                vert.ny = normal.y;
                vert.nz = normal.z;
            }

            if (aimesh->HasTextureCoords(0)) {
                vert.tu = aimesh->mTextureCoords[0][v].x;
                vert.tv = aimesh->mTextureCoords[0][v].y;
            }

            vertices.push_back(vert);
        }

        SubMesh sm;
        sm.indexOffset = (uint32_t)indices.size();
        sm.materialIndex = aimesh->mMaterialIndex;

        for (unsigned int f = 0; f < aimesh->mNumFaces; f++) {
            const auto& face = aimesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(vertexOffset + face.mIndices[j]);
            }
        }

        sm.indexCount = (uint32_t)indices.size() - sm.indexOffset;
        submeshes.push_back(sm);
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processAssimpNode(scene, node->mChildren[i], nodeTransform,
                          vertices, indices, submeshes);
    }
}

bool ResourceManager::init() {
    return true;
}

void ResourceManager::shutdown() {
    m_meshes.clear();
    m_meshAssetInfo.clear();
    m_textures.clear();
    m_meshCache.clear();
    m_textureCache.clear();
    for (const std::string& dir : m_zipExtractDirs) {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    m_zipExtractDirs.clear();
    m_nextMeshHandle = 1;
    m_nextTextureHandle = 1;
}

MeshHandle ResourceManager::loadMesh(const std::string& path) {
    const std::string meshPath = fs::absolute(fs::path(path)).lexically_normal().string();

    auto it = m_meshCache.find(meshPath);
    if (it != m_meshCache.end()) {
        return it->second;
    }

    if (lowerCopy(fs::path(meshPath).extension().string()) == ".zip") {
        std::vector<unsigned char> zipBytes;
        std::vector<ZipEntry> entries;
        if (!readFileBytes(meshPath, zipBytes) || !parseZipEntries(zipBytes, entries)) {
            return 0;
        }

        uintmax_t zipSize = 0;
        std::error_code ec;
        zipSize = fs::file_size(meshPath, ec);
        std::string cacheKey = meshPath + "|" + std::to_string(zipSize);
        size_t hash = std::hash<std::string>{}(cacheKey);
        fs::path extractRoot = fs::temp_directory_path() / "RealCoreZipAssets" /
            (fs::path(meshPath).stem().string() + "_" + std::to_string(hash));

        fs::remove_all(extractRoot, ec);
        fs::create_directories(extractRoot, ec);
        if (ec) {
            return 0;
        }

        for (const ZipEntry& entry : entries) {
            if (!extractZipEntry(zipBytes, entry, extractRoot)) {
                fs::remove_all(extractRoot, ec);
                return 0;
            }
        }

        std::optional<fs::path> modelPath = selectModelFromExtractedZip(entries, extractRoot);
        if (!modelPath) {
            fs::remove_all(extractRoot, ec);
            return 0;
        }

        MeshHandle handle = loadMesh(modelPath->string());
        if (handle == 0) {
            fs::remove_all(extractRoot, ec);
            return 0;
        }

        m_meshCache[meshPath] = handle;
        setMeshAssetInfo(handle, meshPath, "file");
        m_zipExtractDirs.push_back(extractRoot.string());
        return handle;
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(meshPath,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_CalcTangentSpace |
        aiProcess_ImproveCacheLocality |
        aiProcess_OptimizeMeshes |
        aiProcess_OptimizeGraph);

    if (!scene || !scene->mRootNode) {
        return 0;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;

    aiMatrix4x4 identity;
    processAssimpNode(scene, scene->mRootNode, identity,
                      vertices, indices, submeshes);

    if (vertices.empty()) {
        return 0;
    }

    auto mesh = std::make_unique<Mesh>();
    if (!mesh->load(vertices, indices, submeshes)) {
        return 0;
    }

    // extract materials
    {
        fs::path modelDir = fs::path(meshPath).parent_path();

        auto loadEmbeddedTexture = [&](const aiTexture* aiTex, const std::string& cacheKey) -> TextureHandle {
            if (!aiTex || !aiTex->pcData) {
                return 0;
            }

            auto texIt = m_textureCache.find(cacheKey);
            if (texIt != m_textureCache.end()) {
                return texIt->second;
            }

            auto tex = std::make_unique<Texture>();
            bool loaded = false;

            if (aiTex->mHeight == 0) {
                loaded = tex->loadFromMemory(aiTex->pcData, aiTex->mWidth);
            } else {
                std::vector<unsigned char> rgba((size_t)aiTex->mWidth * (size_t)aiTex->mHeight * 4);
                for (size_t p = 0; p < (size_t)aiTex->mWidth * (size_t)aiTex->mHeight; p++) {
                    const aiTexel& src = aiTex->pcData[p];
                    rgba[p * 4 + 0] = src.r;
                    rgba[p * 4 + 1] = src.g;
                    rgba[p * 4 + 2] = src.b;
                    rgba[p * 4 + 3] = src.a;
                }
                loaded = tex->loadFromRGBA8(rgba.data(), (int)aiTex->mWidth, (int)aiTex->mHeight);
            }

            if (!loaded) {
                return 0;
            }

            TextureHandle handle = m_nextTextureHandle++;
            m_textures.push_back(std::move(tex));
            m_textureCache[cacheKey] = handle;
            return handle;
        };

        auto loadTextureRef = [&](const char* rawRef, const std::string& cachePrefix) -> TextureHandle {
            if (!rawRef || rawRef[0] == '\0') {
                return 0;
            }

            std::string decodedRef = decodeTextureRef(rawRef);

            const aiTexture* embedded = scene->GetEmbeddedTexture(rawRef);
            if (!embedded && decodedRef != rawRef) {
                embedded = scene->GetEmbeddedTexture(decodedRef.c_str());
            }
            if (embedded) {
                return loadEmbeddedTexture(embedded, cachePrefix + "|embedded|" + decodedRef);
            }

            fs::path texPath(decodedRef);
            if (!texPath.is_absolute()) {
                texPath = modelDir / texPath;
            }
            return loadTexture(texPath.lexically_normal().string());
        };

        auto loadMaterialTexture = [&](aiMaterial* aiMat, aiTextureType type, unsigned int index = 0) -> TextureHandle {
            aiString texRelPath;
            if (aiMat->GetTexture(type, index, &texRelPath) != AI_SUCCESS) {
                return 0;
            }

            return loadTextureRef(texRelPath.C_Str(), meshPath);
        };

        auto loadNamedAlbedoTexture = [&](aiMaterial* aiMat, aiTextureType type) -> TextureHandle {
            unsigned int count = aiMat->GetTextureCount(type);
            for (unsigned int index = 0; index < count; index++) {
                aiString texRelPath;
                if (aiMat->GetTexture(type, index, &texRelPath) != AI_SUCCESS) {
                    continue;
                }
                if (!looksLikeAlbedoTextureName(texRelPath.C_Str())) {
                    continue;
                }
                TextureHandle handle = loadTextureRef(texRelPath.C_Str(), meshPath);
                if (handle != 0) {
                    return handle;
                }
            }
            return 0;
        };

        auto loadNamedNormalTexture = [&](aiMaterial* aiMat, aiTextureType type) -> TextureHandle {
            unsigned int count = aiMat->GetTextureCount(type);
            for (unsigned int index = 0; index < count; index++) {
                aiString texRelPath;
                if (aiMat->GetTexture(type, index, &texRelPath) != AI_SUCCESS) {
                    continue;
                }
                if (!looksLikeNormalTextureName(texRelPath.C_Str())) {
                    continue;
                }
                TextureHandle handle = loadTextureRef(texRelPath.C_Str(), meshPath);
                if (handle != 0) {
                    return handle;
                }
            }
            return 0;
        };

        auto loadNamedTexture = [&](aiMaterial* aiMat, aiTextureType type, bool (*namePredicate)(const std::string&)) -> TextureHandle {
            unsigned int count = aiMat->GetTextureCount(type);
            for (unsigned int index = 0; index < count; index++) {
                aiString texRelPath;
                if (aiMat->GetTexture(type, index, &texRelPath) != AI_SUCCESS) {
                    continue;
                }
                if (!namePredicate(texRelPath.C_Str())) {
                    continue;
                }
                TextureHandle handle = loadTextureRef(texRelPath.C_Str(), meshPath);
                if (handle != 0) {
                    return handle;
                }
            }
            return 0;
        };

        auto textureNameMatches = [](const std::string& filename, int semantic) {
            switch (semantic) {
                case 0: return looksLikeAlbedoTextureName(filename);
                case 1: return looksLikeNormalTextureName(filename);
                case 2: return looksLikeMetallicTextureName(filename);
                case 3: return looksLikeRoughnessTextureName(filename);
                case 4: return looksLikeMetallicRoughnessTextureName(filename);
                default: return false;
            }
        };

        auto loadLooseTexture = [&](const std::string& materialName, int semantic) -> TextureHandle {
            std::string modelStem = lowerCopy(fs::path(meshPath).stem().string());
            std::string materialLower = lowerCopy(materialName);

            fs::path bestPath;
            int bestScore = 0;

            try {
                for (const auto& entry : fs::recursive_directory_iterator(modelDir, fs::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file() || !isSupportedTextureExtension(entry.path())) {
                        continue;
                    }

                    std::string filename = lowerCopy(entry.path().filename().string());
                    bool matches = textureNameMatches(filename, semantic);
                    if (!matches) {
                        continue;
                    }

                    int score = 10;
                    if (!materialLower.empty() && filename.find(materialLower) != std::string::npos) {
                        score += 20;
                    }
                    if (!modelStem.empty() && filename.find(modelStem) != std::string::npos) {
                        score += 10;
                    }

                    if (score > bestScore) {
                        bestScore = score;
                        bestPath = entry.path();
                    }
                }
            } catch (...) {
            }

            if (bestScore == 0) {
                return 0;
            }
            return loadTexture(bestPath.lexically_normal().string());
        };

        std::vector<MaterialData> materials;
        for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
            aiMaterial* aiMat = scene->mMaterials[i];
            MaterialData mat;

            aiColor4D aiColor(0.8f, 0.8f, 0.8f, 1.0f);
            if (aiMat->Get(AI_MATKEY_BASE_COLOR, aiColor) != AI_SUCCESS) {
                aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, aiColor);
            }
            mat.baseColor[0] = aiColor.r;
            mat.baseColor[1] = aiColor.g;
            mat.baseColor[2] = aiColor.b;
            mat.baseColor[3] = aiColor.a;
            aiMat->Get(AI_MATKEY_METALLIC_FACTOR, mat.metallicFactor);
            aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, mat.roughnessFactor);
            mat.metallicFactor = std::clamp(mat.metallicFactor, 0.0f, 1.0f);
            mat.roughnessFactor = std::clamp(mat.roughnessFactor, 0.04f, 1.0f);

            TextureHandle th = loadMaterialTexture(aiMat, aiTextureType_BASE_COLOR);
            if (th == 0) {
                th = loadMaterialTexture(aiMat, aiTextureType_DIFFUSE);
            }
            if (th == 0) {
                th = loadNamedAlbedoTexture(aiMat, aiTextureType_UNKNOWN);
            }
            if (th == 0) {
                aiString materialName;
                aiMat->Get(AI_MATKEY_NAME, materialName);
                th = loadLooseTexture(materialName.C_Str(), 0);
            }

            auto makeMaterialTextureBindings = [&](TextureHandle handle, sg_view& view, sg_sampler& sampler, bool& hasTexture) {
                Texture* tex = getTexture(handle);
                if (!tex) {
                    return;
                }

                sg_view_desc vd = {};
                vd.texture.image = tex->image();
                view = sg_make_view(&vd);

                sg_sampler_desc sd = {};
                sd.min_filter = SG_FILTER_LINEAR;
                sd.mag_filter = SG_FILTER_LINEAR;
                sd.wrap_u = SG_WRAP_REPEAT;
                sd.wrap_v = SG_WRAP_REPEAT;
                sampler = sg_make_sampler(&sd);

                hasTexture =
                    view.id != SG_INVALID_ID &&
                    sampler.id != SG_INVALID_ID;
            };

            if (th != 0) {
                makeMaterialTextureBindings(th, mat.textureView, mat.sampler, mat.hasTexture);
            }

            TextureHandle normalTh = loadMaterialTexture(aiMat, aiTextureType_NORMALS);
            if (normalTh == 0) {
                normalTh = loadMaterialTexture(aiMat, aiTextureType_NORMAL_CAMERA);
            }
            if (normalTh == 0) {
                normalTh = loadNamedNormalTexture(aiMat, aiTextureType_UNKNOWN);
            }
            if (normalTh == 0) {
                normalTh = loadNamedNormalTexture(aiMat, aiTextureType_HEIGHT);
            }
            if (normalTh == 0) {
                aiString materialName;
                aiMat->Get(AI_MATKEY_NAME, materialName);
                normalTh = loadLooseTexture(materialName.C_Str(), 1);
            }

            if (normalTh != 0) {
                makeMaterialTextureBindings(normalTh, mat.normalTextureView, mat.normalSampler, mat.hasNormalTexture);
            }

            TextureHandle metallicRoughnessTh = loadMaterialTexture(aiMat, aiTextureType_GLTF_METALLIC_ROUGHNESS);
            if (metallicRoughnessTh == 0) {
                metallicRoughnessTh = loadNamedTexture(aiMat, aiTextureType_UNKNOWN, looksLikeMetallicRoughnessTextureName);
            }
            if (metallicRoughnessTh == 0) {
                aiString materialName;
                aiMat->Get(AI_MATKEY_NAME, materialName);
                metallicRoughnessTh = loadLooseTexture(materialName.C_Str(), 4);
            }
            if (metallicRoughnessTh != 0) {
                makeMaterialTextureBindings(metallicRoughnessTh, mat.metallicRoughnessTextureView, mat.metallicRoughnessSampler, mat.hasMetallicRoughnessTexture);
            }

            TextureHandle metallicTh = loadMaterialTexture(aiMat, aiTextureType_METALNESS);
            if (metallicTh == 0) {
                metallicTh = loadNamedTexture(aiMat, aiTextureType_UNKNOWN, looksLikeMetallicTextureName);
            }
            if (metallicTh == 0) {
                aiString materialName;
                aiMat->Get(AI_MATKEY_NAME, materialName);
                metallicTh = loadLooseTexture(materialName.C_Str(), 2);
            }
            if (metallicTh != 0) {
                makeMaterialTextureBindings(metallicTh, mat.metallicTextureView, mat.metallicSampler, mat.hasMetallicTexture);
            }

            TextureHandle roughnessTh = loadMaterialTexture(aiMat, aiTextureType_DIFFUSE_ROUGHNESS);
            if (roughnessTh == 0) {
                roughnessTh = loadNamedTexture(aiMat, aiTextureType_UNKNOWN, looksLikeRoughnessTextureName);
            }
            if (roughnessTh == 0) {
                roughnessTh = loadNamedTexture(aiMat, aiTextureType_MAYA_SPECULAR_ROUGHNESS, looksLikeRoughnessTextureName);
            }
            if (roughnessTh == 0) {
                aiString materialName;
                aiMat->Get(AI_MATKEY_NAME, materialName);
                roughnessTh = loadLooseTexture(materialName.C_Str(), 3);
            }
            if (roughnessTh != 0) {
                makeMaterialTextureBindings(roughnessTh, mat.roughnessTextureView, mat.roughnessSampler, mat.hasRoughnessTexture);
            }

            materials.push_back(mat);
        }
        if (!materials.empty()) {
            mesh->setMaterials(std::move(materials));
        }
    }

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes.push_back(std::move(mesh));
    setMeshAssetInfo(handle, meshPath, "file");
    m_meshCache[meshPath] = handle;

    return handle;
}

MeshHandle ResourceManager::createBoxMesh(const Vec3& halfExtent) {
    float hx = halfExtent.x, hy = halfExtent.y, hz = halfExtent.z;

    Vertex verts[24] = {};
    uint32_t inds[36] = {};

    // Unit normals per face
    static const float ns[6][3] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };
    // Face vertex offsets relative to center
    static const float fs[6][4][3] = {
        {{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}},     // +Z
        {{1,-1,-1},{-1,-1,-1},{-1,1,-1},{1,1,-1}}, // -Z
        {{1,-1,1},{1,-1,-1},{1,1,-1},{1,1,1}},     // +X
        {{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}}, // -X
        {{-1,1,1},{1,1,1},{1,1,-1},{-1,1,-1}},     // +Y
        {{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}}, // -Y
    };

    int vi = 0, ii = 0;
    for (int f = 0; f < 6; f++) {
        for (int v = 0; v < 4; v++) {
            verts[vi].px = fs[f][v][0] * hx;
            verts[vi].py = fs[f][v][1] * hy;
            verts[vi].pz = fs[f][v][2] * hz;
            verts[vi].nx = ns[f][0];
            verts[vi].ny = ns[f][1];
            verts[vi].nz = ns[f][2];
            verts[vi].tu = (v == 1 || v == 2) ? 1.0f : 0.0f;
            verts[vi].tv = (v == 2 || v == 3) ? 1.0f : 0.0f;
            vi++;
        }
        inds[ii++] = f*4+0; inds[ii++] = f*4+1; inds[ii++] = f*4+2;
        inds[ii++] = f*4+0; inds[ii++] = f*4+2; inds[ii++] = f*4+3;
    }

    std::vector<Vertex> vertVec(verts, verts + 24);
    std::vector<uint32_t> idxVec(inds, inds + 36);
    SubMesh sm; sm.indexCount = 36;
    std::vector<SubMesh> subs = { sm };

    auto mesh = std::make_unique<Mesh>();
    if (!mesh->load(vertVec, idxVec, subs)) return 0;

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes.push_back(std::move(mesh));
    setMeshAssetInfo(handle, "", "primitive:Box");
    return handle;
}

MeshHandle ResourceManager::createSphereMesh(float radius, int segments) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;

    // generate UV sphere
    for (int lat = 0; lat <= segments; lat++) {
        float theta = (float)lat / (float)segments * 3.14159265f;
        float sinT = std::sin(theta), cosT = std::cos(theta);
        for (int lon = 0; lon <= segments; lon++) {
            float phi = (float)lon / (float)segments * 2.0f * 3.14159265f;
            float sinP = std::sin(phi), cosP = std::cos(phi);

            float nx = sinT * cosP;
            float ny = cosT;
            float nz = sinT * sinP;

            Vertex v;
            v.px = nx * radius; v.py = ny * radius; v.pz = nz * radius;
            v.nx = nx; v.ny = ny; v.nz = nz;
            v.tu = (float)lon / (float)segments;
            v.tv = (float)lat / (float)segments;
            verts.push_back(v);
        }
    }

    for (int lat = 0; lat < segments; lat++) {
        for (int lon = 0; lon < segments; lon++) {
            int first = lat * (segments + 1) + lon;
            int second = first + segments + 1;

            inds.push_back(first); inds.push_back(second); inds.push_back(first + 1);
            inds.push_back(second); inds.push_back(second + 1); inds.push_back(first + 1);
        }
    }

    SubMesh sm; sm.indexCount = (uint32_t)inds.size();
    auto mesh = std::make_unique<Mesh>();
    if (!mesh->load(verts, inds, { sm })) return 0;

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes.push_back(std::move(mesh));
    setMeshAssetInfo(handle, "", "primitive:Sphere");
    return handle;
}

MeshHandle ResourceManager::createCapsuleMesh(float halfHeight, float radius, int segments) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;

    auto addVert = [&](float x, float y, float z, float nx, float ny, float nz) {
        Vertex v; v.px = x; v.py = y; v.pz = z;
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.tu = 0; v.tv = 0;
        verts.push_back(v);
    };

    auto ring = [&](float y, float r, int segs) {
        for (int i = 0; i <= segs; i++) {
            float a = (float)i / (float)segs * 2.0f * 3.14159265f;
            float ca = std::cos(a), sa = std::sin(a);
            addVert(ca * r, y, sa * r, ca * (r / radius), 0, sa * (r / radius));
        }
    };

    auto capRing = [&](float centerY, float r, float ny, int segs) {
        for (int i = 0; i <= segs; i++) {
            float a = (float)i / (float)segs * 2.0f * 3.14159265f;
            float ca = std::cos(a), sa = std::sin(a);
            addVert(ca * r, centerY, sa * r, ca * (r / radius), ny, sa * (r / radius));
        }
    };

    int segs2 = segments / 2;
    // hemisphere top cap, going from top pole to just above cylinder
    for (int lat = 0; lat <= segs2; lat++) {
        float theta = (float)lat / (float)segs2 * 3.14159265f * 0.5f;
        float y = halfHeight + radius * std::sin(theta);
        float r = radius * std::cos(theta);
        float ny = std::sin(theta);
        capRing(y, r, ny, segments);
    }

    // cylinder body (two rings at top and bottom of cylinder part)
    ring(halfHeight, radius, segments);
    ring(-halfHeight, radius, segments);

    // hemisphere bottom cap
    for (int lat = 0; lat <= segs2; lat++) {
        float theta = (float)lat / (float)segs2 * 3.14159265f * 0.5f;
        float y = -halfHeight - radius * std::sin(theta);
        float r = radius * std::cos(theta);
        float ny = -std::sin(theta);
        // bottom cap goes from bottom hemisphere to cylinder bottom
        capRing(y, r, -std::cos(theta), segments);
    }

    int stride = segments + 1;
    int topRings = segs2 + 1;
    int totalRings = topRings + 2 + segs2 + 1;

    // top hemisphere
    for (int lat = 0; lat < segs2; lat++) {
        for (int i = 0; i < segments; i++) {
            int a = lat * stride + i;
            int b = (lat + 1) * stride + i;
            inds.push_back(a); inds.push_back(b); inds.push_back(a + 1);
            inds.push_back(b); inds.push_back(b + 1); inds.push_back(a + 1);
        }
    }

    // cylinder body (connects top hemisphere bottom ring to cylinder top ring)
    int cylStart = segs2 * stride;
    for (int i = 0; i < segments; i++) {
        int a = cylStart + i;
        int b = cylStart + stride + i;
        inds.push_back(a); inds.push_back(b); inds.push_back(a + 1);
        inds.push_back(b); inds.push_back(b + 1); inds.push_back(a + 1);
    }

    // cylinder body (connect cylinder bottom ring to bottom hemisphere top ring)
    int cylBottom = cylStart + stride;
    int hemBottom = cylBottom + stride;
    for (int i = 0; i < segments; i++) {
        int a = cylBottom + i;
        int b = hemBottom + i;
        inds.push_back(a); inds.push_back(b); inds.push_back(a + 1);
        inds.push_back(b); inds.push_back(b + 1); inds.push_back(a + 1);
    }

    // bottom hemisphere
    int bottomStart = hemBottom;
    for (int lat = 0; lat < segs2; lat++) {
        for (int i = 0; i < segments; i++) {
            int a = bottomStart + lat * stride + i;
            int b = bottomStart + (lat + 1) * stride + i;
            inds.push_back(a); inds.push_back(b); inds.push_back(a + 1);
            inds.push_back(b); inds.push_back(b + 1); inds.push_back(a + 1);
        }
    }

    SubMesh sm; sm.indexCount = (uint32_t)inds.size();
    auto mesh = std::make_unique<Mesh>();
    if (!mesh->load(verts, inds, { sm })) return 0;

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes.push_back(std::move(mesh));
    setMeshAssetInfo(handle, "", "primitive:Capsule");
    return handle;
}

MeshHandle ResourceManager::createCylinderMesh(float halfHeight, float radius, int segments) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;

    auto addVert = [&](float x, float y, float z, float nx, float ny, float nz) {
        Vertex v; v.px = x; v.py = y; v.pz = z;
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.tu = 0; v.tv = 0; verts.push_back(v);
    };

    auto sideRing = [&](float y, int segs) {
        for (int i = 0; i <= segs; i++) {
            float a = (float)i / (float)segs * 2.0f * 3.14159265f;
            float ca = std::cos(a), sa = std::sin(a);
            addVert(ca * radius, y, sa * radius, ca, 0, sa);
        }
    };

    auto capRing = [&](float y, float ny, int segs) {
        for (int i = 0; i <= segs; i++) {
            float a = (float)i / (float)segs * 2.0f * 3.14159265f;
            float ca = std::cos(a), sa = std::sin(a);
            addVert(ca * radius, y, sa * radius, 0, ny, 0);
        }
    };

    sideRing(halfHeight, segments);
    sideRing(-halfHeight, segments);
    capRing(halfHeight, 1, segments);
    capRing(-halfHeight, -1, segments);

    int stride = segments + 1;

    // cylinder body sides
    for (int i = 0; i < segments; i++) {
        int a = i, b = stride + i;
        inds.push_back(a); inds.push_back(b); inds.push_back(a + 1);
        inds.push_back(b); inds.push_back(b + 1); inds.push_back(a + 1);
    }

    // top cap
    int topOff = stride * 2;
    for (int i = 0; i < segments; i++) {
        int a = topOff;
        int b = topOff + i + 1;
        int c = topOff + i;
        inds.push_back(a); inds.push_back(b); inds.push_back(c);
    }

    // bottom cap
    int botOff = stride * 3;
    for (int i = 0; i < segments; i++) {
        int a = botOff;
        int b = botOff + i;
        int c = botOff + i + 1;
        inds.push_back(a); inds.push_back(b); inds.push_back(c);
    }

    SubMesh sm; sm.indexCount = (uint32_t)inds.size();
    auto mesh = std::make_unique<Mesh>();
    if (!mesh->load(verts, inds, { sm })) return 0;

    MeshHandle handle = m_nextMeshHandle++;
    m_meshes.push_back(std::move(mesh));
    setMeshAssetInfo(handle, "", "primitive:Cylinder");
    return handle;
}

TextureHandle ResourceManager::loadTexture(const std::string& path) {
    const std::string texPath = fs::absolute(fs::path(path)).lexically_normal().string();

    auto it = m_textureCache.find(texPath);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    auto tex = std::make_unique<Texture>();
    if (!tex->loadFromFile(texPath)) {
        return 0;
    }

    TextureHandle handle = m_nextTextureHandle++;
    m_textures.push_back(std::move(tex));
    m_textureCache[texPath] = handle;

    return handle;
}

Mesh* ResourceManager::getMesh(MeshHandle handle) {
    if (handle == 0 || handle > m_meshes.size()) return nullptr;
    return m_meshes[handle - 1].get();
}

Texture* ResourceManager::getTexture(TextureHandle handle) {
    if (handle == 0 || handle > m_textures.size()) return nullptr;
    return m_textures[handle - 1].get();
}

const std::string& ResourceManager::meshSourcePath(MeshHandle handle) const {
    static const std::string empty;
    if (handle == 0 || handle > m_meshAssetInfo.size()) return empty;
    return m_meshAssetInfo[handle - 1].sourcePath;
}

const std::string& ResourceManager::meshAssetKind(MeshHandle handle) const {
    static const std::string empty;
    if (handle == 0 || handle > m_meshAssetInfo.size()) return empty;
    return m_meshAssetInfo[handle - 1].kind;
}

void ResourceManager::setMeshAssetInfo(MeshHandle handle, const std::string& sourcePath, const std::string& kind) {
    if (handle == 0) return;
    if (m_meshAssetInfo.size() < handle) {
        m_meshAssetInfo.resize(handle);
    }
    m_meshAssetInfo[handle - 1] = { sourcePath, kind };
}

void ResourceManager::destroyMesh(MeshHandle handle) {
    if (handle == 0 || handle > m_meshes.size()) return;
    m_meshes[handle - 1].reset();
}

void ResourceManager::destroyTexture(TextureHandle handle) {
    if (handle == 0 || handle > m_textures.size()) return;
    m_textures[handle - 1].reset();
}
