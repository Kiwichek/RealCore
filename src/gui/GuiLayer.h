#pragma once
#include <ai/OllamaClient.h>
#include <imgui.h>
#include <gui/FileBrowser.h>
#include <cstdint>
#include <string>
#include <vector>

class GuiLayer {
public:
    GuiLayer() = default;
    GuiLayer(const GuiLayer&) = delete;
    GuiLayer& operator=(const GuiLayer&) = delete;

    bool init();
    void newFrame();
    void render();
    bool handleEvent(const void* ev);
    void shutdown();
    bool isViewportArea(float x, float y) const;

    FileBrowser& fileBrowser() { return m_fileBrowser; }

private:
    void showMainMenu();
    void showStartupDialog();
    void showSceneHierarchy();
    void showInspector();
    void showInfoPanel();
    void showContentBrowser();
    void showCodeEditor();
    void showScriptEditorWindow();
    void showAiAssistant(bool embedded);
    void showAiAssistantWindow();
    void showDebugWindow();
    void recordFrameStats(float deltaTime);
    bool openTextEditor(const std::string& path);
    bool saveCodeEditor();
    std::string currentCodeText() const;
    void appendToCodeEditor(const std::string& text);
    bool applyCodeSuggestionToEditor(const std::string& response, std::string& status);
    std::string buildAiEngineContext() const;
    bool applyAiActions(const std::string& response, std::string& status);

    enum class DebugMode {
        Compact,
        Performance,
        Engine
    };

    FileBrowser m_fileBrowser;
    bool m_showDemo = false;
    bool m_showSceneHierarchy = true;
    bool m_showInspector = true;
    bool m_showContentBrowser = true;
    bool m_showDebugWindow = false;
    DebugMode m_debugMode = DebugMode::Compact;
    std::string m_statusMessage;

    struct LoadedAsset {
        std::string name;
        uint32_t meshHandle = 0;
    };
    std::vector<LoadedAsset> m_loadedAssets;
    std::string m_lastPreviewPath;
    bool m_showStartupDialog = true;
    bool m_showSaveAsDialog = false;
    bool m_showLoadSceneDialog = false;
    char m_projectRootBuf[1024] = {};
    char m_projectNameBuf[128] = {};
    char m_scenePathBuf[1024] = {};
    char m_saveScenePathBuf[1024] = {};
    char m_scriptAttachPathBuf[1024] = {};
    char m_hierarchyRenameBuf[256] = {};
    uint32_t m_hierarchyRenameEntity = 0;
    bool m_showHierarchyRename = false;
    bool m_openHierarchyRenamePopup = false;

    // panel widths and heights for splitter layout
    float m_leftPanelWidth = 250.0f;
    float m_rightPanelWidth = 280.0f;
    float m_bottomPanelHeight = 280.0f;

    // script editor state
    bool m_showCodeEditor = false;
    bool m_codeDirty = false;
    std::string m_codePath;
    std::string m_codeName;
    std::vector<char> m_codeBuffer;
    std::string m_codeStatus;
    ImVec2 m_scriptEditorPos = ImVec2(100.0f, 100.0f);
    ImVec2 m_scriptEditorSize = ImVec2(600.0f, 400.0f);

    // AI assistant state
    bool m_showAiAssistant = false;
    bool m_showAiWindow = false;
    bool m_aiEngineMode = true;
    char m_ollamaBaseUrl[256] = {};
    char m_aiPromptBuf[2048] = {};
    std::vector<OllamaModel> m_ollamaModels;
    int m_ollamaModelIndex = -1;
    std::string m_aiResponse;
    std::string m_aiStatus;

    // debug metrics
    std::vector<float> m_frameTimeSamples;
    size_t m_frameTimeSampleIndex = 0;
    float m_lastFrameMs = 0.0f;
    float m_avgFrameMs = 0.0f;
    float m_minFrameMs = 0.0f;
    float m_maxFrameMs = 0.0f;
};
