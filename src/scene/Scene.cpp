#include <scene/Scene.h>

#include <algorithm>
#include <utility>

Mat4 Transform::toMatrix() const {
    return Mat4::translate(position) *
           Mat4::rotateZ(rotation.z) *
           Mat4::rotateY(rotation.y) *
           Mat4::rotateX(rotation.x) *
           Mat4::scale(scale);
}

SceneEntity Scene::createObject(const std::string& name) {
    SceneObject object;
    object.id = (SceneEntity)m_objects.size() + 1;
    object.name = name.empty() ? "Object " + std::to_string(object.id) : name;
    object.transform.world = object.transform.local.toMatrix();
    m_objects.push_back(std::move(object));
    return m_objects.back().id;
}

SceneEntity Scene::createMeshObject(MeshHandle meshHandle, const std::string& name) {
    SceneEntity entity = createObject(name.empty() ? "Mesh" : name);
    addMeshRenderer(entity, meshHandle);
    return entity;
}

SceneObject* Scene::getObject(SceneEntity entity) {
    if (!isValid(entity)) {
        return nullptr;
    }
    return &m_objects[(size_t)entity - 1];
}

const SceneObject* Scene::getObject(SceneEntity entity) const {
    if (!isValid(entity)) {
        return nullptr;
    }
    return &m_objects[(size_t)entity - 1];
}

TransformComponent* Scene::getTransform(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object ? &object->transform : nullptr;
}

const TransformComponent* Scene::getTransform(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object ? &object->transform : nullptr;
}

MeshRendererComponent* Scene::addMeshRenderer(SceneEntity entity, MeshHandle meshHandle) {
    SceneObject* object = getObject(entity);
    if (!object || meshHandle == 0) {
        return nullptr;
    }

    object->meshRenderer.meshHandle = meshHandle;
    object->meshRenderer.visible = true;
    object->hasMeshRenderer = true;
    return &object->meshRenderer;
}

MeshRendererComponent* Scene::getMeshRenderer(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object && object->hasMeshRenderer ? &object->meshRenderer : nullptr;
}

const MeshRendererComponent* Scene::getMeshRenderer(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object && object->hasMeshRenderer ? &object->meshRenderer : nullptr;
}

void Scene::removeMeshRenderer(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return;
    }

    object->meshRenderer = {};
    object->hasMeshRenderer = false;
}

RigidBodyComponent* Scene::addRigidBody(SceneEntity entity, uint32_t bodyId) {
    SceneObject* object = getObject(entity);
    if (!object || bodyId == InvalidRigidBodyId) {
        return nullptr;
    }

    object->rigidBody = {};
    object->rigidBody.bodyId = bodyId;
    object->rigidBody.syncTransform = true;
    object->hasRigidBody = true;
    return &object->rigidBody;
}

RigidBodyComponent* Scene::getRigidBody(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object && object->hasRigidBody ? &object->rigidBody : nullptr;
}

const RigidBodyComponent* Scene::getRigidBody(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object && object->hasRigidBody ? &object->rigidBody : nullptr;
}

void Scene::removeRigidBody(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return;
    }

    object->rigidBody = {};
    object->hasRigidBody = false;
}

LightComponent* Scene::addLight(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return nullptr;
    }

    object->light = {};
    object->hasLight = true;
    return &object->light;
}

LightComponent* Scene::getLight(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object && object->hasLight ? &object->light : nullptr;
}

const LightComponent* Scene::getLight(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object && object->hasLight ? &object->light : nullptr;
}

void Scene::removeLight(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return;
    }

    object->light = {};
    object->hasLight = false;
}

CameraComponent* Scene::addCamera(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return nullptr;
    }

    object->camera = {};
    object->hasCamera = true;
    return &object->camera;
}

CameraComponent* Scene::getCamera(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object && object->hasCamera ? &object->camera : nullptr;
}

const CameraComponent* Scene::getCamera(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object && object->hasCamera ? &object->camera : nullptr;
}

void Scene::removeCamera(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return;
    }

    object->camera = {};
    object->hasCamera = false;
}

ScriptComponent* Scene::addScript(SceneEntity entity, const std::string& moduleName) {
    SceneObject* object = getObject(entity);
    if (!object) return nullptr;

    object->script = {};
    object->script.moduleName = moduleName;
    object->hasScript = true;
    return &object->script;
}

ScriptComponent* Scene::getScript(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    return object && object->hasScript ? &object->script : nullptr;
}

const ScriptComponent* Scene::getScript(SceneEntity entity) const {
    const SceneObject* object = getObject(entity);
    return object && object->hasScript ? &object->script : nullptr;
}

void Scene::removeScript(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) return;

    object->script = {};
    object->hasScript = false;
}

bool Scene::setParent(SceneEntity child, SceneEntity parent) {
    SceneObject* childObject = getObject(child);
    if (!childObject || child == parent) {
        return false;
    }
    if (parent != InvalidSceneEntity && !getObject(parent)) {
        return false;
    }
    if (parent != InvalidSceneEntity && isDescendant(child, parent)) {
        return false;
    }

    childObject->parent = parent;
    return true;
}

void Scene::destroyObject(SceneEntity entity) {
    SceneObject* object = getObject(entity);
    if (!object) {
        return;
    }

    object->active = false;
    removeMeshRenderer(entity);
    removeRigidBody(entity);
    removeLight(entity);
    removeCamera(entity);
    removeScript(entity);

    for (auto& child : m_objects) {
        if (child.parent == entity) {
            child.parent = InvalidSceneEntity;
        }
    }
}

void Scene::clear() {
    m_objects.clear();
}

SceneEntity Scene::findFirstByMesh(MeshHandle meshHandle) const {
    if (meshHandle == 0) {
        return InvalidSceneEntity;
    }

    for (const auto& object : m_objects) {
        if (object.active && object.hasMeshRenderer && object.meshRenderer.meshHandle == meshHandle) {
            return object.id;
        }
    }
    return InvalidSceneEntity;
}

SceneEntity Scene::findPrimaryCamera() const {
    SceneEntity fallback = InvalidSceneEntity;

    for (const auto& object : m_objects) {
        if (!object.active || !object.hasCamera || !object.camera.enabled) {
            continue;
        }
        if (object.camera.primary) {
            return object.id;
        }
        if (fallback == InvalidSceneEntity) {
            fallback = object.id;
        }
    }

    return fallback;
}

void Scene::updateWorldTransforms() {
    std::vector<uint8_t> visitState(m_objects.size(), 0);
    for (const auto& object : m_objects) {
        if (object.active) {
            updateObjectWorld(object.id, visitState);
        }
    }
}

bool Scene::isValid(SceneEntity entity) const {
    return entity != InvalidSceneEntity && entity <= m_objects.size();
}

bool Scene::isDescendant(SceneEntity entity, SceneEntity possibleDescendant) const {
    const SceneObject* current = getObject(possibleDescendant);
    while (current && current->parent != InvalidSceneEntity) {
        if (current->parent == entity) {
            return true;
        }
        current = getObject(current->parent);
    }
    return false;
}

void Scene::updateObjectWorld(SceneEntity entity, std::vector<uint8_t>& visitState) {
    if (!isValid(entity)) {
        return;
    }

    const size_t index = (size_t)entity - 1;
    if (visitState[index] == 2) {
        return;
    }
    if (visitState[index] == 1) {
        m_objects[index].parent = InvalidSceneEntity;
        return;
    }

    visitState[index] = 1;

    SceneObject& object = m_objects[index];
    Mat4 local = object.transform.local.toMatrix();

    const SceneObject* parent = getObject(object.parent);
    if (parent && parent->active) {
        updateObjectWorld(parent->id, visitState);
        object.transform.world = parent->transform.world * local;
    } else {
        object.transform.world = local;
    }

    visitState[index] = 2;
}
