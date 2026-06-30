#include <script/ScriptEngine.h>
#include <angelscript.h>
#include <core/engine.h>
#include <core/math.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

struct ScriptVector2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct ScriptQuaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct ScriptColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct ScriptObjectRef {
    int id = 0;
};

struct ScriptNode {
    int refCount = 1;
    bool persistent = false;
    int id = 0;
};

struct ScriptTransformRef {
    int id = 0;
};

struct ScriptCameraRef {
    int id = 0;
};

struct ScriptLightRef {
    int id = 0;
};

struct ScriptRigidBodyRef {
    int id = 0;
};

struct ScriptSceneRef {
    int refCount = 1;
    bool persistent = false;
};

struct ScriptInputRef {
    int refCount = 1;
    bool persistent = false;
};

struct ScriptPhysicsRef {
    int dummy = 0;
};

struct ScriptAudioRef {
    int dummy = 0;
};

struct ScriptTimeRef {
    float deltaTime = 0.0f;
    float totalTime = 0.0f;
    int frame = 0;
};

static ScriptNode g_nodeStorage{ 1, true, 0 };
static ScriptNode* g_node = &g_nodeStorage;
static ScriptSceneRef g_sceneStorage{ 1, true };
static ScriptSceneRef* g_scene = &g_sceneStorage;
static ScriptInputRef g_inputStorage{ 1, true };
static ScriptInputRef* g_input = &g_inputStorage;
static ScriptPhysicsRef g_physics;
static ScriptAudioRef g_audio;
static ScriptTimeRef g_time;

static Vec3 makeVec3(float x, float y, float z) {
    return { x, y, z };
}

static ScriptVector2 makeVector2(float x, float y) {
    return { x, y };
}

static ScriptQuaternion makeQuaternion(float x, float y, float z, float w) {
    return { x, y, z, w };
}

static ScriptColor makeColor(float r, float g, float b, float a) {
    return { r, g, b, a };
}

static void vector2ConstructArgs(float x, float y, ScriptVector2* self) { new(self) ScriptVector2{ x, y }; }
static void vector3ConstructArgs(float x, float y, float z, Vec3* self) { new(self) Vec3{ x, y, z }; }
static void quaternionConstructArgs(float x, float y, float z, float w, ScriptQuaternion* self) { new(self) ScriptQuaternion{ x, y, z, w }; }
static void colorConstructArgs(float r, float g, float b, float a, ScriptColor* self) { new(self) ScriptColor{ r, g, b, a }; }

static Vec3 vec3Add(const Vec3& b, const Vec3* self) { return self ? *self + b : b; }
static Vec3 vec3Sub(const Vec3& b, const Vec3* self) { return self ? *self - b : Vec3{} - b; }
static Vec3 vec3Mul(float s, const Vec3& v) { return v * s; }
static Vec3 vec3MulR(float s, const Vec3* self) { return self ? *self * s : Vec3{}; }
static float vec3Length(const Vec3* self) { return self ? self->length() : 0.0f; }
static Vec3 vec3Normalized(const Vec3* self) { return self ? self->normalized() : Vec3{}; }

static SceneObject* scriptObject(int id) {
    return Engine::instance().scene().getObject((SceneEntity)id);
}

static const SceneObject* scriptObjectConst(int id) {
    return Engine::instance().scene().getObject((SceneEntity)id);
}

static SceneObject* scriptObjectByBodyId(int bodyId) {
    for (auto& object : Engine::instance().scene().objects()) {
        if (object.active && object.hasRigidBody && object.rigidBody.bodyId == (uint32_t)bodyId) {
            return &object;
        }
    }
    return nullptr;
}

static int currentNodeForModule(const std::string& moduleName) {
    for (const auto& object : Engine::instance().scene().objects()) {
        if (object.active && object.hasScript && object.script.moduleName == moduleName) {
            return (int)object.id;
        }
    }
    return 0;
}

static void updateScriptGlobals(const std::string& moduleName, float deltaTime) {
    g_nodeStorage.id = currentNodeForModule(moduleName);
    g_time.deltaTime = deltaTime;
    uint64_t frame = sapp_frame_count();
    static uint64_t lastFrame = 0;
    if (frame != lastFrame) {
        g_time.totalTime += deltaTime;
        g_time.frame = (int)frame;
        lastFrame = frame;
    }
}

static void script_NodeAddRef(ScriptNode* self) {
    if (self) {
        self->refCount++;
    }
}

static void script_NodeRelease(ScriptNode* self) {
    if (!self || self->persistent) {
        return;
    }
    self->refCount--;
    if (self->refCount <= 0) {
        delete self;
    }
}

static void script_SystemAddRef(void* self) {
    (void)self;
}

static void script_SystemRelease(void* self) {
    (void)self;
}

static ScriptNode* makeNodeHandle(int id) {
    if (id == 0 || !scriptObjectConst(id)) {
        return nullptr;
    }
    ScriptNode* node = new ScriptNode();
    node->id = id;
    return node;
}

static Vec3 script_NodeGetPosition(ScriptNode* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object ? object->transform.local.position : Vec3{};
}

static void script_NodeSetPosition(const Vec3& position, ScriptNode* self) {
    if (!self) {
        return;
    }
    Engine::instance().setEntityPosition((SceneEntity)self->id, position);
}

static Vec3 script_NodeGetVelocity(ScriptNode* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return {};
    }
    return Engine::instance().physics().getLinearVelocity(object->rigidBody.bodyId);
}

static void script_NodeSetVelocity(const Vec3& velocity, ScriptNode* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return;
    }
    Engine::instance().physics().setLinearVelocity(object->rigidBody.bodyId, velocity);
}

static bool script_NodeValid(ScriptNode* self) {
    return self && scriptObjectConst(self->id) != nullptr;
}

static ScriptNode* script_SceneFindNode(const std::string& name, ScriptSceneRef*) {
    for (auto& object : Engine::instance().scene().objects()) {
        if (object.active && object.name == name) {
            return makeNodeHandle((int)object.id);
        }
    }
    return nullptr;
}

static float script_InputMouseDeltaX(ScriptInputRef*) {
    return Engine::instance().mouseDeltaX();
}

static float script_InputMouseDeltaY(ScriptInputRef*) {
    return Engine::instance().mouseDeltaY();
}

static void script_InputSetMouseVisible(bool visible, ScriptInputRef*) {
    Engine::instance().setMouseVisible(visible);
}

static void script_MessageCallback(const asSMessageInfo* msg, void*) {
    const char* type = "info";
    if (msg->type == asMSGTYPE_WARNING) type = "warn";
    else if (msg->type == asMSGTYPE_ERROR) type = "ERROR";
    printf("[AS %s] %s (%d,%d): %s\n", type, msg->section, msg->row, msg->col, msg->message);
}

static void script_PrintInt(int value) {
    printf("%d\n", value);
}

static void script_PrintFloat(float value) {
    printf("%g\n", value);
}

static void script_PrintBool(bool value) {
    printf("%s\n", value ? "true" : "false");
}

static void script_PrintString(const std::string& value) {
    printf("%s\n", value.c_str());
}

static void script_LoadScene(const std::string& scenePath) {
    Engine::instance().requestSceneLoad(scenePath);
}

static void script_QuitGame() {
    Engine::instance().requestQuit();
}

// ---- Camera ----
static void script_UseSceneCamera(bool enabled) {
    Engine::instance().setUseSceneCamera(enabled);
}

// ---- Entity ----
static int script_FindEntity(const std::string& name) {
    for (auto& obj : Engine::instance().scene().objects()) {
        if (obj.active && obj.name == name) return (int)obj.id;
    }
    return 0;
}

static float script_GetEntityPosX(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.position.x : 0;
}

static float script_GetEntityPosY(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.position.y : 0;
}

static float script_GetEntityPosZ(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.position.z : 0;
}

static void script_SetEntityPosition(int id, float x, float y, float z) {
    Engine::instance().setEntityPosition((SceneEntity)id, { x, y, z });
}

static float script_GetEntityRotX(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.rotation.x : 0;
}

static float script_GetEntityRotY(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.rotation.y : 0;
}

static float script_GetEntityRotZ(int id) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    return obj ? obj->transform.local.rotation.z : 0;
}

static void script_SetEntityRotation(int id, float x, float y, float z) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)id);
    if (obj) obj->transform.local.rotation = { x, y, z };
}

// ---- Input ----
static float script_GetMouseX() {
    return Engine::instance().mouseX();
}

static float script_GetMouseY() {
    return Engine::instance().mouseY();
}

static int script_GetScreenWidth() {
    return sapp_width();
}

static int script_GetScreenHeight() {
    return sapp_height();
}

static bool script_IsMouseDown(int btn) {
    return Engine::instance().isMouseDown(btn);
}

static bool script_IsMousePressed(int btn) {
    return Engine::instance().isMousePressed(btn);
}

// ---- Physics ----
static int script_GetRigidBodyId(int entityId) {
    auto* obj = Engine::instance().scene().getObject((SceneEntity)entityId);
    return (obj && obj->hasRigidBody) ? (int)obj->rigidBody.bodyId : -1;
}

static void script_SetBodyPosition(int bodyId, float x, float y, float z) {
    Engine::instance().physics().setPosition((uint32_t)bodyId, { x, y, z });
}

static void script_SetBodyVelocity(int bodyId, float x, float y, float z) {
    Engine::instance().physics().setLinearVelocity((uint32_t)bodyId, { x, y, z });
}

static int script_AddKinematicBox(float x, float y, float z, float hx, float hy, float hz) {
    return (int)Engine::instance().physics().addKinematicBox({ x, y, z }, { hx, hy, hz });
}

// ---- Screen to world ----
static bool script_ScreenToPlane(float mouseX, float mouseY, float planeY, float& outX, float& outY, float& outZ) {
    Vec3 pos;
    bool hit = Engine::instance().screenToPlane(mouseX, mouseY, planeY, pos);
    if (hit) {
        outX = pos.x;
        outY = pos.y;
        outZ = pos.z;
    }
    return hit;
}

// ---- Object API ----
static bool script_ObjectValid(const ScriptObjectRef* self) {
    return self && self->id != 0 && scriptObjectConst(self->id) != nullptr;
}

static int script_ObjectId(const ScriptObjectRef* self) {
    return self ? self->id : 0;
}

static std::string script_ObjectName(const ScriptObjectRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object ? object->name : std::string();
}

static void script_SetObjectName(const std::string& name, ScriptObjectRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object) {
        object->name = name;
    }
}

static ScriptTransformRef script_ObjectTransform(const ScriptObjectRef* self) {
    return { self ? self->id : 0 };
}

static ScriptCameraRef script_ObjectCamera(const ScriptObjectRef* self) {
    return { self ? self->id : 0 };
}

static ScriptLightRef script_ObjectLight(const ScriptObjectRef* self) {
    return { self ? self->id : 0 };
}

static ScriptRigidBodyRef script_ObjectRigidBody(const ScriptObjectRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    return { object && object->hasRigidBody ? (int)object->rigidBody.bodyId : -1 };
}

static bool script_ObjectHasCamera(const ScriptObjectRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasCamera;
}

static bool script_ObjectHasLight(const ScriptObjectRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasLight;
}

static bool script_ObjectHasRigidBody(const ScriptObjectRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasRigidBody;
}

static Vec3 script_TransformPosition(const ScriptTransformRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object ? object->transform.local.position : Vec3{};
}

static void script_SetTransformPosition(const Vec3& position, ScriptTransformRef* self) {
    if (!self) return;
    Engine::instance().setEntityPosition((SceneEntity)self->id, position);
}

static Vec3 script_TransformRotation(const ScriptTransformRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object ? object->transform.local.rotation : Vec3{};
}

static void script_SetTransformRotation(const Vec3& rotation, ScriptTransformRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object) {
        object->transform.local.rotation = rotation;
    }
}

static Vec3 script_TransformScale(const ScriptTransformRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object ? object->transform.local.scale : Vec3{ 1.0f, 1.0f, 1.0f };
}

static void script_SetTransformScale(const Vec3& scale, ScriptTransformRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object) {
        object->transform.local.scale = scale;
    }
}

static bool script_CameraValid(const ScriptCameraRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasCamera;
}

static float script_CameraFov(const ScriptCameraRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasCamera ? object->camera.fovY : 0.0f;
}

static void script_SetCameraFov(float fov, ScriptCameraRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object && object->hasCamera) object->camera.fovY = fov;
}

static bool script_CameraEnabled(const ScriptCameraRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasCamera && object->camera.enabled;
}

static void script_SetCameraEnabled(bool enabled, ScriptCameraRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object && object->hasCamera) object->camera.enabled = enabled;
}

static bool script_LightValid(const ScriptLightRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasLight;
}

static Vec3 script_LightDirection(const ScriptLightRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasLight ? object->light.direction : Vec3{};
}

static void script_SetLightDirection(const Vec3& direction, ScriptLightRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object && object->hasLight) object->light.direction = direction;
}

static ScriptColor script_LightColor(const ScriptLightRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    if (!object || !object->hasLight) return {};
    return { object->light.color.x, object->light.color.y, object->light.color.z, 1.0f };
}

static void script_SetLightColor(const ScriptColor& color, ScriptLightRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object && object->hasLight) object->light.color = { color.r, color.g, color.b };
}

static float script_LightIntensity(const ScriptLightRef* self) {
    const SceneObject* object = self ? scriptObjectConst(self->id) : nullptr;
    return object && object->hasLight ? object->light.intensity : 0.0f;
}

static void script_SetLightIntensity(float intensity, ScriptLightRef* self) {
    SceneObject* object = self ? scriptObject(self->id) : nullptr;
    if (object && object->hasLight) object->light.intensity = intensity;
}

static bool script_RigidBodyValid(const ScriptRigidBodyRef* self) {
    return self && self->id >= 0;
}

static Vec3 script_RigidBodyPosition(const ScriptRigidBodyRef* self) {
    return self && self->id >= 0 ? Engine::instance().physics().getPosition((uint32_t)self->id) : Vec3{};
}

static void script_SetRigidBodyPosition(const Vec3& position, ScriptRigidBodyRef* self) {
    if (self && self->id >= 0) {
        Engine::instance().physics().setPosition((uint32_t)self->id, position);
    }
}

static Vec3 script_RigidBodyVelocity(const ScriptRigidBodyRef* self) {
    return self && self->id >= 0 ? Engine::instance().physics().getLinearVelocity((uint32_t)self->id) : Vec3{};
}

static void script_SetRigidBodyVelocity(const Vec3& velocity, ScriptRigidBodyRef* self) {
    if (self && self->id >= 0) {
        Engine::instance().physics().setLinearVelocity((uint32_t)self->id, velocity);
    }
}

static bool script_RigidBodyFrozen(const ScriptRigidBodyRef* self) {
    SceneObject* object = self && self->id >= 0 ? scriptObjectByBodyId(self->id) : nullptr;
    return object && object->rigidBody.frozen;
}

static void script_SetRigidBodyFrozen(bool frozen, ScriptRigidBodyRef* self) {
    if (!self || self->id < 0) return;
    SceneObject* object = scriptObjectByBodyId(self->id);
    if (object) {
        object->rigidBody.frozen = frozen;
    }
    Engine::instance().physics().setFrozen((uint32_t)self->id, frozen);
}

static ScriptObjectRef script_SceneFind(const std::string& name, ScriptSceneRef*) {
    return { script_FindEntity(name) };
}

static ScriptObjectRef script_SceneSelected(ScriptSceneRef*) {
    return { (int)Engine::instance().selectedEntity() };
}

static ScriptObjectRef script_SceneCreatePrimitive(const std::string& type, ScriptSceneRef*) {
    return { (int)Engine::instance().createPrimitive(type) };
}

static void script_SceneLoad(const std::string& scenePath, ScriptSceneRef*) {
    script_LoadScene(scenePath);
}

static float script_InputMouseX(ScriptInputRef*) { return script_GetMouseX(); }
static float script_InputMouseY(ScriptInputRef*) { return script_GetMouseY(); }
static int script_InputScreenWidth(ScriptInputRef*) { return script_GetScreenWidth(); }
static int script_InputScreenHeight(ScriptInputRef*) { return script_GetScreenHeight(); }
static bool script_InputMouseDown(int button, ScriptInputRef*) { return script_IsMouseDown(button); }
static bool script_InputMousePressed(int button, ScriptInputRef*) { return script_IsMousePressed(button); }

static Vec3 script_PhysicsGravity(ScriptPhysicsRef*) {
    return Engine::instance().physics().gravity();
}

static void script_SetPhysicsGravity(const Vec3& gravity, ScriptPhysicsRef*) {
    Engine::instance().physics().setGravity(gravity);
}

static int script_PhysicsAddKinematicBox(const Vec3& position, const Vec3& halfExtent, ScriptPhysicsRef*) {
    return (int)Engine::instance().physics().addKinematicBox(position, halfExtent);
}

static bool script_PhysicsScreenToPlane(const Vec3& mouse, float planeY, Vec3& out, ScriptPhysicsRef*) {
    return Engine::instance().screenToPlane(mouse.x, mouse.y, planeY, out);
}

static uint64_t script_AudioPlaySound(const std::string& path, float volume, ScriptAudioRef*) {
    return Engine::instance().audio().playSound(path, volume);
}

static uint64_t script_AudioPlaySoundDefault(const std::string& path, ScriptAudioRef*) {
    return Engine::instance().audio().playSound(path, 1.0f);
}

static void script_AudioStopSound(uint64_t id, ScriptAudioRef*) {
    Engine::instance().audio().stopSound(id);
}

static void script_AudioStopAll(ScriptAudioRef*) {
    Engine::instance().audio().stopAll();
}

static float script_AudioMasterVolume(ScriptAudioRef*) {
    return Engine::instance().audio().masterVolume();
}

static void script_SetAudioMasterVolume(float volume, ScriptAudioRef*) {
    Engine::instance().audio().setMasterVolume(volume);
}

static void stringDefaultConstruct(void* memory) {
    new(memory) std::string();
}

static void stringCopyConstruct(const std::string& other, void* memory) {
    new(memory) std::string(other);
}

static void stringDestruct(std::string* self) {
    self->~basic_string();
}

static std::string& stringAssign(const std::string& other, std::string* self) {
    *self = other;
    return *self;
}

class StdStringFactory final : public asIStringFactory {
public:
    const void* GetStringConstant(const char* data, asUINT length) override {
        std::string value(data, (size_t)length);
        auto it = m_strings.find(value);
        if (it != m_strings.end()) {
            it->second.refCount++;
            return &it->second.value;
        }

        Entry entry;
        entry.value = value;
        entry.refCount = 1;
        auto inserted = m_strings.emplace(std::move(value), std::move(entry));
        return &inserted.first->second.value;
    }

    int ReleaseStringConstant(const void* str) override {
        if (!str) {
            return asERROR;
        }

        const std::string* value = static_cast<const std::string*>(str);
        auto it = m_strings.find(*value);
        if (it == m_strings.end()) {
            return asERROR;
        }

        it->second.refCount--;
        if (it->second.refCount <= 0) {
            m_strings.erase(it);
        }
        return asSUCCESS;
    }

    int GetRawStringData(const void* str, char* data, asUINT* length) const override {
        if (!str) {
            return asERROR;
        }

        const std::string* value = static_cast<const std::string*>(str);
        if (length) {
            *length = (asUINT)value->size();
        }
        if (data && !value->empty()) {
            std::memcpy(data, value->data(), value->size());
        }
        return asSUCCESS;
    }

private:
    struct Entry {
        std::string value;
        int refCount = 0;
    };
    std::unordered_map<std::string, Entry> m_strings;
};

static StdStringFactory g_stringFactory;

static bool registerStringType(asIScriptEngine* engine) {
    int r = engine->RegisterObjectType("string", (int)sizeof(std::string),
        asOBJ_VALUE | asOBJ_APP_CLASS_CDAK);
    if (r < 0) return false;

    r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT, "void f()",
        asFUNCTION(stringDefaultConstruct), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;

    r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT, "void f(const string &in)",
        asFUNCTION(stringCopyConstruct), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;

    r = engine->RegisterObjectBehaviour("string", asBEHAVE_DESTRUCT, "void f()",
        asFUNCTION(stringDestruct), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;

    r = engine->RegisterObjectMethod("string", "string &opAssign(const string &in)",
        asFUNCTION(stringAssign), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;

    r = engine->RegisterStringFactory("string", &g_stringFactory);
    return r >= 0;
}

template <typename T>
static void defaultConstruct(void* memory) {
    new(memory) T();
}

template <typename T>
static void copyConstruct(const T& other, void* memory) {
    new(memory) T(other);
}

template <typename T>
static void destruct(T* self) {
    self->~T();
}

template <typename T>
static T& assignValue(const T& other, T* self) {
    *self = other;
    return *self;
}

template <typename T>
static bool registerDefaultValueBehaviours(asIScriptEngine* engine, const char* name) {
    int r = engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(defaultConstruct<T>), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    std::string copyDecl = "void f(const " + std::string(name) + " &in)";
    r = engine->RegisterObjectBehaviour(name, asBEHAVE_CONSTRUCT, copyDecl.c_str(), asFUNCTION(copyConstruct<T>), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectBehaviour(name, asBEHAVE_DESTRUCT, "void f()", asFUNCTION(destruct<T>), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    std::string assignDecl = std::string(name) + " &opAssign(const " + name + " &in)";
    r = engine->RegisterObjectMethod(name, assignDecl.c_str(), asFUNCTION(assignValue<T>), asCALL_CDECL_OBJLAST);
    return r >= 0;
}

static bool registerObjectRefMethods(asIScriptEngine* engine, const char* name) {
    int r = 0;
    std::string decl;

    r = engine->RegisterObjectProperty(name, "int id", asOFFSET(ScriptObjectRef, id));
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "bool valid() const", asFUNCTION(script_ObjectValid), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "int get_idValue() const", asFUNCTION(script_ObjectId), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "string get_name() const", asFUNCTION(script_ObjectName), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "void set_name(const string &in)", asFUNCTION(script_SetObjectName), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "Transform get_transform() const", asFUNCTION(script_ObjectTransform), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "Camera get_camera() const", asFUNCTION(script_ObjectCamera), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "Light get_light() const", asFUNCTION(script_ObjectLight), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "RigidBody get_rigidBody() const", asFUNCTION(script_ObjectRigidBody), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "bool hasCamera() const", asFUNCTION(script_ObjectHasCamera), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "bool hasLight() const", asFUNCTION(script_ObjectHasLight), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = engine->RegisterObjectMethod(name, "bool hasRigidBody() const", asFUNCTION(script_ObjectHasRigidBody), asCALL_CDECL_OBJLAST);
    return r >= 0;
}

} // namespace

bool ScriptEngine::init() {
    m_engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (!m_engine) return false;

    m_engine->SetMessageCallback(asFUNCTION(script_MessageCallback), nullptr, asCALL_CDECL);

    if (!registerStringType(m_engine)) {
        return false;
    }

    if (!registerValueTypes() || !registerObjectTypes() || !registerSystemTypes()) {
        return false;
    }

    int r;
    r = m_engine->RegisterGlobalFunction("void print(int)", asFUNCTION(script_PrintInt), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("void print(float)", asFUNCTION(script_PrintFloat), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("void print(bool)", asFUNCTION(script_PrintBool), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(script_PrintString), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("void loadScene(const string &in)", asFUNCTION(script_LoadScene), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("void quitGame()", asFUNCTION(script_QuitGame), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("void useSceneCamera(bool)", asFUNCTION(script_UseSceneCamera), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("int findEntity(const string &in)", asFUNCTION(script_FindEntity), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("float getEntityPosX(int)", asFUNCTION(script_GetEntityPosX), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("float getEntityPosY(int)", asFUNCTION(script_GetEntityPosY), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("float getEntityPosZ(int)", asFUNCTION(script_GetEntityPosZ), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("void setEntityPosition(int, float, float, float)", asFUNCTION(script_SetEntityPosition), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("float getEntityRotX(int)", asFUNCTION(script_GetEntityRotX), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("float getEntityRotY(int)", asFUNCTION(script_GetEntityRotY), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("float getEntityRotZ(int)", asFUNCTION(script_GetEntityRotZ), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("void setEntityRotation(int, float, float, float)", asFUNCTION(script_SetEntityRotation), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("float getMouseX()", asFUNCTION(script_GetMouseX), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("float getMouseY()", asFUNCTION(script_GetMouseY), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("int getScreenWidth()", asFUNCTION(script_GetScreenWidth), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("int getScreenHeight()", asFUNCTION(script_GetScreenHeight), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("bool isMouseDown(int)", asFUNCTION(script_IsMouseDown), asCALL_CDECL);
    if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("bool isMousePressed(int)", asFUNCTION(script_IsMousePressed), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("int getRigidBodyId(int)", asFUNCTION(script_GetRigidBodyId), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("void setBodyPosition(int, float, float, float)", asFUNCTION(script_SetBodyPosition), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("void setBodyVelocity(int, float, float, float)", asFUNCTION(script_SetBodyVelocity), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("int addKinematicBox(float, float, float, float, float, float)", asFUNCTION(script_AddKinematicBox), asCALL_CDECL);
    if (r < 0) return false;

    r = m_engine->RegisterGlobalFunction("bool screenToPlane(float, float, float, float &out, float &out, float &out)", asFUNCTION(script_ScreenToPlane), asCALL_CDECL);
    if (r < 0) return false;

    return true;
}

bool ScriptEngine::registerValueTypes() {
    int r = 0;

    const int valueFlags = asOBJ_VALUE | asOBJ_APP_CLASS_CDAK;

    r = m_engine->RegisterObjectType("Vector2", (int)sizeof(ScriptVector2), valueFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptVector2>(m_engine, "Vector2")) return false;
    r = m_engine->RegisterObjectBehaviour("Vector2", asBEHAVE_CONSTRUCT, "void f(float, float)", asFUNCTION(vector2ConstructArgs), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Vector2", "float x", asOFFSET(ScriptVector2, x)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Vector2", "float y", asOFFSET(ScriptVector2, y)); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Vector3", (int)sizeof(Vec3), valueFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<Vec3>(m_engine, "Vector3")) return false;
    r = m_engine->RegisterObjectBehaviour("Vector3", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTION(vector3ConstructArgs), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Vector3", "float x", asOFFSET(Vec3, x)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Vector3", "float y", asOFFSET(Vec3, y)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Vector3", "float z", asOFFSET(Vec3, z)); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Vector3", "Vector3 opAdd(const Vector3 &in) const", asFUNCTION(vec3Add), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Vector3", "Vector3 opSub(const Vector3 &in) const", asFUNCTION(vec3Sub), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Vector3", "Vector3 opMul(float) const", asFUNCTION(vec3MulR), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Vector3", "float length() const", asFUNCTION(vec3Length), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Vector3", "Vector3 normalized() const", asFUNCTION(vec3Normalized), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterGlobalFunction("Vector3 opMul(float, const Vector3 &in)", asFUNCTION(vec3Mul), asCALL_CDECL); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Quaternion", (int)sizeof(ScriptQuaternion), valueFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptQuaternion>(m_engine, "Quaternion")) return false;
    r = m_engine->RegisterObjectBehaviour("Quaternion", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(quaternionConstructArgs), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Quaternion", "float x", asOFFSET(ScriptQuaternion, x)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Quaternion", "float y", asOFFSET(ScriptQuaternion, y)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Quaternion", "float z", asOFFSET(ScriptQuaternion, z)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Quaternion", "float w", asOFFSET(ScriptQuaternion, w)); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Color", (int)sizeof(ScriptColor), valueFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptColor>(m_engine, "Color")) return false;
    r = m_engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(colorConstructArgs), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Color", "float r", asOFFSET(ScriptColor, r)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Color", "float g", asOFFSET(ScriptColor, g)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Color", "float b", asOFFSET(ScriptColor, b)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Color", "float a", asOFFSET(ScriptColor, a)); if (r < 0) return false;

    return true;
}

bool ScriptEngine::registerObjectTypes() {
    int r = 0;
    const int refFlags = asOBJ_VALUE | asOBJ_APP_CLASS_CDAK;

    r = m_engine->RegisterObjectType("Node", 0, asOBJ_REF);
    if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Node", asBEHAVE_ADDREF, "void f()", asFUNCTION(script_NodeAddRef), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Node", asBEHAVE_RELEASE, "void f()", asFUNCTION(script_NodeRelease), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Node", "Vector3 getPosition()", asFUNCTION(script_NodeGetPosition), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Node", "void setPosition(const Vector3 &in)", asFUNCTION(script_NodeSetPosition), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Node", "Vector3 get_velocity() const", asFUNCTION(script_NodeGetVelocity), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Node", "void set_velocity(const Vector3 &in)", asFUNCTION(script_NodeSetVelocity), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Node", "bool valid() const", asFUNCTION(script_NodeValid), asCALL_CDECL_OBJLAST);
    if (r < 0) return false;

    for (const char* name : { "Entity", "GameObject" }) {
        r = m_engine->RegisterObjectType(name, (int)sizeof(ScriptObjectRef), refFlags);
        if (r < 0) return false;
        if (!registerDefaultValueBehaviours<ScriptObjectRef>(m_engine, name)) return false;
    }

    r = m_engine->RegisterObjectType("Transform", (int)sizeof(ScriptTransformRef), refFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptTransformRef>(m_engine, "Transform")) return false;
    r = m_engine->RegisterObjectProperty("Transform", "int entityId", asOFFSET(ScriptTransformRef, id)); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "Vector3 get_position() const", asFUNCTION(script_TransformPosition), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "void set_position(const Vector3 &in)", asFUNCTION(script_SetTransformPosition), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "Vector3 get_rotation() const", asFUNCTION(script_TransformRotation), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "void set_rotation(const Vector3 &in)", asFUNCTION(script_SetTransformRotation), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "Vector3 get_scale() const", asFUNCTION(script_TransformScale), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Transform", "void set_scale(const Vector3 &in)", asFUNCTION(script_SetTransformScale), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Camera", (int)sizeof(ScriptCameraRef), refFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptCameraRef>(m_engine, "Camera")) return false;
    r = m_engine->RegisterObjectMethod("Camera", "bool valid() const", asFUNCTION(script_CameraValid), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Camera", "float get_fov() const", asFUNCTION(script_CameraFov), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Camera", "void set_fov(float)", asFUNCTION(script_SetCameraFov), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Camera", "bool get_enabled() const", asFUNCTION(script_CameraEnabled), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Camera", "void set_enabled(bool)", asFUNCTION(script_SetCameraEnabled), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Light", (int)sizeof(ScriptLightRef), refFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptLightRef>(m_engine, "Light")) return false;
    r = m_engine->RegisterObjectMethod("Light", "bool valid() const", asFUNCTION(script_LightValid), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "Vector3 get_direction() const", asFUNCTION(script_LightDirection), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "void set_direction(const Vector3 &in)", asFUNCTION(script_SetLightDirection), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "Color get_color() const", asFUNCTION(script_LightColor), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "void set_color(const Color &in)", asFUNCTION(script_SetLightColor), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "float get_intensity() const", asFUNCTION(script_LightIntensity), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Light", "void set_intensity(float)", asFUNCTION(script_SetLightIntensity), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("RigidBody", (int)sizeof(ScriptRigidBodyRef), refFlags);
    if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptRigidBodyRef>(m_engine, "RigidBody")) return false;
    r = m_engine->RegisterObjectProperty("RigidBody", "int bodyId", asOFFSET(ScriptRigidBodyRef, id)); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "bool valid() const", asFUNCTION(script_RigidBodyValid), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "Vector3 get_position() const", asFUNCTION(script_RigidBodyPosition), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "void set_position(const Vector3 &in)", asFUNCTION(script_SetRigidBodyPosition), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "Vector3 get_velocity() const", asFUNCTION(script_RigidBodyVelocity), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "void set_velocity(const Vector3 &in)", asFUNCTION(script_SetRigidBodyVelocity), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "bool get_frozen() const", asFUNCTION(script_RigidBodyFrozen), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("RigidBody", "void set_frozen(bool)", asFUNCTION(script_SetRigidBodyFrozen), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    for (const char* name : { "Entity", "GameObject" }) {
        if (!registerObjectRefMethods(m_engine, name)) return false;
    }

    return true;
}

bool ScriptEngine::registerSystemTypes() {
    int r = 0;
    const int systemFlags = asOBJ_VALUE | asOBJ_APP_CLASS_CDAK;

    r = m_engine->RegisterObjectType("Scene", 0, asOBJ_REF); if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Scene", asBEHAVE_ADDREF, "void f()", asFUNCTION(script_SystemAddRef), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Scene", asBEHAVE_RELEASE, "void f()", asFUNCTION(script_SystemRelease), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Scene", "Node@ findNode(const string &in)", asFUNCTION(script_SceneFindNode), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Scene", "GameObject find(const string &in)", asFUNCTION(script_SceneFind), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Scene", "GameObject selected() const", asFUNCTION(script_SceneSelected), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Scene", "GameObject createPrimitive(const string &in)", asFUNCTION(script_SceneCreatePrimitive), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Scene", "void load(const string &in)", asFUNCTION(script_SceneLoad), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Input", 0, asOBJ_REF); if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Input", asBEHAVE_ADDREF, "void f()", asFUNCTION(script_SystemAddRef), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectBehaviour("Input", asBEHAVE_RELEASE, "void f()", asFUNCTION(script_SystemRelease), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "float get_mouseX() const", asFUNCTION(script_InputMouseX), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "float get_mouseY() const", asFUNCTION(script_InputMouseY), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "float getMouseDeltaX()", asFUNCTION(script_InputMouseDeltaX), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "float getMouseDeltaY()", asFUNCTION(script_InputMouseDeltaY), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "void setMouseVisible(bool)", asFUNCTION(script_InputSetMouseVisible), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "int get_screenWidth() const", asFUNCTION(script_InputScreenWidth), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "int get_screenHeight() const", asFUNCTION(script_InputScreenHeight), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "bool isMouseDown(int) const", asFUNCTION(script_InputMouseDown), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Input", "bool isMousePressed(int) const", asFUNCTION(script_InputMousePressed), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Physics", (int)sizeof(ScriptPhysicsRef), systemFlags); if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptPhysicsRef>(m_engine, "Physics")) return false;
    r = m_engine->RegisterObjectMethod("Physics", "Vector3 get_gravity() const", asFUNCTION(script_PhysicsGravity), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Physics", "void set_gravity(const Vector3 &in)", asFUNCTION(script_SetPhysicsGravity), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Physics", "int addKinematicBox(const Vector3 &in, const Vector3 &in)", asFUNCTION(script_PhysicsAddKinematicBox), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Physics", "bool screenToPlane(const Vector3 &in, float, Vector3 &out)", asFUNCTION(script_PhysicsScreenToPlane), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Audio", (int)sizeof(ScriptAudioRef), systemFlags); if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptAudioRef>(m_engine, "Audio")) return false;
    r = m_engine->RegisterObjectMethod("Audio", "uint64 playSound(const string &in)", asFUNCTION(script_AudioPlaySoundDefault), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Audio", "uint64 playSound(const string &in, float)", asFUNCTION(script_AudioPlaySound), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Audio", "void stopSound(uint64)", asFUNCTION(script_AudioStopSound), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Audio", "void stopAll()", asFUNCTION(script_AudioStopAll), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Audio", "float get_masterVolume() const", asFUNCTION(script_AudioMasterVolume), asCALL_CDECL_OBJLAST); if (r < 0) return false;
    r = m_engine->RegisterObjectMethod("Audio", "void set_masterVolume(float)", asFUNCTION(script_SetAudioMasterVolume), asCALL_CDECL_OBJLAST); if (r < 0) return false;

    r = m_engine->RegisterObjectType("Time", (int)sizeof(ScriptTimeRef), systemFlags); if (r < 0) return false;
    if (!registerDefaultValueBehaviours<ScriptTimeRef>(m_engine, "Time")) return false;
    r = m_engine->RegisterObjectProperty("Time", "float deltaTime", asOFFSET(ScriptTimeRef, deltaTime)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Time", "float totalTime", asOFFSET(ScriptTimeRef, totalTime)); if (r < 0) return false;
    r = m_engine->RegisterObjectProperty("Time", "int frame", asOFFSET(ScriptTimeRef, frame)); if (r < 0) return false;

    r = m_engine->RegisterGlobalProperty("Node@ node", &g_node); if (r < 0) return false;
    r = m_engine->RegisterGlobalProperty("Scene@ scene", &g_scene); if (r < 0) return false;
    r = m_engine->RegisterGlobalProperty("Input@ input", &g_input); if (r < 0) return false;
    r = m_engine->RegisterGlobalProperty("Physics physics", &g_physics); if (r < 0) return false;
    r = m_engine->RegisterGlobalProperty("Audio audio", &g_audio); if (r < 0) return false;
    r = m_engine->RegisterGlobalProperty("Time time", &g_time); if (r < 0) return false;

    return true;
}

void ScriptEngine::shutdown() {
    if (m_engine) {
        m_modules.clear();
        m_engine->ShutDownAndRelease();
        m_engine = nullptr;
    }
}

bool ScriptEngine::loadScript(const std::string& scriptPath) {
    if (!m_engine) return false;

    FILE* f = nullptr;
    fopen_s(&f, scriptPath.c_str(), "rb");
    if (!f) {
        printf("Failed to open script: %s\n", scriptPath.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string source(static_cast<size_t>(len), '\0');
    fread(source.data(), 1, static_cast<size_t>(len), f);
    fclose(f);

    return loadScriptFromSource(moduleNameForPath(scriptPath), source);
}

bool ScriptEngine::loadScriptFromSource(const std::string& moduleName, const std::string& source) {
    if (!m_engine) return false;

    unloadScript(moduleName);

    asIScriptModule* mod = m_engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE);
    if (!mod) return false;

    int r = mod->AddScriptSection(moduleName.c_str(), source.c_str(), source.size());
    if (r < 0) { m_engine->DiscardModule(moduleName.c_str()); return false; }

    r = mod->Build();
    if (r < 0) { m_engine->DiscardModule(moduleName.c_str()); return false; }

    m_modules[moduleName] = mod;
    return true;
}

void ScriptEngine::unloadScript(const std::string& moduleName) {
    auto it = m_modules.find(moduleName);
    if (it != m_modules.end()) {
        m_engine->DiscardModule(moduleName.c_str());
        m_modules.erase(it);
    }
}

std::string ScriptEngine::moduleNameForPath(const std::string& scriptPath) const {
    std::string moduleName = fs::path(scriptPath).stem().string();
    if (moduleName.empty()) {
        moduleName = "Script";
    }

    for (char& c : moduleName) {
        if (!std::isalnum((unsigned char)c) && c != '_') {
            c = '_';
        }
    }
    return moduleName;
}

void ScriptEngine::callFunction(const std::string& moduleName, const std::string& funcName) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end()) return;

    asIScriptContext* ctx = m_engine->RequestContext();
    if (!ctx) return;

    std::string decl = "void " + funcName + "()";
    asIScriptFunction* func = it->second->GetFunctionByDecl(decl.c_str());
    if (!func) { m_engine->ReturnContext(ctx); return; }

    updateScriptGlobals(moduleName, 0.0f);
    int r = ctx->Prepare(func);
    if (r >= 0) ctx->Execute();

    m_engine->ReturnContext(ctx);
}

void ScriptEngine::callFunctionFloat(const std::string& moduleName, const std::string& funcName, float value) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end()) return;

    asIScriptContext* ctx = m_engine->RequestContext();
    if (!ctx) return;

    std::string decl = "void " + funcName + "(float)";
    asIScriptFunction* func = it->second->GetFunctionByDecl(decl.c_str());
    if (!func) { m_engine->ReturnContext(ctx); return; }

    updateScriptGlobals(moduleName, value);
    int r = ctx->Prepare(func);
    if (r >= 0) {
        ctx->SetArgFloat(0, value);
        ctx->Execute();
    }

    m_engine->ReturnContext(ctx);
}

void ScriptEngine::callFunctionOnObject(const std::string& moduleName, const std::string& objType, void* obj, const std::string& funcName) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end()) return;

    asIScriptContext* ctx = m_engine->RequestContext();
    if (!ctx) return;

    std::string decl = "void " + funcName + "()";
    asIScriptFunction* func = it->second->GetFunctionByDecl(decl.c_str());
    if (!func) { m_engine->ReturnContext(ctx); return; }

    updateScriptGlobals(moduleName, 0.0f);
    int r = ctx->Prepare(func);
    if (r >= 0) {
        ctx->SetObject(obj);
        ctx->Execute();
    }

    m_engine->ReturnContext(ctx);
}
