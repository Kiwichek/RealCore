#pragma once

#include <core/math.h>
#include <resources/ResourceManager.h>
#include <cstdint>
#include <string>
#include <vector>

using SceneEntity = uint32_t;

constexpr SceneEntity InvalidSceneEntity = 0;
constexpr uint32_t InvalidRigidBodyId = 0xffffffffu;

struct Transform {
    Vec3 position{ 0, 0, 0 };
    Vec3 rotation{ 0, 0, 0 };
    Vec3 scale{ 1, 1, 1 };

    Mat4 toMatrix() const;
};

struct TransformComponent {
    Transform local;
    Mat4 world;
};

struct MeshRendererComponent {
    MeshHandle meshHandle = 0;
    bool visible = true;
};

struct RigidBodyComponent {
    enum class Shape {
        Box,
        Sphere,
        Capsule,
        Cylinder
    };

    uint32_t bodyId = InvalidRigidBodyId;
    bool syncTransform = true;
    bool frozen = false;
    Shape shape = Shape::Box;
    Vec3 halfExtent{ 0.5f, 0.5f, 0.5f };
    float radius = 0.5f;
    float halfHeight = 0.5f;
    bool dynamic = true;
};

struct LightComponent {
    Vec3 direction{ 0.5f, 0.8f, 0.6f };
    Vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float ambient = 0.3f;
    bool enabled = true;
};

struct CameraComponent {
    float fovY = 60.0f * 3.14159f / 180.0f;
    float zNear = 0.01f;
    float zFar = 1000.0f;
    bool primary = true;
    bool enabled = true;
};

struct ScriptComponent {
    enum class Language {
        AngelScript,
        Lua
    };

    std::string moduleName;
    std::string scriptPath;
    Language language = Language::AngelScript;
    bool initialized = false;
};

struct SceneObject {
    SceneEntity id = InvalidSceneEntity;
    SceneEntity parent = InvalidSceneEntity;
    std::string name;
    TransformComponent transform;
    MeshRendererComponent meshRenderer;
    RigidBodyComponent rigidBody;
    LightComponent light;
    CameraComponent camera;
    ScriptComponent script;
    bool hasMeshRenderer = false;
    bool hasRigidBody = false;
    bool hasLight = false;
    bool hasCamera = false;
    bool hasScript = false;
    bool active = true;
};

class Scene {
public:
    SceneEntity createObject(const std::string& name = {});
    SceneEntity createMeshObject(MeshHandle meshHandle, const std::string& name = {});

    SceneObject* getObject(SceneEntity entity);
    const SceneObject* getObject(SceneEntity entity) const;

    TransformComponent* getTransform(SceneEntity entity);
    const TransformComponent* getTransform(SceneEntity entity) const;

    MeshRendererComponent* addMeshRenderer(SceneEntity entity, MeshHandle meshHandle);
    MeshRendererComponent* getMeshRenderer(SceneEntity entity);
    const MeshRendererComponent* getMeshRenderer(SceneEntity entity) const;
    void removeMeshRenderer(SceneEntity entity);

    RigidBodyComponent* addRigidBody(SceneEntity entity, uint32_t bodyId);
    RigidBodyComponent* getRigidBody(SceneEntity entity);
    const RigidBodyComponent* getRigidBody(SceneEntity entity) const;
    void removeRigidBody(SceneEntity entity);

    LightComponent* addLight(SceneEntity entity);
    LightComponent* getLight(SceneEntity entity);
    const LightComponent* getLight(SceneEntity entity) const;
    void removeLight(SceneEntity entity);

    CameraComponent* addCamera(SceneEntity entity);
    CameraComponent* getCamera(SceneEntity entity);
    const CameraComponent* getCamera(SceneEntity entity) const;
    void removeCamera(SceneEntity entity);

    ScriptComponent* addScript(SceneEntity entity, const std::string& moduleName);
    ScriptComponent* getScript(SceneEntity entity);
    const ScriptComponent* getScript(SceneEntity entity) const;
    void removeScript(SceneEntity entity);

    bool setParent(SceneEntity child, SceneEntity parent);
    void destroyObject(SceneEntity entity);
    void clear();

    SceneEntity findFirstByMesh(MeshHandle meshHandle) const;
    SceneEntity findPrimaryCamera() const;
    void updateWorldTransforms();

    std::vector<SceneObject>& objects() { return m_objects; }
    const std::vector<SceneObject>& objects() const { return m_objects; }

private:
    bool isValid(SceneEntity entity) const;
    bool isDescendant(SceneEntity entity, SceneEntity possibleDescendant) const;
    void updateObjectWorld(SceneEntity entity, std::vector<uint8_t>& visitState);

    std::vector<SceneObject> m_objects;
};
