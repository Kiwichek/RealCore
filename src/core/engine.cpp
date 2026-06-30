#include <core/engine.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

static const char* rigidBodyShapeName(RigidBodyComponent::Shape shape) {
    switch (shape) {
        case RigidBodyComponent::Shape::Sphere: return "Sphere";
        case RigidBodyComponent::Shape::Capsule: return "Capsule";
        case RigidBodyComponent::Shape::Cylinder: return "Cylinder";
        case RigidBodyComponent::Shape::Box:
        default: return "Box";
    }
}

static RigidBodyComponent::Shape parseRigidBodyShape(std::string shape) {
    std::transform(shape.begin(), shape.end(), shape.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    if (shape == "sphere") return RigidBodyComponent::Shape::Sphere;
    if (shape == "capsule") return RigidBodyComponent::Shape::Capsule;
    if (shape == "cylinder") return RigidBodyComponent::Shape::Cylinder;
    return RigidBodyComponent::Shape::Box;
}

static uint32_t createRigidBodyForComponent(PhysicsWorld& physics, const Transform& transform, const RigidBodyComponent& rigidBody) {
    switch (rigidBody.shape) {
        case RigidBodyComponent::Shape::Sphere:
            return physics.addSphere(transform.position, rigidBody.radius, rigidBody.dynamic);
        case RigidBodyComponent::Shape::Capsule:
            return physics.addCapsule(transform.position, rigidBody.halfHeight, rigidBody.radius, rigidBody.dynamic);
        case RigidBodyComponent::Shape::Cylinder:
            return physics.addCylinder(transform.position, rigidBody.halfHeight, rigidBody.radius, rigidBody.dynamic);
        case RigidBodyComponent::Shape::Box:
        default:
            return physics.addBox(transform.position, rigidBody.halfExtent, rigidBody.dynamic);
    }
}

static void writeRigidBodyLine(std::ostream& out, const RigidBodyComponent& rigidBody) {
    out << "RigidBody "
        << rigidBody.syncTransform << " " << rigidBody.frozen << " "
        << rigidBodyShapeName(rigidBody.shape) << " " << rigidBody.dynamic << " "
        << rigidBody.halfExtent.x << " " << rigidBody.halfExtent.y << " " << rigidBody.halfExtent.z << " "
        << rigidBody.radius << " " << rigidBody.halfHeight << "\n";
}

Engine& Engine::instance() {
    static Engine engine;
    return engine;
}

static bool projectToScreen(const Mat4& viewProj, const Vec3& worldPos, float width, float height, float& outX, float& outY, float& outDepth) {
    float x = viewProj.m[0] * worldPos.x + viewProj.m[4] * worldPos.y + viewProj.m[8]  * worldPos.z + viewProj.m[12];
    float y = viewProj.m[1] * worldPos.x + viewProj.m[5] * worldPos.y + viewProj.m[9]  * worldPos.z + viewProj.m[13];
    float z = viewProj.m[2] * worldPos.x + viewProj.m[6] * worldPos.y + viewProj.m[10] * worldPos.z + viewProj.m[14];
    float w = viewProj.m[3] * worldPos.x + viewProj.m[7] * worldPos.y + viewProj.m[11] * worldPos.z + viewProj.m[15];

    if (std::abs(w) < 0.0001f) {
        return false;
    }

    float invW = 1.0f / w;
    float ndcX = x * invW;
    float ndcY = y * invW;
    float ndcZ = z * invW;

    if (ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }

    outX = (ndcX * 0.5f + 0.5f) * width;
    outY = (0.5f - ndcY * 0.5f) * height;
    outDepth = ndcZ;
    return true;
}

static SceneLighting findSceneLighting(const Scene& scene) {
    SceneLighting lighting;
    lighting.direction = { 0.45f, 0.85f, 0.30f };
    lighting.color = { 1.0f, 0.96f, 0.86f };
    lighting.intensity = 1.25f;
    lighting.ambient = 0.18f;

    for (const auto& object : scene.objects()) {
        if (!object.active || !object.hasLight || !object.light.enabled) {
            continue;
        }

        lighting.direction = object.light.direction;
        lighting.color = object.light.color;
        lighting.intensity = object.light.intensity;
        lighting.ambient = object.light.ambient;
        break;
    }

    return lighting;
}

static Mat4 makeLightViewProj(const SceneLighting& lighting) {
    Vec3 lightDir = lighting.direction.normalized();
    if (lightDir.length() <= 0.0f) {
        lightDir = { 0.5f, 0.8f, 0.6f };
    }

    Vec3 center{ 0.0f, 0.0f, 0.0f };
    Vec3 eye = center + lightDir * 32.0f;
    Vec3 up{ 0.0f, 1.0f, 0.0f };
    if (std::abs(lightDir.dot(up)) > 0.95f) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    Mat4 lightView = Mat4::lookAt(eye, center, up);
    Mat4 lightProj = Mat4::orthographic(-28.0f, 28.0f, -28.0f, 28.0f, 0.1f, 80.0f);
    return lightProj * lightView;
}

static fs::path currentExecutablePath() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return fs::path(buffer);
    }
#endif
    return fs::current_path() / "RealCore.exe";
}

static std::string sanitizeFileStem(std::string name) {
    if (name.empty()) {
        name = "RealCoreGame";
    }

    for (char& c : name) {
        const bool invalid = c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*';
        if (invalid || std::iscntrl((unsigned char)c)) {
            c = '_';
        }
    }
    return name.empty() ? "RealCoreGame" : name;
}

static bool isLuaScriptPath(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".lua";
}

static bool pathStartsWith(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path absPath = fs::absolute(path, ec).lexically_normal();
    if (ec) {
        return false;
    }
    fs::path absRoot = fs::absolute(root, ec).lexically_normal();
    if (ec) {
        return false;
    }

    auto pit = absPath.begin();
    auto rit = absRoot.begin();
    for (; rit != absRoot.end(); ++rit, ++pit) {
        if (pit == absPath.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

static bool copyDirectoryTree(const fs::path& source, const fs::path& destination, std::error_code& ec) {
    if (!fs::exists(source, ec)) {
        ec.clear();
        return true;
    }

    fs::create_directories(destination, ec);
    if (ec) {
        return false;
    }

    for (const auto& entry : fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            return false;
        }

        fs::path relative = fs::relative(entry.path(), source, ec);
        if (ec) {
            return false;
        }

        fs::path target = destination / relative;
        if (entry.is_directory(ec)) {
            fs::create_directories(target, ec);
        } else if (entry.is_regular_file(ec)) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) {
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
            }
        }

        if (ec) {
            return false;
        }
    }

    return true;
}

static bool isLikelyAssetSidecar(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".mtl" ||
        ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr" ||
        ext == ".wav" || ext == ".mp3" || ext == ".ogg";
}

static std::string makeRelativeGeneric(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path relative = fs::relative(path, root, ec);
    if (ec) {
        return path.generic_string();
    }
    return relative.generic_string();
}

static std::string remapAssetForExport(
    const std::string& sourcePath,
    const fs::path& projectRoot,
    const fs::path& exportRoot,
    std::error_code& ec)
{
    if (sourcePath.empty()) {
        return {};
    }

    fs::path source = fs::absolute(fs::path(sourcePath), ec).lexically_normal();
    if (ec) {
        return sourcePath;
    }

    if (pathStartsWith(source, projectRoot)) {
        return makeRelativeGeneric(source, projectRoot);
    }

    if (pathStartsWith(source, exportRoot)) {
        return makeRelativeGeneric(source, exportRoot);
    }

    fs::path externalDir = exportRoot / "Assets" / "External" / sanitizeFileStem(source.stem().string());
    fs::create_directories(externalDir, ec);
    if (ec) {
        return sourcePath;
    }

    fs::path sourceDir = source.parent_path();
    try {
        for (const auto& entry : fs::directory_iterator(sourceDir)) {
            if (!entry.is_regular_file() || !isLikelyAssetSidecar(entry.path())) {
                continue;
            }
            fs::copy_file(entry.path(), externalDir / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                return sourcePath;
            }
        }
    } catch (...) {
        fs::copy_file(source, externalDir / source.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return sourcePath;
        }
    }

    return makeRelativeGeneric(externalDir / source.filename(), exportRoot);
}

static bool readGameManifest(const fs::path& path, std::string& scenePath, std::string& title) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "RealCoreGame" || version != 1) {
        return false;
    }

    std::string token;
    while (in >> token) {
        if (token == "Scene") {
            in >> std::quoted(scenePath);
        } else if (token == "Title") {
            in >> std::quoted(title);
        }
    }

    return !scenePath.empty();
}

void Engine::configureLaunch(int argc, char* argv[]) {
    m_runtimeMode = false;
    m_launchScenePath.clear();
    m_windowTitle = "RealCore Engine";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--game" || arg == "-game") {
            m_runtimeMode = true;
        } else if ((arg == "--scene" || arg == "-scene") && i + 1 < argc) {
            m_launchScenePath = argv[++i] ? argv[i] : "";
        } else if ((arg == "--title" || arg == "-title") && i + 1 < argc) {
            m_windowTitle = argv[++i] ? argv[i] : "";
        }
    }

    if (!m_runtimeMode) {
        std::string manifestScene;
        std::string manifestTitle;
        if (readGameManifest(fs::current_path() / "realcore.game", manifestScene, manifestTitle)) {
            m_runtimeMode = true;
            m_launchScenePath = manifestScene;
            if (!manifestTitle.empty()) {
                m_windowTitle = manifestTitle;
            }
        }
    }

    if (m_runtimeMode) {
        if (m_launchScenePath.empty()) {
            m_launchScenePath = (fs::path("Scenes") / "Main.rcscene").string();
        }
        if (m_windowTitle.empty() || m_windowTitle == "RealCore Engine") {
            m_windowTitle = "RealCore Game";
        }
    }
}

bool Engine::init() {
    sg_desc desc = {};
    desc.environment = sglue_environment();
    sg_setup(&desc);
    if (!sg_isvalid()) {
        return false;
    }

    if (!m_renderer.init()) {
        return false;
    }

    if (!m_resources.init()) {
        return false;
    }

    if (!m_physics.init()) {
        return false;
    }
    m_groundBodyId = m_physics.addPlane({ 0, 0, 0 }, { 0, 1, 0 });

    if (!m_runtimeMode) {
        if (!m_gui.init()) {
            return false;
        }
        m_guiInitialized = true;
    }

    if (!m_audio.init()) {
        return false;
    }

    if (!m_scriptEngine.init()) {
        return false;
    }
    if (!m_luaScriptEngine.init()) {
        return false;
    }

    if (!m_gizmo.init()) {
        return false;
    }

    int w = sapp_width();
    int h = sapp_height();
    if (h == 0) h = 1;
    m_camera.setPerspective(60.0f * 3.14159f / 180.0f, (float)w / (float)h, 0.01f, 1000.0f);

    if (m_runtimeMode) {
        initRuntimeScene();
    }

    m_running = true;
    return true;
}

void Engine::initRuntimeScene() {
    fs::path scenePath = m_launchScenePath.empty() ? (fs::path("Scenes") / "Main.rcscene") : fs::path(m_launchScenePath);
    if (!scenePath.is_absolute()) {
        scenePath = fs::current_path() / scenePath;
    }

    if (!loadScene(scenePath.string())) {
        clearSceneObjects();
        createDefaultScene();
    }

    m_useSceneCamera = true;
    selectEntity(InvalidSceneEntity);
    m_playState = PlayState::Playing;
}

std::string Engine::resolveProjectPath(const std::string& path) const {
    if (path.empty()) {
        return {};
    }

    fs::path input(path);
    if (input.is_absolute()) {
        return input.lexically_normal().string();
    }

    fs::path root = m_projectPath.empty() ? fs::current_path() : fs::path(m_projectPath);
    if (!root.is_absolute()) {
        root = fs::current_path() / root;
    }
    return (root / input).lexically_normal().string();
}

std::string Engine::resolveScenePath(const std::string& path) const {
    if (path.empty()) {
        return {};
    }

    fs::path input(path);
    if (!input.has_extension()) {
        input += ".rcscene";
    }

    if (input.is_absolute()) {
        return input.lexically_normal().string();
    }

    fs::path root = m_projectPath.empty() ? fs::current_path() : fs::path(m_projectPath);
    if (!root.is_absolute()) {
        root = fs::current_path() / root;
    }

    fs::path direct = (root / input).lexically_normal();
    std::error_code ec;
    if (fs::exists(direct, ec)) {
        return direct.string();
    }

    if (input.parent_path().empty()) {
        return (root / "Scenes" / input).lexically_normal().string();
    }
    return direct.string();
}

std::string Engine::makeProjectRelativePath(const std::string& path) const {
    if (path.empty()) {
        return {};
    }

    std::error_code ec;
    fs::path absolute = fs::absolute(fs::path(path), ec).lexically_normal();
    if (ec) {
        return path;
    }

    fs::path root = m_projectPath.empty() ? fs::current_path() : fs::path(m_projectPath);
    if (!root.is_absolute()) {
        root = fs::current_path() / root;
    }
    root = fs::absolute(root, ec).lexically_normal();
    if (ec) {
        return path;
    }

    if (pathStartsWith(absolute, root)) {
        return fs::relative(absolute, root, ec).generic_string();
    }
    return absolute.string();
}

bool Engine::attachScript(SceneEntity entity, const std::string& scriptPath) {
    SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active || scriptPath.empty()) {
        return false;
    }

    if (object->hasScript && !object->script.moduleName.empty()) {
        if (object->script.initialized) {
            if (object->script.language == ScriptComponent::Language::Lua) {
                m_luaScriptEngine.callFunction(object->script.moduleName, "destroy");
            } else {
                m_scriptEngine.callFunction(object->script.moduleName, "destroy");
            }
        }
        if (object->script.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.unloadScript(object->script.moduleName);
        } else {
            m_scriptEngine.unloadScript(object->script.moduleName);
        }
    }

    std::string resolvedPath = resolveProjectPath(scriptPath);
    const bool luaScript = isLuaScriptPath(resolvedPath);
    bool loaded = luaScript
        ? m_luaScriptEngine.loadScript(resolvedPath)
        : m_scriptEngine.loadScript(resolvedPath);
    if (!loaded) {
        m_lastProjectError = "Failed to load script: " + scriptPath;
        return false;
    }

    std::string storedPath = makeProjectRelativePath(resolvedPath);
    ScriptComponent* script = m_scene.addScript(entity, luaScript
        ? m_luaScriptEngine.moduleNameForPath(resolvedPath)
        : m_scriptEngine.moduleNameForPath(resolvedPath));
    if (!script) {
        return false;
    }
    script->scriptPath = storedPath;
    script->language = luaScript ? ScriptComponent::Language::Lua : ScriptComponent::Language::AngelScript;
    script->initialized = false;

    if (m_playState == PlayState::Playing) {
        m_sceneScriptsInitialized = false;
    }
    return true;
}

void Engine::detachScript(SceneEntity entity) {
    SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active || !object->hasScript) {
        return;
    }

    if (object->script.initialized) {
        if (object->script.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.callFunction(object->script.moduleName, "destroy");
        } else {
            m_scriptEngine.callFunction(object->script.moduleName, "destroy");
        }
    }
    if (object->script.language == ScriptComponent::Language::Lua) {
        m_luaScriptEngine.unloadScript(object->script.moduleName);
    } else {
        m_scriptEngine.unloadScript(object->script.moduleName);
    }
    m_scene.removeScript(entity);
}

void Engine::requestSceneLoad(const std::string& path) {
    if (path.empty()) {
        return;
    }
    m_pendingScenePath = path;
}

void Engine::requestQuit() {
    sapp_request_quit();
}

void Engine::loadSceneScripts() {
    for (auto& object : m_scene.objects()) {
        if (!object.active || !object.hasScript || object.script.scriptPath.empty()) {
            continue;
        }

        std::string resolvedPath = resolveProjectPath(object.script.scriptPath);
        const bool luaScript = isLuaScriptPath(resolvedPath);
        bool loaded = luaScript
            ? m_luaScriptEngine.loadScript(resolvedPath)
            : m_scriptEngine.loadScript(resolvedPath);
        if (loaded) {
            object.script.language = luaScript ? ScriptComponent::Language::Lua : ScriptComponent::Language::AngelScript;
            object.script.moduleName = luaScript
                ? m_luaScriptEngine.moduleNameForPath(resolvedPath)
                : m_scriptEngine.moduleNameForPath(resolvedPath);
            object.script.initialized = false;
        } else {
            m_lastProjectError = "Failed to load script: " + object.script.scriptPath;
        }
    }
    m_sceneScriptsInitialized = false;
}

void Engine::initSceneScripts() {
    if (m_sceneScriptsInitialized) {
        return;
    }

    for (auto& object : m_scene.objects()) {
        if (!object.active || !object.hasScript || object.script.moduleName.empty() || object.script.initialized) {
            continue;
        }

        if (object.script.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.callFunction(object.script.moduleName, "init");
        } else {
            m_scriptEngine.callFunction(object.script.moduleName, "init");
        }
        object.script.initialized = true;
    }
    m_sceneScriptsInitialized = true;
}

void Engine::updateSceneScripts(float deltaTime) {
    initSceneScripts();
    if (!m_pendingScenePath.empty()) {
        return;
    }

    struct ScriptCall {
        std::string moduleName;
        ScriptComponent::Language language = ScriptComponent::Language::AngelScript;
    };
    std::vector<ScriptCall> modules;
    for (const auto& object : m_scene.objects()) {
        if (!object.active || !object.hasScript || object.script.moduleName.empty()) {
            continue;
        }
        modules.push_back({ object.script.moduleName, object.script.language });
    }

    for (const ScriptCall& call : modules) {
        if (call.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.callFunctionFloat(call.moduleName, "update", deltaTime);
        } else {
            m_scriptEngine.callFunctionFloat(call.moduleName, "update", deltaTime);
        }
        if (!m_pendingScenePath.empty()) {
            break;
        }
    }
}

void Engine::shutdownSceneScripts() {
    for (auto& object : m_scene.objects()) {
        if (!object.active || !object.hasScript || object.script.moduleName.empty() || !object.script.initialized) {
            continue;
        }

        if (object.script.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.callFunction(object.script.moduleName, "destroy");
        } else {
            m_scriptEngine.callFunction(object.script.moduleName, "destroy");
        }
        object.script.initialized = false;
    }
    m_sceneScriptsInitialized = false;
}

void Engine::processPendingSceneLoad() {
    if (m_pendingScenePath.empty()) {
        return;
    }

    std::string requestedPath = m_pendingScenePath;
    m_pendingScenePath.clear();

    std::string scenePath = resolveScenePath(requestedPath);
    if (!loadScene(scenePath)) {
        if (m_lastProjectError.empty()) {
            m_lastProjectError = "Failed to load scene: " + requestedPath;
        }
        return;
    }

    if (m_runtimeMode) {
        m_useSceneCamera = true;
    }
    if (m_playState == PlayState::Playing) {
        m_sceneScriptsInitialized = false;
    }
}

void Engine::frame() {
    float dt = (float)sapp_frame_duration();
    if (dt == 0) dt = 0.016f;

    m_mousePressed[0] = m_mousePressed[1] = m_mousePressed[2] = false;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    for (int i = 0; i < 512; i++) {
        m_keyPressed[i] = false;
        m_keyReleased[i] = false;
    }

    int w = sapp_width();
    int h = sapp_height();
    if (h == 0) h = 1;
    m_camera.setPerspective(m_camera.fovY(), (float)w / (float)h, m_camera.zNear(), m_camera.zFar());

    if (m_playState == PlayState::Playing) {
        updateSceneScripts(dt);
        processPendingSceneLoad();
        m_physics.step(dt);
    }
    m_camera.update(dt);
    if (!m_runtimeMode) {
        m_gui.newFrame();
    }

    // optionally render preview offscreen BEFORE main pass
    if (!m_runtimeMode && m_previewMesh != 0) {
        renderPreview();
    }

    renderShadowMap();

    sg_pass pass = {};
    pass.action = m_renderer.passAction();
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    m_renderer.render();
    renderScene();
    if (!m_runtimeMode) {
        m_gui.render();
    }
    sg_end_pass();
    sg_commit();
}

void Engine::setMouseVisible(bool visible) {
    sapp_show_mouse(visible);
}

void Engine::lockMouse(bool lock) {
    sapp_lock_mouse(lock);
}

void Engine::renderShadowMap() {
    for (auto& object : m_scene.objects()) {
        if (object.active && object.hasRigidBody && object.rigidBody.syncTransform) {
            object.transform.local.position = m_physics.getPosition(object.rigidBody.bodyId);
        }
    }

    m_scene.updateWorldTransforms();

    SceneLighting lighting = findSceneLighting(m_scene);
    Mat4 lightViewProj = makeLightViewProj(lighting);
    m_renderer.setLightViewProj(lightViewProj);
    m_renderer.setShadowsEnabled(true);

    m_renderer.beginShadowPass();
    for (const auto& object : m_scene.objects()) {
        if (!object.active || !object.hasMeshRenderer || !object.meshRenderer.visible || object.hasLight) {
            continue;
        }

        Mesh* mesh = m_resources.getMesh(object.meshRenderer.meshHandle);
        if (mesh) {
            m_renderer.drawMeshShadow(*mesh, object.transform.world, lightViewProj);
        }
    }
    m_renderer.endShadowPass();
}

void Engine::renderScene() {
    for (auto& object : m_scene.objects()) {
        if (object.active && object.hasRigidBody && object.rigidBody.syncTransform) {
            object.transform.local.position = m_physics.getPosition(object.rigidBody.bodyId);
        }
    }

    m_scene.updateWorldTransforms();

    SceneLighting lighting = findSceneLighting(m_scene);
    m_renderer.setLighting(lighting);
    m_renderer.setLightViewProj(makeLightViewProj(lighting));
    m_renderer.setShadowsEnabled(true);

    Mat4 view;
    Mat4 proj;
    Vec3 cameraPosition;
    resolveRenderCamera(view, proj, &cameraPosition);

    if (!m_runtimeMode) {
        m_renderer.drawGrid(view, proj, cameraPosition);

        if (m_selectedEntity != InvalidSceneEntity) {
            const SceneObject* sel = m_scene.getObject(m_selectedEntity);
            if (sel && sel->active) {
                Vec3 pos = {
                    sel->transform.world.m[12],
                    sel->transform.world.m[13],
                    sel->transform.world.m[14]
                };
                m_gizmo.draw(view, proj, pos);
            }
        }
    }

    for (const auto& object : m_scene.objects()) {
        if (!object.active || !object.hasMeshRenderer || !object.meshRenderer.visible) {
            continue;
        }

        Mesh* mesh = m_resources.getMesh(object.meshRenderer.meshHandle);
        if (mesh) {
            m_renderer.drawMesh(*mesh, object.transform.world, view, proj);
        }
    }

    if (!m_runtimeMode) {
        float aspect = (float)std::max(1, sapp_width()) / (float)std::max(1, sapp_height());
        for (const auto& object : m_scene.objects()) {
            if (!object.active) {
                continue;
            }

            Vec3 position = {
                object.transform.world.m[12],
                object.transform.world.m[13],
                object.transform.world.m[14]
            };

            if (object.hasCamera && object.camera.enabled) {
                m_gizmo.drawCameraFrustum(
                    view,
                    proj,
                    object.transform.world,
                    object.camera.fovY,
                    aspect,
                    object.camera.zNear,
                    object.camera.zFar);
            }

            if (object.hasLight && object.light.enabled) {
                m_gizmo.drawLightDirection(view, proj, position, object.light.direction);
            }
        }

        for (SceneEntity selectedEntity : m_selectedEntities) {
            const SceneObject* selected = m_scene.getObject(selectedEntity);
            if (!selected || !selected->active || !selected->hasMeshRenderer || !selected->meshRenderer.visible) {
                continue;
            }

            Mesh* mesh = m_resources.getMesh(selected->meshRenderer.meshHandle);
            if (mesh) {
                m_renderer.drawSelectionOutline(*mesh, selected->transform.world, view, proj);
            }
        }
    }
}

bool Engine::resolveRenderCamera(Mat4& view, Mat4& proj, Vec3* cameraPosition) {
    int w = sapp_width();
    int h = sapp_height();
    if (h == 0) h = 1;
    float aspect = (float)w / (float)h;

    if (m_useSceneCamera) {
        SceneEntity cameraEntity = m_scene.findPrimaryCamera();
        const SceneObject* cameraObject = m_scene.getObject(cameraEntity);
        const CameraComponent* camera = m_scene.getCamera(cameraEntity);
        if (cameraObject && camera && camera->enabled) {
            Vec3 position = {
                cameraObject->transform.world.m[12],
                cameraObject->transform.world.m[13],
                cameraObject->transform.world.m[14]
            };
            Vec3 forwardPoint = cameraObject->transform.world.transformPoint({ 0, 0, 1 });
            Vec3 upPoint = cameraObject->transform.world.transformPoint({ 0, 1, 0 });
            Vec3 forward = (forwardPoint - position).normalized();
            Vec3 up = (upPoint - position).normalized();

            view = Mat4::lookAt(position, position + forward, up);
            proj = Mat4::perspective(camera->fovY, aspect, camera->zNear, camera->zFar);
            if (cameraPosition) {
                *cameraPosition = position;
            }
            return true;
        }
    }

    view = m_camera.viewMatrix();
    proj = m_camera.projMatrix();
    if (cameraPosition) {
        *cameraPosition = m_camera.position();
    }
    return false;
}

bool Engine::isEntitySelected(SceneEntity entity) const {
    return std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity) != m_selectedEntities.end();
}

void Engine::selectEntity(SceneEntity entity) {
    if (entity != InvalidSceneEntity) {
        const SceneObject* object = m_scene.getObject(entity);
        if (!object || !object->active) {
            entity = InvalidSceneEntity;
        }
    }
    m_selectedEntity = entity;
    m_selectedEntities.clear();
    if (entity != InvalidSceneEntity) {
        m_selectedEntities.push_back(entity);
    }
}

void Engine::toggleEntitySelection(SceneEntity entity) {
    if (entity == InvalidSceneEntity) {
        return;
    }

    const SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active) {
        return;
    }

    auto it = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
    if (it != m_selectedEntities.end()) {
        m_selectedEntities.erase(it);
        if (m_selectedEntity == entity) {
            m_selectedEntity = m_selectedEntities.empty() ? InvalidSceneEntity : m_selectedEntities.back();
        }
        return;
    }

    m_selectedEntities.push_back(entity);
    m_selectedEntity = entity;
}

SceneEntity Engine::pickSceneEntity(float mouseX, float mouseY) {
    m_scene.updateWorldTransforms();

    Mat4 view;
    Mat4 proj;
    resolveRenderCamera(view, proj);
    Mat4 viewProj = proj * view;

    float width = (float)sapp_width();
    float height = (float)sapp_height();
    SceneEntity bestEntity = InvalidSceneEntity;
    float bestDepth = std::numeric_limits<float>::max();

    for (const auto& object : m_scene.objects()) {
        if (!object.active || !object.hasMeshRenderer || !object.meshRenderer.visible) {
            continue;
        }

        Mesh* mesh = m_resources.getMesh(object.meshRenderer.meshHandle);
        if (!mesh || !mesh->bounds().valid) {
            continue;
        }

        Vec3 center = object.transform.world.transformPoint(mesh->bounds().center);
        Vec3 radiusPoint = object.transform.world.transformPoint(mesh->bounds().center + Vec3{ mesh->bounds().radius, 0, 0 });

        float centerX, centerY, centerDepth;
        float radiusX, radiusY, radiusDepth;
        if (!projectToScreen(viewProj, center, width, height, centerX, centerY, centerDepth)) {
            continue;
        }
        if (!projectToScreen(viewProj, radiusPoint, width, height, radiusX, radiusY, radiusDepth)) {
            radiusX = centerX + 32.0f;
            radiusY = centerY;
        }

        float screenRadius = std::max(18.0f, std::hypot(radiusX - centerX, radiusY - centerY));
        float dist = std::hypot(mouseX - centerX, mouseY - centerY);
        if (dist <= screenRadius && centerDepth < bestDepth) {
            bestDepth = centerDepth;
            bestEntity = object.id;
        }
    }

    return bestEntity;
}

bool Engine::moveSelectedEntityByKey(uint32_t key, uint32_t modifiers) {
    SceneObject* selected = m_scene.getObject(m_selectedEntity);
    if (!selected || !selected->active) {
        return false;
    }

    TransformComponent* transform = m_scene.getTransform(m_selectedEntity);
    if (!transform) {
        return false;
    }

    float step = (modifiers & SAPP_MODIFIER_SHIFT) ? 1.0f : 0.25f;
    if (modifiers & SAPP_MODIFIER_ALT) {
        float rotationStep = ((modifiers & SAPP_MODIFIER_SHIFT) ? 45.0f : 15.0f) * 3.14159265f / 180.0f;
        switch (key) {
            case SAPP_KEYCODE_LEFT:
                transform->local.rotation.y -= rotationStep;
                return true;
            case SAPP_KEYCODE_RIGHT:
                transform->local.rotation.y += rotationStep;
                return true;
            case SAPP_KEYCODE_UP:
                transform->local.rotation.x -= rotationStep;
                return true;
            case SAPP_KEYCODE_DOWN:
                transform->local.rotation.x += rotationStep;
                return true;
            case SAPP_KEYCODE_PAGE_UP:
                transform->local.rotation.z += rotationStep;
                return true;
            case SAPP_KEYCODE_PAGE_DOWN:
                transform->local.rotation.z -= rotationStep;
                return true;
            default:
                return false;
        }
    }

    switch (key) {
        case SAPP_KEYCODE_LEFT:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ -step, 0.0f, 0.0f });
            return true;
        case SAPP_KEYCODE_RIGHT:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ step, 0.0f, 0.0f });
            return true;
        case SAPP_KEYCODE_UP:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ 0.0f, 0.0f, step });
            return true;
        case SAPP_KEYCODE_DOWN:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ 0.0f, 0.0f, -step });
            return true;
        case SAPP_KEYCODE_PAGE_UP:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ 0.0f, step, 0.0f });
            return true;
        case SAPP_KEYCODE_PAGE_DOWN:
            setEntityPosition(m_selectedEntity, transform->local.position + Vec3{ 0.0f, -step, 0.0f });
            return true;
        default:
            return false;
    }
}

SceneEntity Engine::addRenderItem(MeshHandle handle, const std::string& name) {
    if (handle == 0) {
        return InvalidSceneEntity;
    }

    SceneEntity entity = m_scene.createMeshObject(handle, name);
    selectEntity(entity);
    focusOnEntity(entity);
    return entity;
}

SceneEntity Engine::importRenderItem(const std::string& path) {
    fs::path source(path);
    std::string loadPath = path;

    if (hasProject() && fs::exists(source)) {
        fs::path assetsDir = fs::path(m_projectPath) / "Assets";
        std::error_code ec;
        fs::create_directories(assetsDir, ec);

        auto isImportSidecar = [](const fs::path& file) {
            std::string ext = file.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](char c) { return (char)std::tolower((unsigned char)c); });
            return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
                   ext == ".mtl" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                   ext == ".tga" || ext == ".bmp" || ext == ".hdr";
        };

        fs::path dest;
        std::string ext = source.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](char c) { return (char)std::tolower((unsigned char)c); });

        if (ext == ".zip") {
            dest = assetsDir / source.filename();
            if (fs::absolute(source).lexically_normal() != fs::absolute(dest).lexically_normal()) {
                fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
            }
        } else {
            fs::path assetFolder = assetsDir / source.stem();
            fs::create_directories(assetFolder, ec);
            dest = assetFolder / source.filename();

            try {
                for (const auto& entry : fs::directory_iterator(source.parent_path())) {
                    if (!entry.is_regular_file() || !isImportSidecar(entry.path())) {
                        continue;
                    }
                    fs::copy_file(entry.path(), assetFolder / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
                    ec.clear();
                }
            } catch (...) {
            }
        }

        if (!ec && !dest.empty()) {
            loadPath = dest.string();
        }
    }

    MeshHandle handle = m_resources.loadMesh(loadPath);
    if (handle == 0) {
        return InvalidSceneEntity;
    }
    return addRenderItem(handle, fs::path(loadPath).filename().string());
}

void Engine::setEntityPosition(SceneEntity entity, const Vec3& position) {
    SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active) {
        return;
    }

    object->transform.local.position = position;
    if (object->hasRigidBody && object->rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
        m_physics.setPosition(object->rigidBody.bodyId, position);
        if (m_playState != PlayState::Playing) {
            m_physics.resetMotion(object->rigidBody.bodyId);
        }
    }
}

void Engine::createDefaultScene() {
    SceneEntity cameraEntity = m_scene.createObject("Main Camera");
    if (SceneObject* cameraObject = m_scene.getObject(cameraEntity)) {
        cameraObject->transform.local.position = { 0, 1, -5 };
        cameraObject->transform.local.rotation = { -0.2f, 0, 0 };
    }
    if (CameraComponent* camera = m_scene.addCamera(cameraEntity)) {
        camera->primary = true;
        camera->enabled = true;
    }

}

void Engine::clearSceneObjects() {
    for (const auto& object : m_scene.objects()) {
        if (object.active && object.hasRigidBody && object.rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
            m_physics.removeBody(object.rigidBody.bodyId);
        }
        if (object.active && object.hasScript && !object.script.moduleName.empty()) {
            if (object.script.language == ScriptComponent::Language::Lua) {
                m_luaScriptEngine.unloadScript(object.script.moduleName);
            } else {
                m_scriptEngine.unloadScript(object.script.moduleName);
            }
        }
    }
    m_scene.clear();
    selectEntity(InvalidSceneEntity);
    m_previewMesh = 0;
}

void Engine::deleteEntity(SceneEntity entity) {
    SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active) {
        return;
    }

    if (object->hasRigidBody && object->rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
        m_physics.removeBody(object->rigidBody.bodyId);
    }
    if (object->hasScript && !object->script.moduleName.empty()) {
        if (object->script.initialized) {
            if (object->script.language == ScriptComponent::Language::Lua) {
                m_luaScriptEngine.callFunction(object->script.moduleName, "destroy");
            } else {
                m_scriptEngine.callFunction(object->script.moduleName, "destroy");
            }
        }
        if (object->script.language == ScriptComponent::Language::Lua) {
            m_luaScriptEngine.unloadScript(object->script.moduleName);
        } else {
            m_scriptEngine.unloadScript(object->script.moduleName);
        }
    }
    m_scene.destroyObject(entity);

    auto it = std::find(m_selectedEntities.begin(), m_selectedEntities.end(), entity);
    if (it != m_selectedEntities.end()) {
        m_selectedEntities.erase(it);
    }
    if (m_selectedEntity == entity) {
        m_selectedEntity = m_selectedEntities.empty() ? InvalidSceneEntity : m_selectedEntities.back();
    }
}

void Engine::deleteSelectedEntity() {
    std::vector<SceneEntity> entities = m_selectedEntities;
    for (SceneEntity entity : entities) {
        deleteEntity(entity);
    }
    selectEntity(InvalidSceneEntity);
}

void Engine::play() {
    if (m_playState == PlayState::Stopped) {
        capturePlaySnapshot();
        m_sceneScriptsInitialized = false;
    }
    m_playState = PlayState::Playing;
    m_useSceneCamera = true;
}

void Engine::pause() {
    if (m_playState == PlayState::Playing) {
        m_playState = PlayState::Paused;
    }
}

void Engine::stop() {
    if (m_playState == PlayState::Stopped) {
        return;
    }

    shutdownSceneScripts();
    restorePlaySnapshot();
    m_playSnapshot.clear();
    m_playState = PlayState::Stopped;
    if (!m_runtimeMode) {
        m_useSceneCamera = false;
    }
}

void Engine::capturePlaySnapshot() {
    m_playSnapshot.clear();
    for (const auto& object : m_scene.objects()) {
        if (!object.active) {
            continue;
        }
        m_playSnapshot.push_back({ object.id, object.transform.local });
        if (object.hasRigidBody && object.rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
            m_physics.setPosition(object.rigidBody.bodyId, object.transform.local.position);
            m_physics.resetMotion(object.rigidBody.bodyId);
            m_physics.setFrozen(object.rigidBody.bodyId, object.rigidBody.frozen);
        }
    }
}

void Engine::restorePlaySnapshot() {
    for (const TransformSnapshot& snapshot : m_playSnapshot) {
        SceneObject* object = m_scene.getObject(snapshot.entity);
        if (!object || !object->active) {
            continue;
        }

        object->transform.local = snapshot.transform;
        if (object->hasRigidBody && object->rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
            m_physics.setPosition(object->rigidBody.bodyId, snapshot.transform.position);
            m_physics.resetMotion(object->rigidBody.bodyId);
            m_physics.setFrozen(object->rigidBody.bodyId, object->rigidBody.frozen);
        }
    }
    m_scene.updateWorldTransforms();
}

void Engine::focusOnItem(MeshHandle handle) {
    focusOnEntity(m_scene.findFirstByMesh(handle));
}

void Engine::focusOnEntity(SceneEntity entity) {
    SceneObject* object = m_scene.getObject(entity);
    if (!object || !object->active) {
        return;
    }

    m_scene.updateWorldTransforms();

    MeshRendererComponent* meshRenderer = m_scene.getMeshRenderer(entity);
    Mesh* mesh = meshRenderer ? m_resources.getMesh(meshRenderer->meshHandle) : nullptr;
    if (mesh && mesh->bounds().valid) {
        Vec3 center = object->transform.world.transformPoint(mesh->bounds().center);
        m_camera.focusOn(center, mesh->bounds().radius);
    } else {
        Vec3 pos = {
            object->transform.world.m[12],
            object->transform.world.m[13],
            object->transform.world.m[14]
        };
        m_camera.focusOn(pos, 1.0f);
    }
}

void Engine::renderPreview() {
    Mesh* mesh = m_resources.getMesh(m_previewMesh);
    if (!mesh) return;

    // lazy init preview target
    if (m_renderer.previewImTextureID() == 0) {
        if (!m_renderer.initPreview(256, 256)) {
            return;
        }
    }

    m_previewOrbit += 0.5f * (float)sapp_frame_duration();

    Vec3 center = mesh->bounds().valid ? mesh->bounds().center : Vec3{ 0, 0, 0 };
    float radius = mesh->bounds().valid ? std::max(mesh->bounds().radius, 0.25f) : 1.0f;
    float dist = std::max(radius * 2.8f, 1.5f);
    float cx = center.x + dist * std::sin(m_previewOrbit);
    float cz = center.z + dist * std::cos(m_previewOrbit);
    Vec3 eye = { cx, center.y + radius * 0.35f, cz };
    Mat4 view = Mat4::lookAt(eye, center, { 0, 1, 0 });
    Mat4 proj = Mat4::perspective(45.0f * 3.14159f / 180.0f,
        (float)m_renderer.previewWidth() / (float)m_renderer.previewHeight(),
        0.1f, 100.0f);

    m_renderer.beginPreviewPass();
    m_renderer.drawMeshPreview(*mesh, Mat4::translate({ 0, 0, 0 }), view, proj);
    m_renderer.endPreviewPass();
}

void Engine::computeMouseRay(float mouseX, float mouseY, Vec3& rayOrigin, Vec3& rayDir) {
    rayOrigin = m_camera.position();

    const Mat4& view = m_camera.viewMatrix();
    Vec3 right = { view.m[0], view.m[4], view.m[8] };
    Vec3 up = { view.m[1], view.m[5], view.m[9] };
    Vec3 forward = { -view.m[2], -view.m[6], -view.m[10] };

    int w = sapp_width(), h = sapp_height();
    if (h == 0) h = 1;
    float aspect = (float)w / (float)h;
    float tanHalfFov = std::tan(m_camera.fovY() * 0.5f);

    float ndcX = (2.0f * mouseX / (float)w - 1.0f);
    float ndcY = (1.0f - 2.0f * mouseY / (float)h);

    float viewX = ndcX * aspect * tanHalfFov;
    float viewY = ndcY * tanHalfFov;

    rayDir = (right * viewX + up * viewY + forward).normalized();
}

bool Engine::screenToPlane(float mouseX, float mouseY, float planeY, Vec3& outPos) {
    Mat4 view, proj;
    Vec3 camPos;
    if (!resolveRenderCamera(view, proj, &camPos)) {
        return false;
    }

    int w = sapp_width();
    int h = sapp_height();
    if (h == 0) h = 1;

    Vec3 forward = { -view.m[2], -view.m[6], -view.m[10] };
    Vec3 right = { view.m[0], view.m[4], view.m[8] };
    Vec3 up = { view.m[1], view.m[5], view.m[9] };

    float tanHalfFov = 1.0f / proj.m[5];
    float aspect = (float)w / (float)h;

    float ndcX = (2.0f * mouseX / (float)w - 1.0f);
    float ndcY = (1.0f - 2.0f * mouseY / (float)h);

    float viewX = ndcX * aspect * tanHalfFov;
    float viewY = ndcY * tanHalfFov;

    Vec3 rayDir = (right * viewX + up * viewY + forward).normalized();

    float denom = rayDir.y;
    if (std::abs(denom) < 0.0001f) return false;

    float t = (planeY - camPos.y) / denom;
    if (t < 0) return false;

    outPos.x = camPos.x + t * rayDir.x;
    outPos.y = planeY;
    outPos.z = camPos.z + t * rayDir.z;
    return true;
}

void Engine::event(const sapp_event* ev) {
    auto setKeyDown = [&](uint32_t key) {
        if (key < 512) {
            if (!m_keyState[key]) {
                m_keyPressed[key] = true;
            }
            m_keyState[key] = true;
        }
    };

    auto setKeyUp = [&](uint32_t key) {
        if (key < 512) {
            if (m_keyState[key]) {
                m_keyReleased[key] = true;
            }
            m_keyState[key] = false;
        }
    };

    if (m_runtimeMode) {
        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN:
                setKeyDown(ev->key_code);
                if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                    sapp_request_quit();
                } else if (!m_useSceneCamera) {
                    m_camera.onKeyDown(ev->key_code);
                }
                break;
            case SAPP_EVENTTYPE_KEY_UP:
                setKeyUp(ev->key_code);
                if (!m_useSceneCamera) {
                    m_camera.onKeyUp(ev->key_code);
                }
                break;
            case SAPP_EVENTTYPE_MOUSE_MOVE:
                m_mouseDeltaX += ev->mouse_dx;
                m_mouseDeltaY += ev->mouse_dy;
                m_mouseX = ev->mouse_x;
                m_mouseY = ev->mouse_y;
                if (!m_useSceneCamera && m_camera.isMouseCaptured()) {
                    m_camera.onMouseMove(ev->mouse_dx, ev->mouse_dy);
                }
                break;
            case SAPP_EVENTTYPE_MOUSE_DOWN: {
                int btn = (int)ev->mouse_button;
                if (btn >= 0 && btn < 3) {
                    m_mouseState[btn] = true;
                    m_mousePressed[btn] = true;
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_UP: {
                int btn = (int)ev->mouse_button;
                if (btn >= 0 && btn < 3) {
                    m_mouseState[btn] = false;
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_SCROLL:
                if (!m_useSceneCamera) {
                    m_camera.onMouseScroll(ev->scroll_y);
                }
                break;
            default:
                break;
        }
        return;
    }

    // track mouse state for scripts during play mode
    m_mouseX = ev->mouse_x;
    m_mouseY = ev->mouse_y;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_MOVE) {
        m_mouseDeltaX += ev->mouse_dx;
        m_mouseDeltaY += ev->mouse_dy;
    }

    if (m_playState == PlayState::Playing) {
        // in play mode: track mouse buttons, handle ESC to stop, skip editor input
        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN:
                setKeyDown(ev->key_code);
                if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                    stop();
                }
                break;
            case SAPP_EVENTTYPE_KEY_UP:
                setKeyUp(ev->key_code);
                break;
            case SAPP_EVENTTYPE_MOUSE_DOWN: {
                int btn = (int)ev->mouse_button;
                if (btn >= 0 && btn < 3) {
                    m_mouseState[btn] = true;
                    m_mousePressed[btn] = true;
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_UP: {
                int btn = (int)ev->mouse_button;
                if (btn >= 0 && btn < 3) {
                    m_mouseState[btn] = false;
                }
                break;
            }
            default:
                break;
        }
        return;
    }

    bool imguiCaptured = m_gui.handleEvent(ev);
    bool inViewport = m_gui.isViewportArea(ev->mouse_x, ev->mouse_y);
    if (m_camera.isMouseCaptured()) {
        imguiCaptured = false;
    }

    switch (ev->type) {
        case SAPP_EVENTTYPE_KEY_DOWN:
            setKeyDown(ev->key_code);
            if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
                m_camera.captureMouse(false);
                sapp_lock_mouse(false);
                if (m_gizmo.activeAxis() != Gizmo::NONE) {
                    m_gizmo.endDrag();
                }
            } else if (!imguiCaptured && ev->key_code == SAPP_KEYCODE_S && (ev->modifiers & SAPP_MODIFIER_CTRL)) {
                saveScene();
            } else if (!imguiCaptured && moveSelectedEntityByKey(ev->key_code, ev->modifiers)) {
                break;
            } else if (!imguiCaptured || m_camera.isMouseCaptured()) {
                m_camera.onKeyDown(ev->key_code);
            }
            break;

        case SAPP_EVENTTYPE_KEY_UP:
            setKeyUp(ev->key_code);
            m_camera.onKeyUp(ev->key_code);
            break;

        case SAPP_EVENTTYPE_MOUSE_DOWN:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT && !m_camera.isMouseCaptured() && !imguiCaptured) {
                if (m_selectedEntity != InvalidSceneEntity) {
                    Vec3 rayOrigin, rayDir;
                    computeMouseRay(ev->mouse_x, ev->mouse_y, rayOrigin, rayDir);
                    const SceneObject* sel = m_scene.getObject(m_selectedEntity);
                    if (sel && sel->active) {
                        Vec3 pos = {
                            sel->transform.world.m[12],
                            sel->transform.world.m[13],
                            sel->transform.world.m[14]
                        };
                        Gizmo::Axis hit = m_gizmo.pick(rayOrigin, rayDir, pos);
                        if (hit != Gizmo::NONE) {
                            m_gizmo.beginDrag(hit, pos, rayOrigin, rayDir);
                            break;
                        }
                    }
                }
                SceneEntity picked = pickSceneEntity(ev->mouse_x, ev->mouse_y);
                if (ev->modifiers & SAPP_MODIFIER_CTRL) {
                    toggleEntitySelection(picked);
                } else {
                    selectEntity(picked);
                }
            } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT && !m_camera.isMouseCaptured() && (!imguiCaptured || inViewport)) {
                m_rmbDown = true;
                m_rmbDownX = ev->mouse_x;
                m_rmbDownY = ev->mouse_y;
            }
            break;

        case SAPP_EVENTTYPE_MOUSE_UP:
            if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT && m_gizmo.activeAxis() != Gizmo::NONE) {
                m_gizmo.endDrag();
            } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
                if (m_camera.isMouseCaptured()) {
                    m_camera.captureMouse(false);
                    sapp_lock_mouse(false);
                } else if (m_rmbDown && inViewport) {
                    m_showViewportMenu = true;
                }
                m_rmbDown = false;
            }
            break;

        case SAPP_EVENTTYPE_MOUSE_MOVE:
            if (!m_camera.isMouseCaptured() && m_gizmo.activeAxis() != Gizmo::NONE && m_selectedEntity != InvalidSceneEntity) {
                Vec3 rayOrigin, rayDir;
                computeMouseRay(ev->mouse_x, ev->mouse_y, rayOrigin, rayDir);
                SceneObject* sel = m_scene.getObject(m_selectedEntity);
                if (sel) {
                    if (m_gizmo.activeMode() == Gizmo::ROTATE) {
                        float delta = m_gizmo.dragRotation(rayOrigin, rayDir);
                        switch (m_gizmo.activeAxis()) {
                            case Gizmo::X_AXIS:
                                sel->transform.local.rotation.x += delta;
                                break;
                            case Gizmo::Y_AXIS:
                                sel->transform.local.rotation.y += delta;
                                break;
                            case Gizmo::Z_AXIS:
                                sel->transform.local.rotation.z += delta;
                                break;
                            default:
                                break;
                        }
                        m_gizmo.beginDrag(m_gizmo.activeAxis(), {
                            sel->transform.world.m[12],
                            sel->transform.world.m[13],
                            sel->transform.world.m[14]
                        }, rayOrigin, rayDir);
                    } else {
                        Vec3 newPos = m_gizmo.drag(rayOrigin, rayDir);
                        setEntityPosition(m_selectedEntity, newPos);
                    }
                }
            } else if (!m_camera.isMouseCaptured() && !imguiCaptured && m_selectedEntity != InvalidSceneEntity) {
                Vec3 rayOrigin, rayDir;
                computeMouseRay(ev->mouse_x, ev->mouse_y, rayOrigin, rayDir);
                const SceneObject* sel = m_scene.getObject(m_selectedEntity);
                if (sel && sel->active) {
                    Vec3 pos = {
                        sel->transform.world.m[12],
                        sel->transform.world.m[13],
                        sel->transform.world.m[14]
                    };
                    Gizmo::Axis hover = m_gizmo.pick(rayOrigin, rayDir, pos);
                    m_gizmo.setHover(m_gizmo.hoverMode(), hover);
                }
            }
            if (m_rmbDown && !m_camera.isMouseCaptured()) {
                float dx = ev->mouse_x - m_rmbDownX;
                float dy = ev->mouse_y - m_rmbDownY;
                if (dx * dx + dy * dy > 16.0f) {
                    m_camera.captureMouse(true);
                    sapp_lock_mouse(true);
                }
            }
            if (m_camera.isMouseCaptured()) {
                m_camera.onMouseMove(ev->mouse_dx, ev->mouse_dy);
            }
            break;

        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            if (!imguiCaptured || m_camera.isMouseCaptured()) {
                m_camera.onMouseScroll(ev->scroll_y);
            }
            break;

        default:
            break;
    }
}

SceneEntity Engine::createPrimitive(const std::string& type) {
    MeshHandle meshHandle = 0;
    Vec3 halfExtent{ 0.5f, 0.5f, 0.5f };
    float radius = 0.5f;
    float halfHeight = 0.5f;
    Vec3 spawnPosition{ 0.0f, 0.5f, 0.0f };
    std::string name;
    uint32_t bodyId = PhysicsWorld::InvalidBodyId;

    if (type == "Box") {
        meshHandle = m_resources.createBoxMesh(halfExtent);
        name = "Box";
        spawnPosition.y = halfExtent.y + 0.01f;
        bodyId = m_physics.addBox(spawnPosition, halfExtent, true);
    } else if (type == "Sphere") {
        meshHandle = m_resources.createSphereMesh(radius);
        name = "Sphere";
        spawnPosition.y = radius + 0.01f;
        bodyId = m_physics.addSphere(spawnPosition, radius, true);
    } else if (type == "Capsule") {
        meshHandle = m_resources.createCapsuleMesh(halfHeight, radius, 16);
        name = "Capsule";
        spawnPosition.y = halfHeight + radius + 0.01f;
        bodyId = m_physics.addCapsule(spawnPosition, halfHeight, radius, true);
    } else if (type == "Cylinder") {
        meshHandle = m_resources.createCylinderMesh(halfHeight, radius, 24);
        name = "Cylinder";
        spawnPosition.y = halfHeight + 0.01f;
        bodyId = m_physics.addCylinder(spawnPosition, halfHeight, radius, true);
    }

    if (meshHandle == 0) {
        if (bodyId != PhysicsWorld::InvalidBodyId) {
            m_physics.removeBody(bodyId);
        }
        return InvalidSceneEntity;
    }

    SceneEntity entity = m_scene.createMeshObject(meshHandle, name);
    setEntityPosition(entity, spawnPosition);
    if (bodyId != PhysicsWorld::InvalidBodyId) {
        if (RigidBodyComponent* rigidBody = m_scene.addRigidBody(entity, bodyId)) {
            rigidBody->dynamic = true;
            if (type == "Sphere") {
                rigidBody->shape = RigidBodyComponent::Shape::Sphere;
                rigidBody->radius = radius;
            } else if (type == "Capsule") {
                rigidBody->shape = RigidBodyComponent::Shape::Capsule;
                rigidBody->halfHeight = halfHeight;
                rigidBody->radius = radius;
            } else if (type == "Cylinder") {
                rigidBody->shape = RigidBodyComponent::Shape::Cylinder;
                rigidBody->halfHeight = halfHeight;
                rigidBody->radius = radius;
            } else {
                rigidBody->shape = RigidBodyComponent::Shape::Box;
                rigidBody->halfExtent = halfExtent;
            }
        }
    }
    selectEntity(entity);
    focusOnEntity(entity);
    return entity;
}

bool Engine::createProject(const std::string& rootPath, const std::string& projectName) {
    m_lastProjectError.clear();
    if (rootPath.empty() || projectName.empty()) {
        m_lastProjectError = "Project path and name are required.";
        return false;
    }

    fs::path projectDir = fs::path(rootPath) / projectName;
    std::error_code ec;
    fs::create_directories(projectDir / "Assets", ec);
    fs::create_directories(projectDir / "Scripts", ec);
    fs::create_directories(projectDir / "Scenes", ec);
    fs::create_directories(projectDir / "Build", ec);
    if (ec) {
        m_lastProjectError = "Failed to create project folders.";
        return false;
    }

    clearSceneObjects();
    createDefaultScene();
    m_projectPath = fs::absolute(projectDir).lexically_normal().string();
    m_currentScenePath = (fs::path(m_projectPath) / "Scenes" / "Main.rcscene").string();
    return saveScene(m_currentScenePath);
}

void Engine::newScene() {
    shutdownSceneScripts();
    m_playSnapshot.clear();
    m_playState = PlayState::Stopped;
    clearSceneObjects();
    createDefaultScene();
    m_currentScenePath.clear();
    m_sceneScriptsInitialized = false;
    m_pendingScenePath.clear();
}

bool Engine::saveScene(const std::string& path) {
    std::string savePath = path.empty() ? m_currentScenePath : path;
    if (savePath.empty()) {
        if (!hasProject()) {
            m_lastProjectError = "Create or load a project before saving.";
            return false;
        }
        savePath = (fs::path(m_projectPath) / "Scenes" / "Main.rcscene").string();
    }

    std::error_code ec;
    fs::create_directories(fs::path(savePath).parent_path(), ec);

    std::ofstream out(savePath, std::ios::binary);
    if (!out) {
        m_lastProjectError = "Failed to open scene for writing.";
        return false;
    }

    out << "RealCoreScene 1\n";
    out << "Project " << std::quoted(m_projectPath) << "\n";

    for (const auto& object : m_scene.objects()) {
        if (!object.active) {
            continue;
        }

        out << "Object " << std::quoted(object.name) << "\n";
        out << "Transform "
            << object.transform.local.position.x << " " << object.transform.local.position.y << " " << object.transform.local.position.z << " "
            << object.transform.local.rotation.x << " " << object.transform.local.rotation.y << " " << object.transform.local.rotation.z << " "
            << object.transform.local.scale.x << " " << object.transform.local.scale.y << " " << object.transform.local.scale.z << "\n";

        if (object.hasMeshRenderer) {
            std::string kind = m_resources.meshAssetKind(object.meshRenderer.meshHandle);
            std::string source = m_resources.meshSourcePath(object.meshRenderer.meshHandle);
            out << "Mesh " << std::quoted(kind) << " " << std::quoted(source) << " " << object.meshRenderer.visible << "\n";
        }
        if (object.hasRigidBody) {
            writeRigidBodyLine(out, object.rigidBody);
        }
        if (object.hasLight) {
            out << "Light "
                << object.light.direction.x << " " << object.light.direction.y << " " << object.light.direction.z << " "
                << object.light.color.x << " " << object.light.color.y << " " << object.light.color.z << " "
                << object.light.intensity << " " << object.light.ambient << " " << object.light.enabled << "\n";
        }
        if (object.hasCamera) {
            out << "Camera "
                << object.camera.fovY << " " << object.camera.zNear << " " << object.camera.zFar << " "
                << object.camera.primary << " " << object.camera.enabled << "\n";
        }
        if (object.hasScript) {
            out << "Script " << std::quoted(object.script.scriptPath) << "\n";
        }
        out << "EndObject\n";
    }

    if (!out.good()) {
        m_lastProjectError = "Failed while writing scene.";
        return false;
    }

    m_currentScenePath = fs::absolute(savePath).lexically_normal().string();
    if (m_projectPath.empty()) {
        fs::path scenePath(m_currentScenePath);
        if (scenePath.parent_path().filename() == "Scenes") {
            m_projectPath = scenePath.parent_path().parent_path().string();
        }
    }
    return true;
}

bool Engine::loadScene(const std::string& path) {
    m_lastProjectError.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        m_lastProjectError = "Failed to open scene.";
        return false;
    }

    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (magic != "RealCoreScene" || version != 1) {
        m_lastProjectError = "Unsupported scene format.";
        return false;
    }

    shutdownSceneScripts();
    clearSceneObjects();

    std::string token;
    SceneObject* current = nullptr;
    while (in >> token) {
        if (token == "Project") {
            in >> std::quoted(m_projectPath);
        } else if (token == "Object") {
            std::string name;
            in >> std::quoted(name);
            SceneEntity entity = m_scene.createObject(name);
            current = m_scene.getObject(entity);
        } else if (token == "Transform" && current) {
            in >> current->transform.local.position.x >> current->transform.local.position.y >> current->transform.local.position.z
               >> current->transform.local.rotation.x >> current->transform.local.rotation.y >> current->transform.local.rotation.z
               >> current->transform.local.scale.x >> current->transform.local.scale.y >> current->transform.local.scale.z;
        } else if (token == "Mesh" && current) {
            std::string kind;
            std::string source;
            bool visible = true;
            in >> std::quoted(kind) >> std::quoted(source) >> visible;

            MeshHandle handle = 0;
            if (kind == "primitive:Box") {
                handle = m_resources.createBoxMesh({ 0.5f, 0.5f, 0.5f });
                current->rigidBody.bodyId = m_physics.addBox(current->transform.local.position, { 0.5f, 0.5f, 0.5f }, true);
                current->rigidBody.syncTransform = true;
                current->rigidBody.shape = RigidBodyComponent::Shape::Box;
                current->rigidBody.halfExtent = { 0.5f, 0.5f, 0.5f };
                current->rigidBody.dynamic = true;
                current->hasRigidBody = current->rigidBody.bodyId != PhysicsWorld::InvalidBodyId;
            } else if (kind == "primitive:Sphere") {
                handle = m_resources.createSphereMesh(0.5f);
                current->rigidBody.bodyId = m_physics.addSphere(current->transform.local.position, 0.5f, true);
                current->rigidBody.syncTransform = true;
                current->rigidBody.shape = RigidBodyComponent::Shape::Sphere;
                current->rigidBody.radius = 0.5f;
                current->rigidBody.dynamic = true;
                current->hasRigidBody = current->rigidBody.bodyId != PhysicsWorld::InvalidBodyId;
            } else if (kind == "primitive:Capsule") {
                handle = m_resources.createCapsuleMesh(0.5f, 0.5f, 16);
                current->rigidBody.bodyId = m_physics.addCapsule(current->transform.local.position, 0.5f, 0.5f, true);
                current->rigidBody.syncTransform = true;
                current->rigidBody.shape = RigidBodyComponent::Shape::Capsule;
                current->rigidBody.halfHeight = 0.5f;
                current->rigidBody.radius = 0.5f;
                current->rigidBody.dynamic = true;
                current->hasRigidBody = current->rigidBody.bodyId != PhysicsWorld::InvalidBodyId;
            } else if (kind == "primitive:Cylinder") {
                handle = m_resources.createCylinderMesh(0.5f, 0.5f, 24);
                current->rigidBody.bodyId = m_physics.addCylinder(current->transform.local.position, 0.5f, 0.5f, true);
                current->rigidBody.syncTransform = true;
                current->rigidBody.shape = RigidBodyComponent::Shape::Cylinder;
                current->rigidBody.halfHeight = 0.5f;
                current->rigidBody.radius = 0.5f;
                current->rigidBody.dynamic = true;
                current->hasRigidBody = current->rigidBody.bodyId != PhysicsWorld::InvalidBodyId;
            } else if (!source.empty()) {
                handle = m_resources.loadMesh(resolveProjectPath(source));
            }

            if (handle != 0) {
                current->meshRenderer.meshHandle = handle;
                current->meshRenderer.visible = visible;
                current->hasMeshRenderer = true;
            }
        } else if (token == "RigidBody" && current) {
            std::string line;
            std::getline(in >> std::ws, line);
            std::istringstream rbIn(line);

            RigidBodyComponent settings = current->hasRigidBody ? current->rigidBody : RigidBodyComponent{};
            rbIn >> settings.syncTransform >> settings.frozen;

            std::string shapeName;
            if (rbIn >> shapeName) {
                settings.shape = parseRigidBodyShape(shapeName);
                rbIn >> settings.dynamic
                     >> settings.halfExtent.x >> settings.halfExtent.y >> settings.halfExtent.z
                     >> settings.radius >> settings.halfHeight;
            }

            if (current->hasRigidBody && current->rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
                m_physics.removeBody(current->rigidBody.bodyId);
            }
            m_scene.removeRigidBody(current->id);

            settings.bodyId = createRigidBodyForComponent(m_physics, current->transform.local, settings);
            if (settings.bodyId != PhysicsWorld::InvalidBodyId) {
                RigidBodyComponent* rigidBody = m_scene.addRigidBody(current->id, settings.bodyId);
                if (rigidBody) {
                    *rigidBody = settings;
                    rigidBody->bodyId = settings.bodyId;
                    current->hasRigidBody = true;
                    m_physics.setFrozen(rigidBody->bodyId, rigidBody->frozen);
                    m_physics.setPosition(rigidBody->bodyId, current->transform.local.position);
                }
            }
        } else if (token == "Light" && current) {
            LightComponent* light = m_scene.addLight(current->id);
            if (light) {
                in >> light->direction.x >> light->direction.y >> light->direction.z
                   >> light->color.x >> light->color.y >> light->color.z
                   >> light->intensity >> light->ambient >> light->enabled;
            }
        } else if (token == "Camera" && current) {
            CameraComponent* camera = m_scene.addCamera(current->id);
            if (camera) {
                in >> camera->fovY >> camera->zNear >> camera->zFar
                   >> camera->primary >> camera->enabled;
            }
        } else if (token == "Script" && current) {
            std::string scriptPath;
            in >> std::quoted(scriptPath);
            ScriptComponent* script = m_scene.addScript(current->id, {});
            if (script) {
                script->scriptPath = scriptPath;
                script->initialized = false;
            }
        } else if (token == "EndObject") {
            current = nullptr;
        }
    }

    m_scene.updateWorldTransforms();
    m_currentScenePath = fs::absolute(path).lexically_normal().string();
    if (m_projectPath.empty()) {
        fs::path scenePath(m_currentScenePath);
        if (scenePath.parent_path().filename() == "Scenes") {
            m_projectPath = scenePath.parent_path().parent_path().string();
        }
    }
    selectEntity(InvalidSceneEntity);
    loadSceneScripts();
    return true;
}

bool Engine::exportGame(std::string& exportedExePath) {
    exportedExePath.clear();
    m_lastProjectError.clear();

    if (!hasProject()) {
        if (m_currentScenePath.empty()) {
            m_lastProjectError = "Create or load a project before exporting.";
            return false;
        }

        fs::path scenePath(m_currentScenePath);
        if (scenePath.parent_path().filename() == "Scenes") {
            m_projectPath = scenePath.parent_path().parent_path().string();
        }
    }

    if (m_projectPath.empty()) {
        m_lastProjectError = "Export needs a project folder.";
        return false;
    }

    if (!saveScene()) {
        if (m_lastProjectError.empty()) {
            m_lastProjectError = "Failed to save scene before export.";
        }
        return false;
    }

    std::error_code ec;
    fs::path projectRoot = fs::absolute(fs::path(m_projectPath), ec).lexically_normal();
    if (ec) {
        m_lastProjectError = "Invalid project path.";
        return false;
    }

    std::string projectName = sanitizeFileStem(projectRoot.filename().string());
    fs::path exportRoot = projectRoot / "Build" / "Windows";
    fs::create_directories(exportRoot, ec);
    if (ec) {
        m_lastProjectError = "Failed to create export folder.";
        return false;
    }

    fs::path sourceExe = currentExecutablePath();
    fs::path targetExe = exportRoot / (projectName + ".exe");
    fs::copy_file(sourceExe, targetExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        m_lastProjectError = "Failed to copy game executable.";
        return false;
    }

    fs::path runtimeDir = sourceExe.parent_path();
    for (const auto& entry : fs::directory_iterator(runtimeDir, ec)) {
        if (ec) {
            m_lastProjectError = "Failed to read runtime folder.";
            return false;
        }
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".dll") {
            fs::copy_file(entry.path(), exportRoot / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                m_lastProjectError = "Failed to copy runtime DLLs.";
                return false;
            }
        }
    }
    ec.clear();

    if (!copyDirectoryTree(projectRoot / "Assets", exportRoot / "Assets", ec)) {
        m_lastProjectError = "Failed to copy Assets folder.";
        return false;
    }
    if (!copyDirectoryTree(projectRoot / "Scripts", exportRoot / "Scripts", ec)) {
        m_lastProjectError = "Failed to copy Scripts folder.";
        return false;
    }
    if (!copyDirectoryTree(projectRoot / "Scenes", exportRoot / "Scenes", ec)) {
        m_lastProjectError = "Failed to copy Scenes folder.";
        return false;
    }
    fs::create_directories(exportRoot / "Scenes", ec);
    if (ec) {
        m_lastProjectError = "Failed to create exported Scenes folder.";
        return false;
    }

    fs::path exportedScene = exportRoot / "Scenes" / "Main.rcscene";
    std::ofstream out(exportedScene, std::ios::binary);
    if (!out) {
        m_lastProjectError = "Failed to write exported scene.";
        return false;
    }

    out << "RealCoreScene 1\n";
    out << "Project " << std::quoted(std::string(".")) << "\n";

    for (const auto& object : m_scene.objects()) {
        if (!object.active) {
            continue;
        }

        out << "Object " << std::quoted(object.name) << "\n";
        out << "Transform "
            << object.transform.local.position.x << " " << object.transform.local.position.y << " " << object.transform.local.position.z << " "
            << object.transform.local.rotation.x << " " << object.transform.local.rotation.y << " " << object.transform.local.rotation.z << " "
            << object.transform.local.scale.x << " " << object.transform.local.scale.y << " " << object.transform.local.scale.z << "\n";

        if (object.hasMeshRenderer) {
            std::string kind = m_resources.meshAssetKind(object.meshRenderer.meshHandle);
            std::string source = m_resources.meshSourcePath(object.meshRenderer.meshHandle);
            if (!source.empty()) {
                source = remapAssetForExport(source, projectRoot, exportRoot, ec);
                if (ec) {
                    m_lastProjectError = "Failed to remap exported asset paths.";
                    return false;
                }
            }
            out << "Mesh " << std::quoted(kind) << " " << std::quoted(source) << " " << object.meshRenderer.visible << "\n";
        }
        if (object.hasRigidBody) {
            writeRigidBodyLine(out, object.rigidBody);
        }
        if (object.hasLight) {
            out << "Light "
                << object.light.direction.x << " " << object.light.direction.y << " " << object.light.direction.z << " "
                << object.light.color.x << " " << object.light.color.y << " " << object.light.color.z << " "
                << object.light.intensity << " " << object.light.ambient << " " << object.light.enabled << "\n";
        }
        if (object.hasCamera) {
            out << "Camera "
                << object.camera.fovY << " " << object.camera.zNear << " " << object.camera.zFar << " "
                << object.camera.primary << " " << object.camera.enabled << "\n";
        }
        if (object.hasScript) {
            out << "Script " << std::quoted(object.script.scriptPath) << "\n";
        }
        out << "EndObject\n";
    }

    if (!out.good()) {
        m_lastProjectError = "Failed while writing exported scene.";
        return false;
    }

    std::ofstream manifest(exportRoot / "realcore.game", std::ios::binary);
    if (!manifest) {
        m_lastProjectError = "Failed to write game manifest.";
        return false;
    }
    manifest << "RealCoreGame 1\n";
    manifest << "Title " << std::quoted(projectName) << "\n";
    manifest << "Scene " << std::quoted(std::string("Scenes/Main.rcscene")) << "\n";

    if (!manifest.good()) {
        m_lastProjectError = "Failed while writing game manifest.";
        return false;
    }

    exportedExePath = fs::absolute(targetExe, ec).lexically_normal().string();
    if (ec) {
        exportedExePath = targetExe.string();
    }
    return true;
}

void Engine::shutdown() {
    if (m_groundBodyId != PhysicsWorld::InvalidBodyId) {
        m_physics.removeBody(m_groundBodyId);
        m_groundBodyId = PhysicsWorld::InvalidBodyId;
    }
    clearSceneObjects();
    m_resources.shutdown();
    m_renderer.shutdown();
    m_gizmo.shutdown();
    m_physics.shutdown();
    m_audio.shutdown();
    m_scriptEngine.shutdown();
    m_luaScriptEngine.shutdown();
    if (m_guiInitialized) {
        m_gui.shutdown();
        m_guiInitialized = false;
    }
    sg_shutdown();
    m_running = false;
}
