#pragma once
#include <sokol_app.h>
#include <graphics/renderer.h>
#include <graphics/Gizmo.h>
#include <resources/ResourceManager.h>
#include <core/Camera.h>
#include <physics/PhysicsWorld.h>
#include <gui/GuiLayer.h>
#include <audio/AudioEngine.h>
#include <script/ScriptEngine.h>
#include <script/LuaScriptEngine.h>
#include <scene/Scene.h>
#include <string>
#include <vector>

class Engine {
public:
    enum class PlayState {
        Stopped,
        Playing,
        Paused
    };

    static Engine& instance();

    bool init();
    void frame();
    void event(const sapp_event* ev);
    void shutdown();
    void configureLaunch(int argc, char* argv[]);

    Renderer& renderer() { return m_renderer; }
    ResourceManager& resources() { return m_resources; }
    Camera& camera() { return m_camera; }
    PhysicsWorld& physics() { return m_physics; }
    GuiLayer& gui() { return m_gui; }
    AudioEngine& audio() { return m_audio; }
    ScriptEngine& scriptEngine() { return m_scriptEngine; }
    LuaScriptEngine& luaScriptEngine() { return m_luaScriptEngine; }
    Scene& scene() { return m_scene; }
    bool useSceneCamera() const { return m_useSceneCamera; }
    void setUseSceneCamera(bool enabled) { m_useSceneCamera = enabled; }
    SceneEntity selectedEntity() const { return m_selectedEntity; }
    const std::vector<SceneEntity>& selectedEntities() const { return m_selectedEntities; }
    bool isEntitySelected(SceneEntity entity) const;
    size_t selectedEntityCount() const { return m_selectedEntities.size(); }
    void selectEntity(SceneEntity entity);
    void toggleEntitySelection(SceneEntity entity);

    SceneEntity addRenderItem(MeshHandle handle, const std::string& name = {});
    SceneEntity importRenderItem(const std::string& path);
    SceneEntity createPrimitive(const std::string& type);
    void focusOnItem(MeshHandle handle);
    void focusOnEntity(SceneEntity entity);
    void setEntityPosition(SceneEntity entity, const Vec3& position);
    void deleteEntity(SceneEntity entity);
    void deleteSelectedEntity();
    void play();
    void pause();
    void stop();
    PlayState playState() const { return m_playState; }
    bool isPlaying() const { return m_playState == PlayState::Playing; }
    bool createProject(const std::string& rootPath, const std::string& projectName);
    void newScene();
    bool saveScene(const std::string& path = {});
    bool loadScene(const std::string& path);
    bool attachScript(SceneEntity entity, const std::string& scriptPath);
    void detachScript(SceneEntity entity);
    void requestSceneLoad(const std::string& path);
    void requestQuit();
    bool exportGame(std::string& exportedExePath);
    bool hasProject() const { return !m_projectPath.empty(); }
    const std::string& projectPath() const { return m_projectPath; }
    const std::string& currentScenePath() const { return m_currentScenePath; }
    const std::string& lastProjectError() const { return m_lastProjectError; }
    bool runtimeMode() const { return m_runtimeMode; }
    const std::string& windowTitle() const { return m_windowTitle; }

    // mouse state for scripting
    float mouseX() const { return m_mouseX; }
    float mouseY() const { return m_mouseY; }
    float mouseDeltaX() const { return m_mouseDeltaX; }
    float mouseDeltaY() const { return m_mouseDeltaY; }
    bool isMouseDown(int btn) const { return btn >= 0 && btn < 3 && m_mouseState[btn]; }
    bool isMousePressed(int btn) const { return btn >= 0 && btn < 3 && m_mousePressed[btn]; }
    bool isKeyDown(int key) const { return key >= 0 && key < 512 && m_keyState[key]; }
    bool isKeyPressed(int key) const { return key >= 0 && key < 512 && m_keyPressed[key]; }
    bool isKeyReleased(int key) const { return key >= 0 && key < 512 && m_keyReleased[key]; }
    void setMouseVisible(bool visible);
    void lockMouse(bool lock);

    // screen to world helpers
    bool screenToPlane(float mouseX, float mouseY, float planeY, Vec3& outPos);

    // preview
    void setPreviewMesh(MeshHandle handle) { m_previewMesh = handle; }
    MeshHandle previewMesh() const { return m_previewMesh; }
    void renderPreview();
    bool consumeViewportMenu() { bool v = m_showViewportMenu; m_showViewportMenu = false; return v; }

private:
    Engine() = default;
    ~Engine() = default;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void renderScene();
    void renderShadowMap();
    bool resolveRenderCamera(Mat4& view, Mat4& proj, Vec3* cameraPosition = nullptr);
    void initRuntimeScene();
    void loadSceneScripts();
    void initSceneScripts();
    void updateSceneScripts(float deltaTime);
    void shutdownSceneScripts();
    void processPendingSceneLoad();
    std::string resolveProjectPath(const std::string& path) const;
    std::string resolveScenePath(const std::string& path) const;
    std::string makeProjectRelativePath(const std::string& path) const;
    SceneEntity pickSceneEntity(float mouseX, float mouseY);
    bool moveSelectedEntityByKey(uint32_t key, uint32_t modifiers);
    void computeMouseRay(float mouseX, float mouseY, Vec3& rayOrigin, Vec3& rayDir);
    void capturePlaySnapshot();
    void restorePlaySnapshot();
    void createDefaultScene();
    void clearSceneObjects();

    struct TransformSnapshot {
        SceneEntity entity = InvalidSceneEntity;
        Transform transform;
    };

    Renderer m_renderer;
    Gizmo m_gizmo;
    ResourceManager m_resources;
    Camera m_camera;
    PhysicsWorld m_physics;
    GuiLayer m_gui;
    AudioEngine m_audio;
    ScriptEngine m_scriptEngine;
    LuaScriptEngine m_luaScriptEngine;
    Scene m_scene;
    bool m_running = false;
    bool m_useSceneCamera = false;
    SceneEntity m_selectedEntity = InvalidSceneEntity;
    std::vector<SceneEntity> m_selectedEntities;
    bool m_showViewportMenu = false;
    bool m_rmbDown = false;
    float m_rmbDownX = 0, m_rmbDownY = 0;
    uint32_t m_groundBodyId = PhysicsWorld::InvalidBodyId;
    PlayState m_playState = PlayState::Stopped;
    std::vector<TransformSnapshot> m_playSnapshot;
    std::string m_projectPath;
    std::string m_currentScenePath;
    std::string m_lastProjectError;
    bool m_runtimeMode = false;
    bool m_guiInitialized = false;
    bool m_sceneScriptsInitialized = false;
    std::string m_launchScenePath;
    std::string m_pendingScenePath;
    std::string m_windowTitle = "RealCore Engine";

    float m_mouseX = 0, m_mouseY = 0;
    float m_mouseDeltaX = 0, m_mouseDeltaY = 0;
    bool m_mouseState[3] = { false, false, false };
    bool m_mousePressed[3] = { false, false, false };
    bool m_keyState[512] = {};
    bool m_keyPressed[512] = {};
    bool m_keyReleased[512] = {};

    MeshHandle m_previewMesh = 0;
    float m_previewOrbit = 0;
};
