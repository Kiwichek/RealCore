#include <script/LuaScriptEngine.h>

#include <core/engine.h>
#include <core/math.h>
#include <scene/Scene.h>

#include <lua.hpp>
#include <sokol_app.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr const char* kNodeMeta = "RealCore.Node";
constexpr const char* kCurrentNodeId = "realcore.currentNodeId";

static int currentNodeId(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kCurrentNodeId);
    int id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return id;
}

static void setCurrentNodeId(lua_State* L, int id) {
    lua_pushinteger(L, id);
    lua_setfield(L, LUA_REGISTRYINDEX, kCurrentNodeId);

    lua_getglobal(L, "node");
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, id);
        lua_setfield(L, -2, "__node_id");
    }
    lua_pop(L, 1);
}

static int luaCurrentNodeForState(lua_State* L) {
    return currentNodeId(L);
}

static int currentNodeForModule(const std::string& moduleName) {
    for (const auto& object : Engine::instance().scene().objects()) {
        if (object.active && object.hasScript && object.script.moduleName == moduleName) {
            return (int)object.id;
        }
    }
    return 0;
}

static void pushVector3(lua_State* L, const Vec3& value) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, value.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, value.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, value.z);
    lua_setfield(L, -2, "z");
}

static void pushBoundsTable(lua_State* L, bool valid, const Vec3& min, const Vec3& max, const Vec3& center, float radius) {
    Vec3 size = max - min;
    lua_createtable(L, 0, 6);

    lua_pushboolean(L, valid);
    lua_setfield(L, -2, "valid");
    pushVector3(L, min);
    lua_setfield(L, -2, "min");
    pushVector3(L, max);
    lua_setfield(L, -2, "max");
    pushVector3(L, size);
    lua_setfield(L, -2, "size");
    pushVector3(L, center);
    lua_setfield(L, -2, "center");
    lua_pushnumber(L, radius);
    lua_setfield(L, -2, "radius");
}

static Vec3 componentMin(const Vec3& a, const Vec3& b) {
    return { (std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z) };
}

static Vec3 componentMax(const Vec3& a, const Vec3& b) {
    return { (std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z) };
}

static Vec3 readVector3(lua_State* L, int index) {
    Vec3 out{};
    if (!lua_istable(L, index)) {
        return out;
    }
    if (index < 0) {
        index = lua_gettop(L) + index + 1;
    }
    lua_getfield(L, index, "x");
    out.x = (float)luaL_optnumber(L, -1, 0.0);
    lua_pop(L, 1);
    lua_getfield(L, index, "y");
    out.y = (float)luaL_optnumber(L, -1, 0.0);
    lua_pop(L, 1);
    lua_getfield(L, index, "z");
    out.z = (float)luaL_optnumber(L, -1, 0.0);
    lua_pop(L, 1);
    return out;
}

static SceneObject* objectById(int id) {
    return Engine::instance().scene().getObject((SceneEntity)id);
}

static int nodeIdFromSelf(lua_State* L, int index) {
    if (!lua_istable(L, index)) {
        return 0;
    }
    lua_getfield(L, index, "__node_id");
    int id = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return id;
}

static void pushNode(lua_State* L, int id) {
    if (id == 0 || !objectById(id)) {
        lua_pushnil(L);
        return;
    }
    lua_createtable(L, 0, 1);
    lua_pushinteger(L, id);
    lua_setfield(L, -2, "__node_id");
    luaL_getmetatable(L, kNodeMeta);
    lua_setmetatable(L, -2);
}

static int lua_Vector3(lua_State* L) {
    Vec3 value{
        (float)luaL_optnumber(L, 1, 0.0),
        (float)luaL_optnumber(L, 2, 0.0),
        (float)luaL_optnumber(L, 3, 0.0)
    };
    pushVector3(L, value);
    return 1;
}

static int lua_print(lua_State* L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) std::printf("\t");
        const char* text = luaL_tolstring(L, i, nullptr);
        std::printf("%s", text ? text : "");
        lua_pop(L, 1);
    }
    std::printf("\n");
    return 0;
}

static int lua_loadScene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    Engine::instance().requestSceneLoad(path ? path : "");
    return 0;
}

static int lua_quitGame(lua_State*) {
    Engine::instance().requestQuit();
    return 0;
}

static Vec3 nodePosition(int id) {
    SceneObject* object = objectById(id);
    return object ? object->transform.local.position : Vec3{};
}

static void pushNodeLocalBounds(lua_State* L, int id) {
    SceneObject* object = objectById(id);
    if (!object || !object->hasMeshRenderer) {
        Vec3 position = object ? object->transform.local.position : Vec3{};
        pushBoundsTable(L, false, position, position, position, 0.0f);
        return;
    }

    Mesh* mesh = Engine::instance().resources().getMesh(object->meshRenderer.meshHandle);
    if (!mesh || !mesh->bounds().valid) {
        Vec3 position = object->transform.local.position;
        pushBoundsTable(L, false, position, position, position, 0.0f);
        return;
    }

    const MeshBounds& bounds = mesh->bounds();
    pushBoundsTable(L, true, bounds.min, bounds.max, bounds.center, bounds.radius);
}

static void pushNodeWorldBounds(lua_State* L, int id) {
    auto& engine = Engine::instance();
    SceneObject* object = objectById(id);
    if (!object) {
        pushBoundsTable(L, false, {}, {}, {}, 0.0f);
        return;
    }

    engine.scene().updateWorldTransforms();

    Mesh* mesh = object->hasMeshRenderer ? engine.resources().getMesh(object->meshRenderer.meshHandle) : nullptr;
    if (!mesh || !mesh->bounds().valid) {
        Vec3 position = object->transform.world.transformPoint({});
        pushBoundsTable(L, false, position, position, position, 0.0f);
        return;
    }

    const MeshBounds& bounds = mesh->bounds();
    Vec3 corners[8] = {
        { bounds.min.x, bounds.min.y, bounds.min.z },
        { bounds.max.x, bounds.min.y, bounds.min.z },
        { bounds.min.x, bounds.max.y, bounds.min.z },
        { bounds.max.x, bounds.max.y, bounds.min.z },
        { bounds.min.x, bounds.min.y, bounds.max.z },
        { bounds.max.x, bounds.min.y, bounds.max.z },
        { bounds.min.x, bounds.max.y, bounds.max.z },
        { bounds.max.x, bounds.max.y, bounds.max.z }
    };

    Vec3 worldMin = object->transform.world.transformPoint(corners[0]);
    Vec3 worldMax = worldMin;
    for (int i = 1; i < 8; i++) {
        Vec3 p = object->transform.world.transformPoint(corners[i]);
        worldMin = componentMin(worldMin, p);
        worldMax = componentMax(worldMax, p);
    }

    Vec3 center = (worldMin + worldMax) * 0.5f;
    float radius = (worldMax - center).length();
    pushBoundsTable(L, true, worldMin, worldMax, center, radius);
}

static void setNodePosition(int id, const Vec3& position) {
    if (objectById(id)) {
        Engine::instance().setEntityPosition((SceneEntity)id, position);
    }
}

static Vec3 nodeVelocity(int id) {
    SceneObject* object = objectById(id);
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return {};
    }
    return Engine::instance().physics().getLinearVelocity(object->rigidBody.bodyId);
}

static void setNodeVelocity(int id, const Vec3& velocity) {
    SceneObject* object = objectById(id);
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return;
    }
    Engine::instance().physics().setLinearVelocity(object->rigidBody.bodyId, velocity);
}

static void applyNodeForce(int id, const Vec3& force) {
    SceneObject* object = objectById(id);
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return;
    }
    Engine::instance().physics().applyForce(object->rigidBody.bodyId, force);
}

static void applyNodeImpulse(int id, const Vec3& impulse) {
    SceneObject* object = objectById(id);
    if (!object || !object->hasRigidBody || object->rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
        return;
    }
    Engine::instance().physics().applyImpulse(object->rigidBody.bodyId, impulse);
}

static int lua_Node_getPosition(lua_State* L) {
    int id = lua_istable(L, 1) ? nodeIdFromSelf(L, 1) : luaCurrentNodeForState(L);
    pushVector3(L, nodePosition(id));
    return 1;
}

static int lua_Node_setPosition(lua_State* L) {
    bool hasSelf = lua_istable(L, 1) && lua_gettop(L) >= 2;
    int id = hasSelf ? nodeIdFromSelf(L, 1) : luaCurrentNodeForState(L);
    setNodePosition(id, readVector3(L, hasSelf ? 2 : 1));
    return 0;
}

static int lua_Node_applyForce(lua_State* L) {
    bool hasSelf = lua_istable(L, 1) && lua_gettop(L) >= 2;
    int id = hasSelf ? nodeIdFromSelf(L, 1) : luaCurrentNodeForState(L);
    applyNodeForce(id, readVector3(L, hasSelf ? 2 : 1));
    return 0;
}

static int lua_Node_applyImpulse(lua_State* L) {
    bool hasSelf = lua_istable(L, 1) && lua_gettop(L) >= 2;
    int id = hasSelf ? nodeIdFromSelf(L, 1) : luaCurrentNodeForState(L);
    applyNodeImpulse(id, readVector3(L, hasSelf ? 2 : 1));
    return 0;
}

static int lua_Node_valid(lua_State* L) {
    int id = lua_istable(L, 1) ? nodeIdFromSelf(L, 1) : luaCurrentNodeForState(L);
    lua_pushboolean(L, objectById(id) != nullptr);
    return 1;
}

static int lua_Node_index(lua_State* L) {
    int id = nodeIdFromSelf(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "velocity") == 0) {
        pushVector3(L, nodeVelocity(id));
        return 1;
    }
    if (std::strcmp(key, "position") == 0) {
        pushVector3(L, nodePosition(id));
        return 1;
    }
    if (std::strcmp(key, "name") == 0) {
        SceneObject* object = objectById(id);
        lua_pushstring(L, object ? object->name.c_str() : "");
        return 1;
    }
    if (std::strcmp(key, "worldBounds") == 0) {
        pushNodeWorldBounds(L, id);
        return 1;
    }
    if (std::strcmp(key, "localBounds") == 0 || std::strcmp(key, "bounds") == 0) {
        pushNodeLocalBounds(L, id);
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_Node_newindex(lua_State* L) {
    int id = nodeIdFromSelf(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "velocity") == 0) {
        setNodeVelocity(id, readVector3(L, 3));
    } else if (std::strcmp(key, "position") == 0) {
        setNodePosition(id, readVector3(L, 3));
    } else if (std::strcmp(key, "name") == 0) {
        SceneObject* object = objectById(id);
        if (object) {
            object->name = luaL_checkstring(L, 3);
        }
    }
    return 0;
}

static int lua_Scene_findNode(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    const char* name = luaL_checkstring(L, arg);
    for (auto& object : Engine::instance().scene().objects()) {
        if (object.active && object.name == (name ? name : "")) {
            pushNode(L, (int)object.id);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int lua_Scene_load(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    const char* path = luaL_checkstring(L, arg);
    Engine::instance().requestSceneLoad(path ? path : "");
    return 0;
}

static int lua_Input_getMouseDeltaX(lua_State* L) {
    lua_pushnumber(L, Engine::instance().mouseDeltaX());
    return 1;
}

static int lua_Input_getMouseDeltaY(lua_State* L) {
    lua_pushnumber(L, Engine::instance().mouseDeltaY());
    return 1;
}

static int lua_Input_setMouseVisible(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    Engine::instance().setMouseVisible(lua_toboolean(L, arg) != 0);
    return 0;
}

static int lua_Input_lockMouse(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    Engine::instance().lockMouse(lua_toboolean(L, arg) != 0);
    return 0;
}

static int lua_Input_isMouseDown(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    lua_pushboolean(L, Engine::instance().isMouseDown((int)luaL_checkinteger(L, arg)));
    return 1;
}

static int lua_Input_isMousePressed(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    lua_pushboolean(L, Engine::instance().isMousePressed((int)luaL_checkinteger(L, arg)));
    return 1;
}

static std::string normalizeKeyName(const char* text) {
    std::string key = text ? text : "";
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) {
        return c == ' ' || c == '_' || c == '-';
    }), key.end());
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return (char)std::toupper(c);
    });
    return key;
}

static int keyCodeFromName(const char* text) {
    std::string key = normalizeKeyName(text);
    if (key.size() == 1) {
        char c = key[0];
        if (c >= 'A' && c <= 'Z') return (int)c;
        if (c >= '0' && c <= '9') return (int)c;
    }

    if (key == "SPACE") return SAPP_KEYCODE_SPACE;
    if (key == "ENTER" || key == "RETURN") return SAPP_KEYCODE_ENTER;
    if (key == "ESC" || key == "ESCAPE") return SAPP_KEYCODE_ESCAPE;
    if (key == "TAB") return SAPP_KEYCODE_TAB;
    if (key == "BACKSPACE") return SAPP_KEYCODE_BACKSPACE;
    if (key == "DELETE" || key == "DEL") return SAPP_KEYCODE_DELETE;
    if (key == "INSERT" || key == "INS") return SAPP_KEYCODE_INSERT;
    if (key == "LEFT") return SAPP_KEYCODE_LEFT;
    if (key == "RIGHT") return SAPP_KEYCODE_RIGHT;
    if (key == "UP") return SAPP_KEYCODE_UP;
    if (key == "DOWN") return SAPP_KEYCODE_DOWN;
    if (key == "PAGEUP") return SAPP_KEYCODE_PAGE_UP;
    if (key == "PAGEDOWN") return SAPP_KEYCODE_PAGE_DOWN;
    if (key == "HOME") return SAPP_KEYCODE_HOME;
    if (key == "END") return SAPP_KEYCODE_END;
    if (key == "SHIFT" || key == "LEFTSHIFT" || key == "LSHIFT") return SAPP_KEYCODE_LEFT_SHIFT;
    if (key == "RIGHTSHIFT" || key == "RSHIFT") return SAPP_KEYCODE_RIGHT_SHIFT;
    if (key == "CTRL" || key == "CONTROL" || key == "LEFTCTRL" || key == "LEFTCONTROL" || key == "LCTRL") return SAPP_KEYCODE_LEFT_CONTROL;
    if (key == "RIGHTCTRL" || key == "RIGHTCONTROL" || key == "RCTRL") return SAPP_KEYCODE_RIGHT_CONTROL;
    if (key == "ALT" || key == "LEFTALT" || key == "LALT") return SAPP_KEYCODE_LEFT_ALT;
    if (key == "RIGHTALT" || key == "RALT") return SAPP_KEYCODE_RIGHT_ALT;
    if (key == "F1") return SAPP_KEYCODE_F1;
    if (key == "F2") return SAPP_KEYCODE_F2;
    if (key == "F3") return SAPP_KEYCODE_F3;
    if (key == "F4") return SAPP_KEYCODE_F4;
    if (key == "F5") return SAPP_KEYCODE_F5;
    if (key == "F6") return SAPP_KEYCODE_F6;
    if (key == "F7") return SAPP_KEYCODE_F7;
    if (key == "F8") return SAPP_KEYCODE_F8;
    if (key == "F9") return SAPP_KEYCODE_F9;
    if (key == "F10") return SAPP_KEYCODE_F10;
    if (key == "F11") return SAPP_KEYCODE_F11;
    if (key == "F12") return SAPP_KEYCODE_F12;
    return SAPP_KEYCODE_INVALID;
}

static int keyCodeFromLua(lua_State* L, int index) {
    if (lua_isnumber(L, index)) {
        return (int)lua_tointeger(L, index);
    }
    if (lua_isstring(L, index)) {
        return keyCodeFromName(lua_tostring(L, index));
    }
    return SAPP_KEYCODE_INVALID;
}

static int lua_Keyboard_isDown(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    lua_pushboolean(L, Engine::instance().isKeyDown(keyCodeFromLua(L, arg)));
    return 1;
}

static int lua_Keyboard_isPressed(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    lua_pushboolean(L, Engine::instance().isKeyPressed(keyCodeFromLua(L, arg)));
    return 1;
}

static int lua_Keyboard_isReleased(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    lua_pushboolean(L, Engine::instance().isKeyReleased(keyCodeFromLua(L, arg)));
    return 1;
}

static int lua_Keyboard_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    int keyCode = keyCodeFromName(key);
    if (keyCode != SAPP_KEYCODE_INVALID) {
        lua_pushboolean(L, Engine::instance().isKeyDown(keyCode));
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_Input_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "mouseX") == 0) {
        lua_pushnumber(L, Engine::instance().mouseX());
        return 1;
    }
    if (std::strcmp(key, "mouseY") == 0) {
        lua_pushnumber(L, Engine::instance().mouseY());
        return 1;
    }
    if (std::strcmp(key, "screenWidth") == 0) {
        lua_pushinteger(L, sapp_width());
        return 1;
    }
    if (std::strcmp(key, "screenHeight") == 0) {
        lua_pushinteger(L, sapp_height());
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_Physics_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "gravity") == 0) {
        pushVector3(L, Engine::instance().physics().gravity());
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_Physics_newindex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "gravity") == 0) {
        Engine::instance().physics().setGravity(readVector3(L, 3));
    }
    return 0;
}

static int lua_Audio_playSound(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    const char* path = luaL_checkstring(L, arg);
    float volume = (float)luaL_optnumber(L, arg + 1, 1.0);
    lua_pushinteger(L, (lua_Integer)Engine::instance().audio().playSound(path ? path : "", volume));
    return 1;
}

static int lua_Audio_stopSound(lua_State* L) {
    int arg = lua_istable(L, 1) ? 2 : 1;
    Engine::instance().audio().stopSound((uint64_t)luaL_checkinteger(L, arg));
    return 0;
}

static int lua_Audio_stopAll(lua_State*) {
    Engine::instance().audio().stopAll();
    return 0;
}

static int lua_Time_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "deltaTime") == 0) {
        lua_getfield(L, LUA_REGISTRYINDEX, "realcore.deltaTime");
        return 1;
    }
    if (std::strcmp(key, "frame") == 0) {
        lua_pushinteger(L, (lua_Integer)sapp_frame_count());
        return 1;
    }
    return 0;
}

static void createObject(lua_State* L, const char* globalName, lua_CFunction index = nullptr, lua_CFunction newindex = nullptr) {
    lua_createtable(L, 0, 0);
    lua_createtable(L, 0, 2);
    if (index) {
        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");
    }
    if (newindex) {
        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");
    }
    lua_setmetatable(L, -2);
    lua_setglobal(L, globalName);
}

static void registerLuaApi(lua_State* L) {
    luaL_openlibs(L);

    lua_pushcfunction(L, lua_print);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, lua_Vector3);
    lua_setglobal(L, "Vector3");
    lua_pushcfunction(L, lua_loadScene);
    lua_setglobal(L, "loadScene");
    lua_pushcfunction(L, lua_quitGame);
    lua_setglobal(L, "quitGame");

    luaL_newmetatable(L, kNodeMeta);
    lua_pushcfunction(L, lua_Node_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_Node_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_Node_getPosition);
    lua_setfield(L, -2, "getPosition");
    lua_pushcfunction(L, lua_Node_setPosition);
    lua_setfield(L, -2, "setPosition");
    lua_pushcfunction(L, lua_Node_applyForce);
    lua_setfield(L, -2, "applyForce");
    lua_pushcfunction(L, lua_Node_applyImpulse);
    lua_setfield(L, -2, "applyImpulse");
    lua_pushcfunction(L, lua_Node_valid);
    lua_setfield(L, -2, "valid");
    lua_pop(L, 1);

    pushNode(L, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 1);
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "__node_id");
        luaL_getmetatable(L, kNodeMeta);
        lua_setmetatable(L, -2);
    }
    lua_setglobal(L, "node");

    createObject(L, "scene");
    lua_getglobal(L, "scene");
    lua_pushcfunction(L, lua_Scene_findNode);
    lua_setfield(L, -2, "findNode");
    lua_pushcfunction(L, lua_Scene_load);
    lua_setfield(L, -2, "load");
    lua_pop(L, 1);

    createObject(L, "input", lua_Input_index);
    lua_getglobal(L, "input");
    lua_pushcfunction(L, lua_Input_getMouseDeltaX);
    lua_setfield(L, -2, "getMouseDeltaX");
    lua_pushcfunction(L, lua_Input_getMouseDeltaY);
    lua_setfield(L, -2, "getMouseDeltaY");
    lua_pushcfunction(L, lua_Input_setMouseVisible);
    lua_setfield(L, -2, "setMouseVisible");
    lua_pushcfunction(L, lua_Input_lockMouse);
    lua_setfield(L, -2, "lockMouse");
    lua_pushcfunction(L, lua_Input_isMouseDown);
    lua_setfield(L, -2, "isMouseDown");
    lua_pushcfunction(L, lua_Input_isMousePressed);
    lua_setfield(L, -2, "isMousePressed");
    lua_pushcfunction(L, lua_Keyboard_isDown);
    lua_setfield(L, -2, "isKeyDown");
    lua_pushcfunction(L, lua_Keyboard_isPressed);
    lua_setfield(L, -2, "isKeyPressed");
    lua_pushcfunction(L, lua_Keyboard_isReleased);
    lua_setfield(L, -2, "isKeyReleased");
    lua_pushcfunction(L, lua_Keyboard_isDown);
    lua_setfield(L, -2, "keyDown");
    lua_pop(L, 1);

    createObject(L, "keyboard", lua_Keyboard_index);
    lua_getglobal(L, "keyboard");
    lua_pushcfunction(L, lua_Keyboard_isDown);
    lua_setfield(L, -2, "isDown");
    lua_pushcfunction(L, lua_Keyboard_isPressed);
    lua_setfield(L, -2, "isPressed");
    lua_pushcfunction(L, lua_Keyboard_isReleased);
    lua_setfield(L, -2, "isReleased");
    lua_pushcfunction(L, lua_Keyboard_isDown);
    lua_setfield(L, -2, "down");
    lua_pushcfunction(L, lua_Keyboard_isPressed);
    lua_setfield(L, -2, "pressed");
    lua_pushcfunction(L, lua_Keyboard_isReleased);
    lua_setfield(L, -2, "released");
    lua_pop(L, 1);

    createObject(L, "physics", lua_Physics_index, lua_Physics_newindex);

    createObject(L, "audio");
    lua_getglobal(L, "audio");
    lua_pushcfunction(L, lua_Audio_playSound);
    lua_setfield(L, -2, "playSound");
    lua_pushcfunction(L, lua_Audio_stopSound);
    lua_setfield(L, -2, "stopSound");
    lua_pushcfunction(L, lua_Audio_stopAll);
    lua_setfield(L, -2, "stopAll");
    lua_pop(L, 1);

    createObject(L, "time", lua_Time_index);
}

} // namespace

bool LuaScriptEngine::init() {
    return true;
}

void LuaScriptEngine::shutdown() {
    for (auto& [_, module] : m_modules) {
        closeModule(module);
    }
    m_modules.clear();
}

void LuaScriptEngine::closeModule(Module& module) {
    if (module.state) {
        lua_close(module.state);
        module.state = nullptr;
    }
}

bool LuaScriptEngine::loadScript(const std::string& scriptPath) {
    std::string moduleName = moduleNameForPath(scriptPath);
    unloadScript(moduleName);

    lua_State* L = luaL_newstate();
    if (!L) {
        std::printf("Failed to create Lua state for %s\n", scriptPath.c_str());
        return false;
    }

    registerLuaApi(L);
    setCurrentNodeId(L, 0);

    if (luaL_loadfile(L, scriptPath.c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::printf("[Lua ERROR] %s\n", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }

    Module module;
    module.state = L;
    module.path = scriptPath;
    m_modules[moduleName] = module;
    return true;
}

void LuaScriptEngine::unloadScript(const std::string& moduleName) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end()) {
        return;
    }
    closeModule(it->second);
    m_modules.erase(it);
}

std::string LuaScriptEngine::moduleNameForPath(const std::string& scriptPath) const {
    std::string moduleName = fs::path(scriptPath).stem().string();
    if (moduleName.empty()) {
        moduleName = "LuaScript";
    }
    for (char& c : moduleName) {
        if (!std::isalnum((unsigned char)c) && c != '_') {
            c = '_';
        }
    }
    return moduleName;
}

void LuaScriptEngine::callFunction(const std::string& moduleName, const std::string& funcName) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end() || !it->second.state) {
        return;
    }
    lua_State* L = it->second.state;
    setCurrentNodeId(L, currentNodeForModule(moduleName));
    lua_getglobal(L, funcName.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::printf("[Lua ERROR] %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void LuaScriptEngine::callFunctionFloat(const std::string& moduleName, const std::string& funcName, float value) {
    auto it = m_modules.find(moduleName);
    if (it == m_modules.end() || !it->second.state) {
        return;
    }
    lua_State* L = it->second.state;
    setCurrentNodeId(L, currentNodeForModule(moduleName));
    lua_pushnumber(L, value);
    lua_setfield(L, LUA_REGISTRYINDEX, "realcore.deltaTime");
    lua_getglobal(L, funcName.c_str());
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_pushnumber(L, value);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        std::printf("[Lua ERROR] %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}
