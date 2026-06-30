#include <gui/GuiLayer.h>
#include <core/engine.h>
#include <platform/NativeDialogs.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_imgui.h>
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace fs = std::filesystem;

namespace {

enum class TransportIcon {
    Play,
    Pause,
    Stop
};

bool transportButton(const char* id, TransportIcon icon, ImVec2 size) {
    if (size.y <= 0.0f) {
        size.y = ImGui::GetFrameHeight();
    }

    bool pressed = ImGui::InvisibleButton(id, size);
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    ImU32 bgColor = IM_COL32(0.18f * 255, 0.18f * 255, 0.18f * 255, 255);
    ImU32 hoverColor = IM_COL32(0.25f * 255, 0.25f * 255, 0.25f * 255, 255);
    ImU32 accentColor = IM_COL32(0.2275f * 255, 0.4784f * 255, 0.9961f * 255, 255);
    ImU32 playColor = IM_COL32(0.2f * 255, 0.7f * 255, 0.3f * 255, 255);

    ImU32 frameColor = isActive ? accentColor : (isHovered ? hoverColor : bgColor);
    ImU32 iconColor = isActive ? IM_COL32(255, 255, 255, 255) : (icon == TransportIcon::Play ? playColor : IM_COL32(220, 220, 220, 255));

    float rounding = 4.0f;
    drawList->AddRectFilled(min, max, frameColor, rounding);
    drawList->AddRect(min, max, IM_COL32(60, 60, 60, 255), rounding);

    float w = max.x - min.x;
    float h = max.y - min.y;
    float cx = min.x + w * 0.5f;
    float cy = min.y + h * 0.5f;

    if (icon == TransportIcon::Play) {
        float r = h * 0.26f;
        drawList->AddTriangleFilled(
            ImVec2(cx - r * 0.65f, cy - r),
            ImVec2(cx - r * 0.65f, cy + r),
            ImVec2(cx + r * 0.95f, cy),
            iconColor);
    } else if (icon == TransportIcon::Pause) {
        float barW = h * 0.12f;
        float barH = h * 0.48f;
        float gap = h * 0.10f;
        drawList->AddRectFilled(ImVec2(cx - gap - barW, cy - barH * 0.5f), ImVec2(cx - gap, cy + barH * 0.5f), iconColor, 1.0f);
        drawList->AddRectFilled(ImVec2(cx + gap, cy - barH * 0.5f), ImVec2(cx + gap + barW, cy + barH * 0.5f), iconColor, 1.0f);
    } else {
        float r = h * 0.22f;
        drawList->AddRectFilled(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), iconColor, 1.0f);
    }

    return pressed;
}

static void setupImGuiStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.WindowBorderSize = 0.5f;
    style.FrameBorderSize = 0.5f;
    style.ChildBorderSize = 0.5f;
    style.PopupBorderSize = 0.5f;
    style.TabBorderSize = 0.0f;
    style.ScrollbarSize = 8.0f;
    style.WindowMinSize = ImVec2(100, 100);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 16.0f;
    style.SeparatorTextBorderSize = 1.0f;

    colors[ImGuiCol_Text]                 = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]             = ImVec4(0.1451f, 0.1451f, 0.1490f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.1176f, 0.1176f, 0.1176f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.1451f, 0.1451f, 0.1490f, 1.00f);
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.2275f, 0.4784f, 0.9961f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.2275f, 0.4784f, 0.9961f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.30f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.2275f, 0.4784f, 0.9961f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_PlotLines]            = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    colors[ImGuiCol_PlotHistogram]        = ImVec4(0.2275f, 0.4784f, 0.9961f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.30f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.2275f, 0.4784f, 0.9961f, 0.40f);
    colors[ImGuiCol_NavHighlight]         = ImVec4(0.2275f, 0.4784f, 0.9961f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]= ImVec4(0.70f, 0.70f, 0.70f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
}

static void setupImGuiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();

    ImFont* font = nullptr;
    const char* fontPaths[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };

    for (const char* path : fontPaths) {
        if (!fs::exists(path)) {
            continue;
        }
        font = io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr, ranges);
        if (font) {
            break;
        }
    }

    if (!font) {
        font = io.Fonts->AddFontDefault();
    }
    io.FontDefault = font;
}

static bool drawSplitter(const char* id, ImVec2 pos, ImVec2 size, ImGuiMouseCursor cursor) {
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(id, nullptr, flags);
    ImGui::PopStyleVar();

    ImGui::InvisibleButton("##splitter-handle", size);
    bool active = ImGui::IsItemActive();
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(cursor);
    }
    ImGui::End();
    return active;
}

} // namespace

bool GuiLayer::init() {
    simgui_desc_t desc = {};
    desc.ini_filename = "imgui.ini";
    desc.no_default_font = true;
    simgui_setup(&desc);
    setupImGuiFonts();
    setupImGuiStyle();
    std::string defaultRoot = (fs::current_path() / "Projects").string();
    std::snprintf(m_projectRootBuf, sizeof(m_projectRootBuf), "%s", defaultRoot.c_str());
    std::snprintf(m_projectNameBuf, sizeof(m_projectNameBuf), "%s", "NewProject");
    std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", (fs::current_path() / "Projects" / "NewProject" / "Scenes" / "Main.rcscene").string().c_str());
    std::snprintf(m_ollamaBaseUrl, sizeof(m_ollamaBaseUrl), "%s", "http://localhost:11434");
    m_frameTimeSamples.assign(120, 0.0f);
    return true;
}

void GuiLayer::newFrame() {
    float deltaTime = (float)sapp_frame_duration();
    recordFrameStats(deltaTime);

    simgui_frame_desc_t desc = {};
    desc.width = sapp_width();
    desc.height = sapp_height();
    desc.delta_time = deltaTime;
    desc.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&desc);
}

void GuiLayer::render() {
    showMainMenu();
    showStartupDialog();

    int vpW = sapp_width();
    int vpH = sapp_height();
    float menuBarH = ImGui::GetFrameHeight();

    float minPanelW = 180.0f;
    float maxPanelW = (float)vpW * 0.40f;
    float minBottomH = 150.0f;
    float maxBottomH = (float)vpH * 0.55f;

    m_leftPanelWidth = std::clamp(m_leftPanelWidth, minPanelW, maxPanelW);
    m_rightPanelWidth = std::clamp(m_rightPanelWidth, minPanelW, maxPanelW);
    m_bottomPanelHeight = std::clamp(m_bottomPanelHeight, minBottomH, maxBottomH);

    float leftW = m_showSceneHierarchy ? m_leftPanelWidth : 0.0f;
    float rightW = m_showInspector ? m_rightPanelWidth : 0.0f;
    float bottomH = m_showContentBrowser ? m_bottomPanelHeight : 0.0f;
    float splitter = 4.0f;

    float viewX = leftW;
    float viewY = menuBarH;
    float viewW = (float)vpW - leftW - rightW;
    float viewH = (float)vpH - menuBarH - bottomH;

    // ---- Scene Hierarchy (left) ----
    if (m_showSceneHierarchy) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, menuBarH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_leftPanelWidth, viewH), ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Hierarchy", &m_showSceneHierarchy, flags);
        ImGui::PopStyleVar();
        showSceneHierarchy();
        ImGui::End();

        // left-right splitter (between hierarchy and viewport)
        if (drawSplitter("##splitter-lr", ImVec2(m_leftPanelWidth - splitter * 0.5f, menuBarH), ImVec2(splitter, viewH), ImGuiMouseCursor_ResizeEW)) {
            m_leftPanelWidth += ImGui::GetIO().MouseDelta.x;
        }
    }

    // ---- Inspector (right) ----
    if (m_showInspector) {
        ImGui::SetNextWindowPos(ImVec2((float)vpW - m_rightPanelWidth, menuBarH), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(m_rightPanelWidth, viewH), ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Inspector", &m_showInspector, flags);
        ImGui::PopStyleVar();
        if (ImGui::BeginTabBar("RightPanelTabs")) {
            if (ImGui::BeginTabItem("Inspector")) {
                showInspector();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Info")) {
                showInfoPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // ---- Content Browser (bottom) ----
    if (m_showContentBrowser) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, (float)vpH - m_bottomPanelHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)vpW, m_bottomPanelHeight), ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Content Browser", &m_showContentBrowser, flags);
        ImGui::PopStyleVar();
        showContentBrowser();
        ImGui::End();

        // bottom splitter (between viewport and content browser)
        float splitterY = (float)vpH - m_bottomPanelHeight - splitter * 0.5f;
        if (drawSplitter("##splitter-bt", ImVec2(leftW, splitterY), ImVec2(viewW, splitter), ImGuiMouseCursor_ResizeNS)) {
            m_bottomPanelHeight -= ImGui::GetIO().MouseDelta.y;
        }
    }

    // ---- Debug Window ----
    if (m_showCodeEditor) {
        showScriptEditorWindow();
    }

    if (m_showDebugWindow) {
        showDebugWindow();
    }

    if (m_showAiWindow) {
        showAiAssistantWindow();
    }

    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

    auto& engine = Engine::instance();
    if (engine.consumeViewportMenu()) {
        ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
        ImGui::OpenPopup("ViewportMenu");
    }

    if (ImGui::BeginPopup("ViewportMenu")) {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Box")) { engine.createPrimitive("Box"); }
            if (ImGui::MenuItem("Sphere")) { engine.createPrimitive("Sphere"); }
            if (ImGui::MenuItem("Capsule")) { engine.createPrimitive("Capsule"); }
            if (ImGui::MenuItem("Cylinder")) { engine.createPrimitive("Cylinder"); }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Empty")) {
            engine.selectEntity(engine.scene().createObject("Empty Object"));
        }
        if (ImGui::MenuItem("Create Camera")) {
            SceneEntity entity = engine.scene().createObject("Camera");
            if (SceneObject* object = engine.scene().getObject(entity)) {
                object->transform.local.position = { 0, 1, -5 };
            }
            engine.scene().addCamera(entity);
            engine.selectEntity(entity);
        }
        if (ImGui::MenuItem("Create Directional Light")) {
            SceneEntity entity = engine.scene().createObject("Directional Light");
            engine.scene().addLight(entity);
            engine.selectEntity(entity);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Selected", nullptr, false, engine.selectedEntityCount() > 0)) {
            engine.deleteSelectedEntity();
        }
        ImGui::EndPopup();
    }

    simgui_render();
}

bool GuiLayer::handleEvent(const void* ev) {
    return simgui_handle_event((const sapp_event*)ev);
}

void GuiLayer::shutdown() {
    simgui_shutdown();
}

bool GuiLayer::isViewportArea(float x, float y) const {
    float menuBarH = ImGui::GetFrameHeight();
    float leftW = m_showSceneHierarchy ? m_leftPanelWidth : 0.0f;
    float rightW = m_showInspector ? m_rightPanelWidth : 0.0f;
    float bottomH = m_showContentBrowser ? m_bottomPanelHeight : 0.0f;
    float viewportX0 = leftW;
    float viewportY0 = menuBarH;
    float viewportX1 = (float)sapp_width() - rightW;
    float viewportY1 = (float)sapp_height() - bottomH;

    return x >= viewportX0 && x < viewportX1 && y >= viewportY0 && y < viewportY1;
}

void GuiLayer::showMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        auto& engine = Engine::instance();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                engine.newScene();
                m_statusMessage = "New scene created. Use Save Scene As to name it.";
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, engine.hasProject() || !engine.currentScenePath().empty())) {
                if (!engine.saveScene()) {
                    m_statusMessage = engine.lastProjectError();
                } else {
                    m_statusMessage = "Scene saved.";
                }
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                std::string path = engine.currentScenePath().empty()
                    ? (fs::path(engine.projectPath()) / "Scenes" / "Main.rcscene").string()
                    : engine.currentScenePath();
                std::snprintf(m_saveScenePathBuf, sizeof(m_saveScenePathBuf), "%s", path.c_str());
                m_showSaveAsDialog = true;
                ImGui::OpenPopup("Save Scene As");
            }
            if (ImGui::MenuItem("Load Scene...")) {
                std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", engine.currentScenePath().c_str());
                m_showLoadSceneDialog = true;
                ImGui::OpenPopup("Load Scene");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Game", nullptr, false, engine.hasProject() || !engine.currentScenePath().empty())) {
                std::string exportedExe;
                if (engine.exportGame(exportedExe)) {
                    m_statusMessage = "Exported game: " + exportedExe;
                } else {
                    m_statusMessage = engine.lastProjectError();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                sapp_request_quit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &m_showSceneHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &m_showInspector);
            ImGui::MenuItem("Content Browser", nullptr, &m_showContentBrowser);
            ImGui::MenuItem("AI Assistant", nullptr, &m_showAiWindow);
            ImGui::Separator();
            ImGui::MenuItem("Debug Window", nullptr, &m_showDebugWindow);
            if (ImGui::BeginMenu("Debug Mode", m_showDebugWindow)) {
                if (ImGui::MenuItem("Compact", nullptr, m_debugMode == DebugMode::Compact)) {
                    m_debugMode = DebugMode::Compact;
                }
                if (ImGui::MenuItem("Performance", nullptr, m_debugMode == DebugMode::Performance)) {
                    m_debugMode = DebugMode::Performance;
                }
                if (ImGui::MenuItem("Engine", nullptr, m_debugMode == DebugMode::Engine)) {
                    m_debugMode = DebugMode::Engine;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("ImGui Demo", nullptr, &m_showDemo);
            ImGui::EndMenu();
        }

        const char* stateText = "Stopped";
        if (engine.playState() == Engine::PlayState::Playing) {
            stateText = "Playing";
        } else if (engine.playState() == Engine::PlayState::Paused) {
            stateText = "Paused";
        }

        const float buttonWidth = 30.0f;
        const float groupWidth =
            buttonWidth * 3.0f +
            ImGui::CalcTextSize(stateText).x +
            ImGui::GetStyle().ItemSpacing.x * 4.0f;
        float rightX = ImGui::GetWindowContentRegionMax().x - groupWidth;
        if (ImGui::GetCursorPosX() < rightX) {
            ImGui::SetCursorPosX(rightX);
        }

        if (transportButton("##play", TransportIcon::Play, ImVec2(buttonWidth, 0.0f))) {
            engine.play();
        }
        ImGui::SameLine();
        if (transportButton("##pause", TransportIcon::Pause, ImVec2(buttonWidth, 0.0f))) {
            engine.pause();
        }
        ImGui::SameLine();
        if (transportButton("##stop", TransportIcon::Stop, ImVec2(buttonWidth, 0.0f))) {
            engine.stop();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(stateText);

        ImGui::EndMainMenuBar();
    }
}

void GuiLayer::showStartupDialog() {
    auto& engine = Engine::instance();

    if (m_showStartupDialog && !engine.hasProject()) {
        ImGui::OpenPopup("RealCore Project");
    }

    ImVec2 center((float)sapp_width() * 0.5f, (float)sapp_height() * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 300), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("RealCore Project", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Create a new project or load an existing .rcscene.");
        ImGui::Separator();

        ImGui::TextUnformatted("Create Project");
        ImGui::InputText("Root Folder", m_projectRootBuf, sizeof(m_projectRootBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##project-root")) {
            std::string folder = NativeDialogs::selectFolder();
            if (!folder.empty()) {
                std::snprintf(m_projectRootBuf, sizeof(m_projectRootBuf), "%s", folder.c_str());
            }
        }
        ImGui::InputText("Project Name", m_projectNameBuf, sizeof(m_projectNameBuf));
        if (ImGui::Button("Create Project", ImVec2(150, 0))) {
            if (engine.createProject(m_projectRootBuf, m_projectNameBuf)) {
                m_showStartupDialog = false;
                m_statusMessage = "Project created: " + engine.projectPath();
                ImGui::CloseCurrentPopup();
            } else {
                m_statusMessage = engine.lastProjectError();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Load Scene");
        ImGui::InputText(".rcscene Path", m_scenePathBuf, sizeof(m_scenePathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##startup-load-scene")) {
            std::string file = NativeDialogs::openSceneFile();
            if (!file.empty()) {
                std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", file.c_str());
            }
        }
        if (ImGui::Button("Load Scene", ImVec2(150, 0))) {
            if (engine.loadScene(m_scenePathBuf)) {
                m_showStartupDialog = false;
                m_statusMessage = "Scene loaded.";
                ImGui::CloseCurrentPopup();
            } else {
                m_statusMessage = engine.lastProjectError();
            }
        }

        if (!m_statusMessage.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", m_statusMessage.c_str());
        }

        ImGui::EndPopup();
    }

    if (m_showSaveAsDialog) {
        ImGui::OpenPopup("Save Scene As");
    }
    ImGui::SetNextWindowSize(ImVec2(560, 120), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Save Scene As", &m_showSaveAsDialog, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::InputText(".rcscene Path", m_saveScenePathBuf, sizeof(m_saveScenePathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##save-scene")) {
            std::string file = NativeDialogs::saveSceneFile(m_saveScenePathBuf);
            if (!file.empty()) {
                std::snprintf(m_saveScenePathBuf, sizeof(m_saveScenePathBuf), "%s", file.c_str());
            }
        }
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            if (engine.saveScene(m_saveScenePathBuf)) {
                m_statusMessage = "Scene saved.";
                m_showSaveAsDialog = false;
                ImGui::CloseCurrentPopup();
            } else {
                m_statusMessage = engine.lastProjectError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_showSaveAsDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_showLoadSceneDialog) {
        ImGui::OpenPopup("Load Scene");
    }
    ImGui::SetNextWindowSize(ImVec2(560, 120), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Load Scene", &m_showLoadSceneDialog, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::InputText(".rcscene Path", m_scenePathBuf, sizeof(m_scenePathBuf));
        ImGui::SameLine();
        if (ImGui::Button("Browse##load-scene")) {
            std::string file = NativeDialogs::openSceneFile();
            if (!file.empty()) {
                std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", file.c_str());
            }
        }
        if (ImGui::Button("Load", ImVec2(100, 0))) {
            if (engine.loadScene(m_scenePathBuf)) {
                m_statusMessage = "Scene loaded.";
                m_showLoadSceneDialog = false;
                m_showStartupDialog = false;
                ImGui::CloseCurrentPopup();
            } else {
                m_statusMessage = engine.lastProjectError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            m_showLoadSceneDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GuiLayer::showSceneHierarchy() {
    auto& engine = Engine::instance();
    auto& scene = engine.scene();

    auto requestRename = [&](SceneEntity entity) {
        SceneObject* object = scene.getObject(entity);
        if (!object || !object->active) {
            return;
        }

        m_hierarchyRenameEntity = entity;
        std::memset(m_hierarchyRenameBuf, 0, sizeof(m_hierarchyRenameBuf));
        std::snprintf(m_hierarchyRenameBuf, sizeof(m_hierarchyRenameBuf), "%s", object->name.c_str());
        m_showHierarchyRename = true;
        m_openHierarchyRenamePopup = true;
    };

    bool useSceneCamera = engine.useSceneCamera();
    if (ImGui::Checkbox("Use Scene Camera", &useSceneCamera)) {
        engine.setUseSceneCamera(useSceneCamera);
    }

    bool hasSelection = engine.selectedEntityCount() > 0;
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Rename")) {
        requestRename(engine.selectedEntity());
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        engine.deleteSelectedEntity();
    }
    ImGui::EndDisabled();
    if (hasSelection) {
        ImGui::SameLine();
        ImGui::TextDisabled("%zu selected", engine.selectedEntityCount());
    }

    if (ImGui::BeginPopupContextWindow("scene-create-menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Create Empty")) {
            engine.selectEntity(scene.createObject("Empty Object"));
        }
        if (ImGui::MenuItem("Create Camera")) {
            SceneEntity entity = scene.createObject("Camera");
            if (SceneObject* object = scene.getObject(entity)) {
                object->transform.local.position = { 0, 1, -5 };
            }
            if (CameraComponent* camera = scene.addCamera(entity)) {
                camera->primary = scene.findPrimaryCamera() == entity;
            }
            engine.selectEntity(entity);
        }
        if (ImGui::MenuItem("Create Directional Light")) {
            SceneEntity entity = scene.createObject("Directional Light");
            scene.addLight(entity);
            engine.selectEntity(entity);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename Selected", nullptr, false, hasSelection)) {
            requestRename(engine.selectedEntity());
        }
        if (ImGui::MenuItem("Delete Selected", nullptr, false, hasSelection)) {
            engine.deleteSelectedEntity();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    ImGui::BeginChild("##hierarchy-list", ImVec2(0, 0), false);
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete) && engine.selectedEntityCount() > 0) {
        engine.deleteSelectedEntity();
    }

    if (scene.objects().empty()) {
        ImGui::TextDisabled("No scene objects.");
    } else {
        for (auto& object : scene.objects()) {
            if (!object.active) continue;
            ImGui::PushID((int)object.id);
            bool selected = engine.isEntitySelected(object.id);
            if (ImGui::Selectable(object.name.c_str(), selected)) {
                if (ImGui::GetIO().KeyCtrl) {
                    engine.toggleEntitySelection(object.id);
                } else {
                    engine.selectEntity(object.id);
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                requestRename(object.id);
            }
            if (ImGui::BeginPopupContextItem("##hierarchy-item-menu")) {
                if (!engine.isEntitySelected(object.id)) {
                    engine.selectEntity(object.id);
                }
                if (ImGui::MenuItem("Rename")) {
                    requestRename(object.id);
                }
                if (ImGui::MenuItem("Delete")) {
                    engine.deleteSelectedEntity();
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("#%u", object.id);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (m_openHierarchyRenamePopup) {
        ImGui::OpenPopup("Rename Object");
        m_openHierarchyRenamePopup = false;
    }

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename Object", &m_showHierarchyRename, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        SceneObject* object = scene.getObject(m_hierarchyRenameEntity);
        if (!object || !object->active) {
            m_showHierarchyRename = false;
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("Object #%u", object->id);
            bool enter = ImGui::InputText("Name", m_hierarchyRenameBuf, sizeof(m_hierarchyRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();

            bool renameClicked = ImGui::Button("Rename") || enter;
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showHierarchyRename = false;
                ImGui::CloseCurrentPopup();
            }

            if (renameClicked && std::strlen(m_hierarchyRenameBuf) > 0) {
                object->name = m_hierarchyRenameBuf;
                m_showHierarchyRename = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void GuiLayer::showInspector() {
    auto& engine = Engine::instance();
    auto& scene = engine.scene();

    SceneObject* selected = scene.getObject(engine.selectedEntity());
    if (!selected || !selected->active) {
        ImGui::TextDisabled("No object selected");
        engine.selectEntity(InvalidSceneEntity);
        return;
    }

    TransformComponent* transform = scene.getTransform(selected->id);
    MeshRendererComponent* meshRenderer = scene.getMeshRenderer(selected->id);
    RigidBodyComponent* rigidBody = scene.getRigidBody(selected->id);
    LightComponent* light = scene.getLight(selected->id);
    CameraComponent* camera = scene.getCamera(selected->id);
    ScriptComponent* script = scene.getScript(selected->id);

    char nameBuf[256] = {};
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", selected->name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        selected->name = nameBuf;
    }
    ImGui::Text("Entity: %u", selected->id);
    if (engine.selectedEntityCount() > 1) {
        ImGui::Text("Multi-selected: %zu objects", engine.selectedEntityCount());
    }

    if (transform) {
        if (ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen)) {
            Vec3 position = transform->local.position;
            if (ImGui::DragFloat3("##Position", &position.x, 0.1f)) {
                engine.setEntityPosition(selected->id, position);
            }
        }

        if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
            Vec3 rotationDegrees = {
                transform->local.rotation.x * 180.0f / 3.14159265f,
                transform->local.rotation.y * 180.0f / 3.14159265f,
                transform->local.rotation.z * 180.0f / 3.14159265f
            };
            if (ImGui::DragFloat3("Degrees", &rotationDegrees.x, 1.0f, -360.0f, 360.0f)) {
                transform->local.rotation = {
                    rotationDegrees.x * 3.14159265f / 180.0f,
                    rotationDegrees.y * 3.14159265f / 180.0f,
                    rotationDegrees.z * 3.14159265f / 180.0f
                };
            }

            if (ImGui::Button("Reset Rotation")) {
                transform->local.rotation = { 0.0f, 0.0f, 0.0f };
            }
        }

        if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("##Scale", &transform->local.scale.x, 0.05f, 0.01f, 100.0f);
            if (ImGui::Button("Reset Scale")) {
                transform->local.scale = { 1.0f, 1.0f, 1.0f };
            }
        }
    }

    if (meshRenderer && ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        Mesh* mesh = engine.resources().getMesh(meshRenderer->meshHandle);
        if (!mesh) {
            ImGui::TextDisabled("No mesh loaded.");
        } else if (mesh->materials().empty()) {
            ImGui::TextDisabled("Mesh has no imported materials.");
            ImGui::Text("Verts: %d", mesh->vertexCount());
            ImGui::Text("Tris:  %d", mesh->indexCount() / 3);
        } else {
            auto& materials = mesh->materials();
            for (size_t i = 0; i < materials.size(); i++) {
                MaterialData& mat = materials[i];
                ImGui::PushID((int)i);
                std::string label = "Material " + std::to_string(i);
                if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::ColorEdit4("Base Color", mat.baseColor);
                    ImGui::SliderFloat("Metallic", &mat.metallicFactor, 0.0f, 1.0f);
                    ImGui::SliderFloat("Roughness", &mat.roughnessFactor, 0.04f, 1.0f);
                    ImGui::SeparatorText("Maps");
                    ImGui::Text("Albedo/BaseColor: %s", mat.hasTexture ? "Yes" : "No");
                    ImGui::Text("Normal: %s", mat.hasNormalTexture ? "Yes" : "No");
                    ImGui::Text("Metallic: %s", mat.hasMetallicTexture ? "Yes" : "No");
                    ImGui::Text("Roughness: %s", mat.hasRoughnessTexture ? "Yes" : "No");
                    ImGui::Text("MetallicRoughness: %s", mat.hasMetallicRoughnessTexture ? "Yes" : "No");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }

    if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (meshRenderer && ImGui::TreeNodeEx("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Mesh Handle: %u", meshRenderer->meshHandle);
            ImGui::Text("Source: %s", engine.resources().meshSourcePath(meshRenderer->meshHandle).empty()
                ? engine.resources().meshAssetKind(meshRenderer->meshHandle).c_str()
                : engine.resources().meshSourcePath(meshRenderer->meshHandle).c_str());
            ImGui::Checkbox("Visible", &meshRenderer->visible);
            Mesh* mesh = engine.resources().getMesh(meshRenderer->meshHandle);
            if (mesh) {
                ImGui::Text("Verts: %d", mesh->vertexCount());
                ImGui::Text("Tris:  %d", mesh->indexCount() / 3);
                ImGui::Text("SubMeshes: %zu", mesh->submeshes().size());
                if (mesh->bounds().valid) {
                    const MeshBounds& bounds = mesh->bounds();
                    Vec3 localSize = bounds.max - bounds.min;
                    Vec3 scaleAbs = {
                        std::abs(selected->transform.local.scale.x),
                        std::abs(selected->transform.local.scale.y),
                        std::abs(selected->transform.local.scale.z)
                    };
                    Vec3 scaledSize = {
                        localSize.x * scaleAbs.x,
                        localSize.y * scaleAbs.y,
                        localSize.z * scaleAbs.z
                    };
                    ImGui::SeparatorText("Bounds");
                    ImGui::Text("Local size: %.3f, %.3f, %.3f", localSize.x, localSize.y, localSize.z);
                    ImGui::Text("Scaled size: %.3f, %.3f, %.3f", scaledSize.x, scaledSize.y, scaledSize.z);
                    ImGui::Text("Local radius: %.3f", bounds.radius);

                    ImGui::SeparatorText("Collision");
                    const char* collisionButton = rigidBody ? "Rebuild Simple Collision" : "Add Simple Collision";
                    if (ImGui::Button(collisionButton)) {
                        Vec3 halfExtent = {
                            (std::max)(0.01f, std::abs(scaledSize.x) * 0.5f),
                            (std::max)(0.01f, std::abs(scaledSize.y) * 0.5f),
                            (std::max)(0.01f, std::abs(scaledSize.z) * 0.5f)
                        };

                        uint32_t bodyId = engine.physics().addBox(selected->transform.local.position, halfExtent, true);
                        if (bodyId == PhysicsWorld::InvalidBodyId) {
                            m_statusMessage = "Failed to create simple collision.";
                        } else {
                            if (selected->hasRigidBody && selected->rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
                                engine.physics().removeBody(selected->rigidBody.bodyId);
                            }
                            scene.removeRigidBody(selected->id);

                            RigidBodyComponent* created = scene.addRigidBody(selected->id, bodyId);
                            if (!created) {
                                engine.physics().removeBody(bodyId);
                                m_statusMessage = "Failed to attach simple collision.";
                            } else {
                                created->shape = RigidBodyComponent::Shape::Box;
                                created->halfExtent = halfExtent;
                                created->dynamic = true;
                                created->syncTransform = true;
                                created->frozen = true;
                                engine.physics().setPosition(created->bodyId, selected->transform.local.position);
                                engine.physics().setFrozen(created->bodyId, created->frozen);
                                engine.physics().resetMotion(created->bodyId);
                                rigidBody = created;
                                m_statusMessage = "Simple box collision added.";
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Simple box only; not for complex models.");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This creates one box collider from mesh bounds. For complex models, create manual collision for now.");
                    }
                }
            }
            ImGui::TreePop();
        }

        if (rigidBody && ImGui::TreeNodeEx("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Body: %u", rigidBody->bodyId);
            ImGui::Text("Collider: %s", rigidBody->shape == RigidBodyComponent::Shape::Box ? "Box" :
                rigidBody->shape == RigidBodyComponent::Shape::Sphere ? "Sphere" :
                rigidBody->shape == RigidBodyComponent::Shape::Capsule ? "Capsule" : "Cylinder");
            if (rigidBody->shape == RigidBodyComponent::Shape::Box) {
                ImGui::Text("Half Extent: %.3f, %.3f, %.3f", rigidBody->halfExtent.x, rigidBody->halfExtent.y, rigidBody->halfExtent.z);
            } else {
                ImGui::Text("Radius: %.3f", rigidBody->radius);
                ImGui::Text("Half Height: %.3f", rigidBody->halfHeight);
            }
            ImGui::Checkbox("Sync Transform", &rigidBody->syncTransform);
            bool frozen = rigidBody->frozen;
            if (ImGui::Checkbox("Frozen In Place", &frozen)) {
                rigidBody->frozen = frozen;
                engine.physics().setFrozen(rigidBody->bodyId, rigidBody->frozen);
                if (rigidBody->frozen) {
                    engine.physics().setPosition(rigidBody->bodyId, selected->transform.local.position);
                }
            }
            ImGui::TextDisabled("Keeps collision active but prevents gravity and movement.");
            ImGui::TreePop();
        }

        if (light && ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &light->enabled);
            ImGui::DragFloat3("Direction", &light->direction.x, 0.02f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", &light->color.x);
            ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Ambient", &light->ambient, 0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (camera && ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &camera->enabled);
            if (ImGui::Checkbox("Primary", &camera->primary) && camera->primary) {
                for (auto& object : scene.objects()) {
                    if (object.id != selected->id && object.hasCamera) {
                        object.camera.primary = false;
                    }
                }
            }

            float fovDegrees = camera->fovY * 180.0f / 3.14159f;
            if (ImGui::DragFloat("FOV", &fovDegrees, 0.5f, 20.0f, 140.0f)) {
                camera->fovY = fovDegrees * 3.14159f / 180.0f;
            }
            ImGui::DragFloat("Near", &camera->zNear, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far", &camera->zFar, 1.0f, 1.0f, 10000.0f);
            ImGui::TreePop();
        }

        if (script && ImGui::TreeNodeEx("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Module: %s", script->moduleName.empty() ? "<not loaded>" : script->moduleName.c_str());
            ImGui::TextWrapped("Path: %s", script->scriptPath.empty() ? "<none>" : script->scriptPath.c_str());
            if (!m_codePath.empty()) {
                ImGui::TextWrapped("Open script: %s", m_codePath.c_str());
                if (ImGui::Button("Replace With Open Script")) {
                    if (engine.attachScript(selected->id, m_codePath)) {
                        m_statusMessage = "Script attached: " + m_codeName;
                    } else {
                        m_statusMessage = engine.lastProjectError();
                    }
                }
            }
            if (ImGui::Button("Reload Script")) {
                if (engine.attachScript(selected->id, script->scriptPath)) {
                    m_statusMessage = "Script reloaded: " + script->scriptPath;
                } else {
                    m_statusMessage = engine.lastProjectError();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Script")) {
                engine.detachScript(selected->id);
                m_statusMessage = "Script removed.";
            }
            ImGui::TreePop();
        }

        if (!meshRenderer && !rigidBody && !light && !camera && !script) {
            ImGui::TextDisabled("No components.");
        }

        if (!script && ImGui::TreeNodeEx("Add Script", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (m_scriptAttachPathBuf[0] == '\0' && !m_codePath.empty()) {
                std::snprintf(m_scriptAttachPathBuf, sizeof(m_scriptAttachPathBuf), "%s", m_codePath.c_str());
            }
            ImGui::TextWrapped("Attach an AngelScript .as or Lua .lua file to this object. During Play/export the engine calls init(), update(dt), and destroy().");
            ImGui::InputText("Path", m_scriptAttachPathBuf, sizeof(m_scriptAttachPathBuf));

            ImGui::BeginDisabled(m_codePath.empty());
            if (ImGui::Button("Use Open Script")) {
                std::snprintf(m_scriptAttachPathBuf, sizeof(m_scriptAttachPathBuf), "%s", m_codePath.c_str());
            }
            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Button("Attach")) {
                if (engine.attachScript(selected->id, m_scriptAttachPathBuf)) {
                    m_statusMessage = "Script attached.";
                } else {
                    m_statusMessage = engine.lastProjectError();
                }
            }

            ImGui::TreePop();
        }
    }

    if (ImGui::Button("Focus Selected", ImVec2(-1, 0))) {
        engine.focusOnEntity(selected->id);
    }
}

void GuiLayer::showInfoPanel() {
    ImGui::BeginChild("##info-panel-scroll", ImVec2(0, 0), false);

    ImGui::SeparatorText("Scene Switching");
    ImGui::TextWrapped("Use separate .rcscene files for menu, levels, and gameplay scenes. Scripts can switch scenes with loadScene().");

    ImGui::Spacing();
    ImGui::BulletText("Create or load your menu scene.");
    ImGui::BulletText("Save it with File > Save Scene As, for example Scenes/Menu.rcscene.");
    ImGui::BulletText("Use File > New Scene to create the gameplay scene.");
    ImGui::BulletText("Save the gameplay scene, for example Scenes/Game.rcscene.");
    ImGui::BulletText("Create a script in Scripts, attach it to an object in the menu scene.");
    ImGui::BulletText("Call loadScene(\"Game\") or loadScene(\"Scenes/Game.rcscene\") from that script.");

    ImGui::Spacing();
    ImGui::SeparatorText("Example");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("##scene-switch-example", ImVec2(0.0f, 152.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(
        "float timer = 0.0f;\n\n"
        "void init() {\n"
        "    print(\"Menu scene loaded\");\n"
        "}\n\n"
        "void update(float dt) {\n"
        "    timer += dt;\n"
        "    if (timer > 2.0f) {\n"
        "        loadScene(\"Game\");\n"
        "    }\n"
        "}\n");
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::SeparatorText("Export");
    ImGui::TextWrapped("Export Game copies the whole Scenes folder and Scripts folder. The exported exe starts from the scene that is open when you export.");
    ImGui::TextWrapped("If the open scene is your menu, the menu becomes the first scene and can switch to Game.rcscene.");

    ImGui::Spacing();
    ImGui::SeparatorText("Script API");
    ImGui::BulletText("loadScene(\"Name\") loads Scenes/Name.rcscene.");
    ImGui::BulletText("loadScene(\"Scenes/Name.rcscene\") loads an explicit scene path.");
    ImGui::BulletText("quitGame() closes the exported game.");
    ImGui::BulletText("print(\"text\") writes text to the console.");
    ImGui::BulletText("Types: Vector2, Vector3, Quaternion, Color.");
    ImGui::BulletText("Globals: node, scene, input, physics, audio, time.");
    ImGui::BulletText("Components: node.transform, node.camera, node.light, node.rigidBody.");

    ImGui::Spacing();
    ImGui::SeparatorText("Object API Example");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 8.0f));
    ImGui::BeginChild("##script-api-example", ImVec2(0.0f, 126.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(
        "void update(float dt) {\n"
        "    Transform t = node.transform;\n"
        "    t.position = t.position + Vector3(0.0f, 1.0f, 0.0f) * dt;\n\n"
        "    if (node.hasRigidBody()) {\n"
        "        node.rigidBody.frozen = true;\n"
        "    }\n"
        "}\n");
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::SeparatorText("Attach Script");
    ImGui::BulletText("Open a .as or .lua file in the Files panel.");
    ImGui::BulletText("Select an object in Hierarchy.");
    ImGui::BulletText("Inspector > Components > Add Script > Use Open Script > Attach.");
    ImGui::BulletText("AI Assistant can also attach scripts with attach_script actions.");

    ImGui::EndChild();
}

void GuiLayer::recordFrameStats(float deltaTime) {
    m_lastFrameMs = deltaTime * 1000.0f;

    if (m_frameTimeSamples.empty()) {
        m_frameTimeSamples.assign(120, 0.0f);
    }

    m_frameTimeSamples[m_frameTimeSampleIndex] = m_lastFrameMs;
    m_frameTimeSampleIndex = (m_frameTimeSampleIndex + 1) % m_frameTimeSamples.size();

    float sum = 0.0f;
    int count = 0;
    m_minFrameMs = 1000000.0f;
    m_maxFrameMs = 0.0f;

    for (float sample : m_frameTimeSamples) {
        if (sample <= 0.0f) continue;
        sum += sample;
        m_minFrameMs = (std::min)(m_minFrameMs, sample);
        m_maxFrameMs = (std::max)(m_maxFrameMs, sample);
        count++;
    }

    if (count > 0) {
        m_avgFrameMs = sum / (float)count;
    } else {
        m_avgFrameMs = m_lastFrameMs;
        m_minFrameMs = m_lastFrameMs;
        m_maxFrameMs = m_lastFrameMs;
    }
}

void GuiLayer::showScriptEditorWindow() {
    if (!m_showCodeEditor) return;

    ImGui::SetNextWindowPos(m_scriptEditorPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(m_scriptEditorSize, ImGuiCond_Appearing);

    if (!ImGui::Begin("Script Editor", &m_showCodeEditor)) {
        ImGui::End();
        return;
    }

    showCodeEditor();

    ImGui::End();

    m_scriptEditorPos = ImGui::GetWindowPos();
    m_scriptEditorSize = ImGui::GetWindowSize();
}

void GuiLayer::showDebugWindow() {
    auto& engine = Engine::instance();
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(12.0f, ImGui::GetFrameHeight() + 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 120.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Debug", &m_showDebugWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginCombo("Mode",
        m_debugMode == DebugMode::Compact ? "Compact" :
        m_debugMode == DebugMode::Performance ? "Performance" : "Engine")) {
        if (ImGui::Selectable("Compact", m_debugMode == DebugMode::Compact)) {
            m_debugMode = DebugMode::Compact;
        }
        if (ImGui::Selectable("Performance", m_debugMode == DebugMode::Performance)) {
            m_debugMode = DebugMode::Performance;
        }
        if (ImGui::Selectable("Engine", m_debugMode == DebugMode::Engine)) {
            m_debugMode = DebugMode::Engine;
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::Text("Frame: %.2f ms", m_lastFrameMs);

    if (m_debugMode == DebugMode::Performance) {
        ImGui::Text("Avg: %.2f ms", m_avgFrameMs);
        ImGui::Text("Min: %.2f ms", m_minFrameMs);
        ImGui::Text("Max: %.2f ms", m_maxFrameMs);
        ImGui::Text("Frame latency: %.2f ms", m_lastFrameMs);
        ImGui::Separator();
        ImGui::Text("Draw vertices: %d", io.MetricsRenderVertices);
        ImGui::Text("Draw indices: %d", io.MetricsRenderIndices);
        ImGui::Text("Active windows: %d", io.MetricsActiveWindows);
    } else if (m_debugMode == DebugMode::Engine) {
        int activeObjects = 0;
        int meshObjects = 0;
        int cameras = 0;
        int lights = 0;
        int scripts = 0;

        for (const auto& object : engine.scene().objects()) {
            if (!object.active) continue;
            activeObjects++;
            if (object.hasMeshRenderer) meshObjects++;
            if (object.hasCamera) cameras++;
            if (object.hasLight) lights++;
            if (object.hasScript) scripts++;
        }

        const char* playState = "Stopped";
        if (engine.playState() == Engine::PlayState::Playing) {
            playState = "Playing";
        } else if (engine.playState() == Engine::PlayState::Paused) {
            playState = "Paused";
        }

        ImGui::Text("Viewport: %d x %d", sapp_width(), sapp_height());
        ImGui::Text("DPI: %.2f", sapp_dpi_scale());
        ImGui::Text("State: %s", playState);
        ImGui::Text("Selected: %zu", engine.selectedEntityCount());
        ImGui::Separator();
        ImGui::Text("Objects: %d", activeObjects);
        ImGui::Text("Meshes: %d", meshObjects);
        ImGui::Text("Cameras: %d", cameras);
        ImGui::Text("Lights: %d", lights);
        ImGui::Text("Scripts: %d", scripts);
        ImGui::Separator();
        ImGui::Text("Project: %s", engine.hasProject() ? "Loaded" : "None");
    }

    ImGui::End();
}

std::string GuiLayer::currentCodeText() const {
    if (m_codeBuffer.empty()) {
        return {};
    }
    return std::string(m_codeBuffer.data(), std::strlen(m_codeBuffer.data()));
}

void GuiLayer::appendToCodeEditor(const std::string& text) {
    if (text.empty()) {
        return;
    }

    std::string current = currentCodeText();
    if (!current.empty() && current.back() != '\n') {
        current += "\n";
    }
    current += text;
    if (!current.empty() && current.back() != '\n') {
        current += "\n";
    }

    m_codeBuffer.assign(current.begin(), current.end());
    m_codeBuffer.push_back('\0');
    m_codeBuffer.resize((std::max)(m_codeBuffer.size() + 4096, (size_t)8192), '\0');
    m_codeDirty = true;
}

static std::string extractFirstMarkdownCodeBlock(const std::string& text);
static std::string trimCopy(std::string value);
static bool extractMarkdownCodeBlockByLabel(const std::string& text, const char* labelPart, std::string& code);
static bool looksLikeFullAngelScript(const std::string& code);
static int countLifecycleFunctions(const std::string& code);
static bool findFunctionName(const std::string& code, std::string& name);
static bool findFunctionRange(const std::string& code, const std::string& name, size_t& rangeStart, size_t& rangeEnd);

bool GuiLayer::applyCodeSuggestionToEditor(const std::string& response, std::string& status) {
    if (m_codePath.empty()) {
        status = "No script file is open.";
        return false;
    }

    std::string current = currentCodeText();
    std::string code;
    bool replaceWholeFile = false;

    if (extractMarkdownCodeBlockByLabel(response, "realcore-script-full", code) ||
        extractMarkdownCodeBlockByLabel(response, "realcore-script", code)) {
        replaceWholeFile = true;
    } else if (extractMarkdownCodeBlockByLabel(response, "realcore-replace-function", code)) {
        replaceWholeFile = false;
    } else {
        code = extractFirstMarkdownCodeBlock(response);
    }

    code = trimCopy(code);
    if (code.empty()) {
        status = "AI response did not contain code.";
        return false;
    }

    std::string next = current;
    std::string functionName;
    if (!replaceWholeFile && countLifecycleFunctions(code) >= 2) {
        replaceWholeFile = true;
    }

    if (replaceWholeFile) {
        next = code;
        status = "Replaced full script from AI.";
    } else if (findFunctionName(code, functionName)) {
        size_t oldStart = 0;
        size_t oldEnd = 0;
        if (findFunctionRange(current, functionName, oldStart, oldEnd)) {
            next.replace(oldStart, oldEnd - oldStart, code);
            status = "Replaced function: " + functionName + "().";
        } else {
            if (!next.empty() && next.back() != '\n') {
                next += "\n";
            }
            next += "\n" + code;
            status = "Added new function: " + functionName + "().";
        }
    } else if (looksLikeFullAngelScript(code) && current.empty()) {
        next = code;
        status = "Inserted full script from AI.";
    } else {
        if (!next.empty() && next.back() != '\n') {
            next += "\n";
        }
        next += "\n" + code;
        status = "Inserted code at end of script.";
    }

    if (!next.empty() && next.back() != '\n') {
        next += "\n";
    }

    m_codeBuffer.assign(next.begin(), next.end());
    m_codeBuffer.push_back('\0');
    m_codeBuffer.resize((std::max)(m_codeBuffer.size() + 4096, (size_t)8192), '\0');
    m_codeDirty = true;
    return true;
}

bool GuiLayer::openTextEditor(const std::string& path) {
    if (path.empty()) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        m_codeStatus = "Failed to open script.";
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    m_codePath = path;
    m_codeName = fs::path(path).filename().string();
    std::string source = ss.str();
    m_codeBuffer.assign(source.begin(), source.end());
    m_codeBuffer.push_back('\0');
    m_codeBuffer.resize((std::max)(m_codeBuffer.size() + 4096, (size_t)8192), '\0');
    m_codeDirty = false;
    m_showCodeEditor = true;
    m_codeStatus = "Editing: " + m_codeName;
    return true;
}

bool GuiLayer::saveCodeEditor() {
    if (m_codePath.empty()) {
        m_codeStatus = "No script file selected.";
        return false;
    }

    std::ofstream file(m_codePath, std::ios::binary | std::ios::trunc);
    if (!file) {
        m_codeStatus = "Failed to save script.";
        return false;
    }

    size_t textLen = std::strlen(m_codeBuffer.data());
    file.write(m_codeBuffer.data(), (std::streamsize)textLen);
    if (!file) {
        m_codeStatus = "Failed to write script.";
        return false;
    }

    m_codeDirty = false;
    m_codeStatus = "Saved: " + m_codeName;
    return true;
}

static std::string extractFirstMarkdownCodeBlock(const std::string& text) {
    size_t start = text.find("```");
    if (start == std::string::npos) {
        return text;
    }

    start += 3;
    size_t lineEnd = text.find('\n', start);
    if (lineEnd != std::string::npos) {
        start = lineEnd + 1;
    }

    size_t end = text.find("```", start);
    if (end == std::string::npos || end <= start) {
        return text;
    }

    return text.substr(start, end - start);
}

static std::string trimCopy(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char c) { return !isSpace((unsigned char)c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char c) { return !isSpace((unsigned char)c); }).base(), value.end());
    return value;
}

static bool extractMarkdownCodeBlockByLabel(const std::string& text, const char* labelPart, std::string& code) {
    size_t pos = 0;
    while ((pos = text.find("```", pos)) != std::string::npos) {
        size_t labelStart = pos + 3;
        size_t lineEnd = text.find('\n', labelStart);
        if (lineEnd == std::string::npos) {
            return false;
        }

        std::string label = text.substr(labelStart, lineEnd - labelStart);
        if (label.find(labelPart) != std::string::npos) {
            size_t contentStart = lineEnd + 1;
            size_t end = text.find("```", contentStart);
            if (end == std::string::npos) {
                return false;
            }
            code = text.substr(contentStart, end - contentStart);
            return true;
        }

        pos = lineEnd + 1;
    }
    return false;
}

static bool looksLikeFullAngelScript(const std::string& code) {
    return code.find("void init(") != std::string::npos ||
        code.find("void update(") != std::string::npos ||
        code.find("void destroy(") != std::string::npos;
}

static int countLifecycleFunctions(const std::string& code) {
    int count = 0;
    if (code.find("void init(") != std::string::npos) count++;
    if (code.find("void update(") != std::string::npos) count++;
    if (code.find("void destroy(") != std::string::npos) count++;
    return count;
}

static bool findFunctionName(const std::string& code, std::string& name) {
    size_t voidPos = code.find("void ");
    if (voidPos == std::string::npos) {
        return false;
    }

    size_t nameStart = voidPos + 5;
    while (nameStart < code.size() && std::isspace((unsigned char)code[nameStart])) {
        nameStart++;
    }

    size_t nameEnd = nameStart;
    while (nameEnd < code.size() && (std::isalnum((unsigned char)code[nameEnd]) || code[nameEnd] == '_')) {
        nameEnd++;
    }

    if (nameEnd == nameStart || nameEnd >= code.size() || code[nameEnd] != '(') {
        return false;
    }

    name = code.substr(nameStart, nameEnd - nameStart);
    return true;
}

static bool findFunctionRange(const std::string& code, const std::string& name, size_t& rangeStart, size_t& rangeEnd) {
    std::string needle = "void " + name + "(";
    size_t start = code.find(needle);
    if (start == std::string::npos) {
        return false;
    }

    size_t brace = code.find('{', start + needle.size());
    if (brace == std::string::npos) {
        return false;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = brace; i < code.size(); i++) {
        char c = code[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                rangeStart = start;
                rangeEnd = i + 1;
                return true;
            }
        }
    }

    return false;
}

static constexpr float kAiPi = 3.14159265f;

static Vec3 degreesToRadians(const Vec3& degrees) {
    return {
        degrees.x * kAiPi / 180.0f,
        degrees.y * kAiPi / 180.0f,
        degrees.z * kAiPi / 180.0f
    };
}

static Vec3 radiansToDegrees(const Vec3& radians) {
    return {
        radians.x * 180.0f / kAiPi,
        radians.y * 180.0f / kAiPi,
        radians.z * 180.0f / kAiPi
    };
}

static size_t skipJsonWhitespace(const std::string& text, size_t pos) {
    while (pos < text.size() && std::isspace((unsigned char)text[pos])) {
        pos++;
    }
    return pos;
}

static int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static void appendUtf8Codepoint(std::string& out, unsigned int cp) {
    if (cp <= 0x7f) {
        out.push_back((char)cp);
    } else if (cp <= 0x7ff) {
        out.push_back((char)(0xc0 | ((cp >> 6) & 0x1f)));
        out.push_back((char)(0x80 | (cp & 0x3f)));
    } else {
        out.push_back((char)(0xe0 | ((cp >> 12) & 0x0f)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back((char)(0x80 | (cp & 0x3f)));
    }
}

static bool readJsonStringValue(const std::string& text, size_t quotePos, std::string& out, size_t* endPos = nullptr) {
    if (quotePos >= text.size() || text[quotePos] != '"') {
        return false;
    }

    out.clear();
    for (size_t i = quotePos + 1; i < text.size(); i++) {
        char c = text[i];
        if (c == '"') {
            if (endPos) {
                *endPos = i + 1;
            }
            return true;
        }

        if (c != '\\') {
            out.push_back(c);
            continue;
        }

        if (++i >= text.size()) {
            return false;
        }

        char escaped = text[i];
        switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= text.size()) {
                    return false;
                }
                unsigned int cp = 0;
                for (int n = 0; n < 4; n++) {
                    int value = hexDigitValue(text[i + 1 + (size_t)n]);
                    if (value < 0) {
                        return false;
                    }
                    cp = (cp << 4) | (unsigned int)value;
                }
                appendUtf8Codepoint(out, cp);
                i += 4;
                break;
            }
            default:
                out.push_back(escaped);
                break;
        }
    }

    return false;
}

static bool findJsonValue(const std::string& objectJson, const char* field, size_t& valuePos) {
    std::string needle = "\"";
    needle += field;
    needle += "\"";

    size_t pos = 0;
    while ((pos = objectJson.find(needle, pos)) != std::string::npos) {
        if (pos > 0 && objectJson[pos - 1] == '\\') {
            pos += needle.size();
            continue;
        }

        size_t colon = objectJson.find(':', pos + needle.size());
        if (colon == std::string::npos) {
            return false;
        }

        valuePos = skipJsonWhitespace(objectJson, colon + 1);
        return valuePos < objectJson.size();
    }

    return false;
}

static bool jsonHasField(const std::string& objectJson, const char* field) {
    size_t valuePos = 0;
    return findJsonValue(objectJson, field, valuePos);
}

static bool jsonReadStringField(const std::string& objectJson, const char* field, std::string& value) {
    size_t valuePos = 0;
    if (!findJsonValue(objectJson, field, valuePos) || valuePos >= objectJson.size() || objectJson[valuePos] != '"') {
        return false;
    }
    return readJsonStringValue(objectJson, valuePos, value);
}

static bool parseJsonNumberAt(const std::string& text, size_t& pos, float& value) {
    pos = skipJsonWhitespace(text, pos);
    if (pos >= text.size()) {
        return false;
    }

    char* end = nullptr;
    value = std::strtof(text.c_str() + pos, &end);
    if (end == text.c_str() + pos) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;
    }

    pos = (size_t)(end - text.c_str());
    return true;
}

static bool jsonReadNumberField(const std::string& objectJson, const char* field, float& value) {
    size_t valuePos = 0;
    if (!findJsonValue(objectJson, field, valuePos)) {
        return false;
    }
    return parseJsonNumberAt(objectJson, valuePos, value);
}

static bool jsonReadBoolField(const std::string& objectJson, const char* field, bool& value) {
    size_t valuePos = 0;
    if (!findJsonValue(objectJson, field, valuePos)) {
        return false;
    }

    if (objectJson.compare(valuePos, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (objectJson.compare(valuePos, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

static bool jsonReadEntityField(const std::string& objectJson, const char* field, SceneEntity& entity) {
    float value = 0.0f;
    if (!jsonReadNumberField(objectJson, field, value) || value <= 0.0f) {
        return false;
    }
    entity = (SceneEntity)value;
    return true;
}

static size_t findMatchingJsonClose(const std::string& text, size_t openPos, char openChar, char closeChar);

static bool jsonReadVec3Field(const std::string& objectJson, const char* field, Vec3& value) {
    size_t valuePos = 0;
    if (!findJsonValue(objectJson, field, valuePos) || valuePos >= objectJson.size()) {
        return false;
    }

    if (objectJson[valuePos] == '[') {
        size_t pos = valuePos + 1;
        float components[3] = {};
        for (int i = 0; i < 3; i++) {
            pos = skipJsonWhitespace(objectJson, pos);
            if (i > 0) {
                if (pos >= objectJson.size() || objectJson[pos] != ',') {
                    return false;
                }
                pos++;
            }
            if (!parseJsonNumberAt(objectJson, pos, components[i])) {
                return false;
            }
        }

        value = { components[0], components[1], components[2] };
        return true;
    }

    if (objectJson[valuePos] == '{') {
        size_t end = findMatchingJsonClose(objectJson, valuePos, '{', '}');
        if (end == std::string::npos || end <= valuePos) {
            return false;
        }
        std::string vectorJson = objectJson.substr(valuePos, end - valuePos + 1);
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!jsonReadNumberField(vectorJson, "x", x) ||
            !jsonReadNumberField(vectorJson, "y", y) ||
            !jsonReadNumberField(vectorJson, "z", z)) {
            return false;
        }
        value = { x, y, z };
        return true;
    }

    return false;
}

static size_t findMatchingJsonClose(const std::string& text, size_t openPos, char openChar, char closeChar) {
    if (openPos >= text.size() || text[openPos] != openChar) {
        return std::string::npos;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = openPos; i < text.size(); i++) {
        char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == openChar) {
            depth++;
        } else if (c == closeChar) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::string::npos;
}

static std::string extractJsonActionArray(const std::string& text) {
    size_t actionPos = text.find("\"action\"");
    size_t actionsPos = text.find("\"actions\"");

    if (actionsPos != std::string::npos) {
        size_t colon = text.find(':', actionsPos + 9);
        if (colon != std::string::npos) {
            size_t arrayStart = skipJsonWhitespace(text, colon + 1);
            if (arrayStart < text.size() && text[arrayStart] == '[') {
                size_t arrayEnd = findMatchingJsonClose(text, arrayStart, '[', ']');
                if (arrayEnd != std::string::npos) {
                    return text.substr(arrayStart, arrayEnd - arrayStart + 1);
                }
            }
        }
    }

    std::string trimmed = trimCopy(text);
    if (!trimmed.empty() && trimmed.front() == '[') {
        size_t arrayEnd = findMatchingJsonClose(trimmed, 0, '[', ']');
        if (arrayEnd != std::string::npos && arrayEnd + 1 <= trimmed.size() &&
            (trimmed.find("\"action\"") != std::string::npos || trimCopy(trimmed.substr(0, arrayEnd + 1)) == "[]")) {
            return trimmed.substr(0, arrayEnd + 1);
        }
    }

    if (actionPos == std::string::npos) {
        return {};
    }

    size_t arrayStart = text.rfind('[', actionPos);
    if (arrayStart != std::string::npos) {
        size_t arrayEnd = findMatchingJsonClose(text, arrayStart, '[', ']');
        if (arrayEnd != std::string::npos && arrayEnd > actionPos) {
            return text.substr(arrayStart, arrayEnd - arrayStart + 1);
        }
    }

    size_t objectStart = text.rfind('{', actionPos);
    if (objectStart != std::string::npos) {
        size_t objectEnd = findMatchingJsonClose(text, objectStart, '{', '}');
        if (objectEnd != std::string::npos && objectEnd > actionPos) {
            return "[" + text.substr(objectStart, objectEnd - objectStart + 1) + "]";
        }
    }

    return {};
}

static std::string extractRealCoreActionsBlock(const std::string& text) {
    size_t pos = 0;
    while ((pos = text.find("```", pos)) != std::string::npos) {
        size_t labelStart = pos + 3;
        size_t lineEnd = text.find('\n', labelStart);
        if (lineEnd == std::string::npos) {
            break;
        }

        std::string label = text.substr(labelStart, lineEnd - labelStart);
        if (label.find("realcore-actions") != std::string::npos) {
            size_t contentStart = lineEnd + 1;
            size_t end = text.find("```", contentStart);
            if (end == std::string::npos) {
                return {};
            }
            std::string content = text.substr(contentStart, end - contentStart);
            std::string actions = extractJsonActionArray(content);
            return actions.empty() ? content : actions;
        }

        pos = lineEnd + 1;
    }

    pos = 0;
    while ((pos = text.find("```", pos)) != std::string::npos) {
        size_t labelStart = pos + 3;
        size_t lineEnd = text.find('\n', labelStart);
        if (lineEnd == std::string::npos) {
            break;
        }

        std::string label = text.substr(labelStart, lineEnd - labelStart);
        size_t contentStart = lineEnd + 1;
        size_t end = text.find("```", contentStart);
        if (end == std::string::npos) {
            break;
        }

        if (label.find("json") != std::string::npos ||
            label.find("actions") != std::string::npos ||
            label.find("realcore") != std::string::npos) {
            std::string actions = extractJsonActionArray(text.substr(contentStart, end - contentStart));
            if (!actions.empty()) {
                return actions;
            }
        }

        pos = end + 3;
    }

    return extractJsonActionArray(text);
}

static std::vector<std::string> splitJsonObjects(const std::string& arrayJson) {
    std::vector<std::string> objects;
    size_t arrayStart = arrayJson.find('[');
    size_t startPos = arrayStart == std::string::npos ? 0 : arrayStart + 1;

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    size_t objectStart = std::string::npos;

    for (size_t i = startPos; i < arrayJson.size(); i++) {
        char c = arrayJson[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (depth == 0) {
                objectStart = i;
            }
            depth++;
        } else if (c == '}') {
            if (depth <= 0) {
                return {};
            }
            depth--;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(arrayJson.substr(objectStart, i - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }

    return objects;
}

static std::string normalizePrimitiveType(std::string type) {
    std::transform(type.begin(), type.end(), type.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    if (type == "sphere") return "Sphere";
    if (type == "capsule") return "Capsule";
    if (type == "cylinder") return "Cylinder";
    return "Box";
}

static void makePrimaryCamera(Scene& scene, SceneEntity entity) {
    for (auto& object : scene.objects()) {
        if (object.hasCamera) {
            object.camera.primary = object.id == entity;
        }
    }
}

static void appendVec3(std::ostringstream& out, const Vec3& value) {
    out << "[" << value.x << ", " << value.y << ", " << value.z << "]";
}

static Vec3 componentAbs(const Vec3& value) {
    return { std::abs(value.x), std::abs(value.y), std::abs(value.z) };
}

static Vec3 componentMin(const Vec3& a, const Vec3& b) {
    return { (std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::min)(a.z, b.z) };
}

static Vec3 componentMax(const Vec3& a, const Vec3& b) {
    return { (std::max)(a.x, b.x), (std::max)(a.y, b.y), (std::max)(a.z, b.z) };
}

static Vec3 defaultBodyHalfExtent(const SceneObject& object) {
    Vec3 scale = componentAbs(object.transform.local.scale);
    return {
        (std::max)(0.05f, scale.x * 0.5f),
        (std::max)(0.05f, scale.y * 0.5f),
        (std::max)(0.05f, scale.z * 0.5f)
    };
}

static Vec3 sanitizePhysicsHalfExtent(Vec3 value) {
    if (!std::isfinite(value.x)) value.x = 0.5f;
    if (!std::isfinite(value.y)) value.y = 0.5f;
    if (!std::isfinite(value.z)) value.z = 0.5f;
    return {
        (std::max)(0.01f, std::abs(value.x)),
        (std::max)(0.01f, std::abs(value.y)),
        (std::max)(0.01f, std::abs(value.z))
    };
}

static Vec3 sanitizePosition(Vec3 value) {
    if (!std::isfinite(value.x)) value.x = 0.0f;
    if (!std::isfinite(value.y)) value.y = 0.0f;
    if (!std::isfinite(value.z)) value.z = 0.0f;
    return value;
}

static float sanitizePositive(float value, float fallback = 0.5f) {
    if (!std::isfinite(value)) {
        value = fallback;
    }
    return (std::max)(0.01f, std::abs(value));
}

static float defaultBodyRadius(const SceneObject& object) {
    Vec3 scale = componentAbs(object.transform.local.scale);
    return (std::max)(0.05f, (std::max)(scale.x, scale.z) * 0.5f);
}

static void appendWorldBounds(std::ostringstream& out, const MeshBounds& bounds, const TransformComponent& transform) {
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

    Vec3 worldMin = transform.world.transformPoint(corners[0]);
    Vec3 worldMax = worldMin;
    for (int i = 1; i < 8; i++) {
        Vec3 p = transform.world.transformPoint(corners[i]);
        worldMin = componentMin(worldMin, p);
        worldMax = componentMax(worldMax, p);
    }

    Vec3 localSize = bounds.max - bounds.min;
    Vec3 worldSize = worldMax - worldMin;
    Vec3 worldCenter = (worldMin + worldMax) * 0.5f;
    Vec3 scaleAbs = componentAbs(transform.local.scale);
    float maxScale = (std::max)(scaleAbs.x, (std::max)(scaleAbs.y, scaleAbs.z));

    out << "  meshBounds valid=true localMin=";
    appendVec3(out, bounds.min);
    out << " localMax=";
    appendVec3(out, bounds.max);
    out << " localSize=";
    appendVec3(out, localSize);
    out << " localCenter=";
    appendVec3(out, bounds.center);
    out << " localRadius=" << bounds.radius << "\n";

    out << "  worldBounds min=";
    appendVec3(out, worldMin);
    out << " max=";
    appendVec3(out, worldMax);
    out << " size=";
    appendVec3(out, worldSize);
    out << " center=";
    appendVec3(out, worldCenter);
    out << " approximateRadius=" << bounds.radius * maxScale << "\n";
}

std::string GuiLayer::buildAiEngineContext() const {
    auto& engine = Engine::instance();
    const auto& scene = engine.scene();

    const char* playState = "Stopped";
    if (engine.playState() == Engine::PlayState::Playing) {
        playState = "Playing";
    } else if (engine.playState() == Engine::PlayState::Paused) {
        playState = "Paused";
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "Project: " << (engine.hasProject() ? engine.projectPath() : "none") << "\n";
    out << "ScenePath: " << (engine.currentScenePath().empty() ? "none" : engine.currentScenePath()) << "\n";
    out << "PlayState: " << playState << "\n";
    out << "CoordinateSystem: X right, Y up, Z world depth\n";
    out << "Selected:";
    if (engine.selectedEntities().empty()) {
        out << " none";
    } else {
        for (SceneEntity entity : engine.selectedEntities()) {
            out << " " << entity;
        }
    }
    out << "\n";
    if (!m_codePath.empty()) {
        out << "OpenScript: name=\"" << m_codeName << "\" path=\"" << m_codePath << "\"\n";
    } else {
        out << "OpenScript: none\n";
    }

    out << "Objects:\n";
    for (const auto& object : scene.objects()) {
        if (!object.active) {
            continue;
        }

        out << "- id=" << object.id << " name=\"" << object.name << "\" components=";
        bool anyComponent = false;
        auto appendComponent = [&](const char* name) {
            if (anyComponent) {
                out << ",";
            }
            out << name;
            anyComponent = true;
        };
        if (object.hasMeshRenderer) appendComponent("MeshRenderer");
        if (object.hasRigidBody) appendComponent("RigidBody");
        if (object.hasLight) appendComponent("Light");
        if (object.hasCamera) appendComponent("Camera");
        if (object.hasScript) appendComponent("Script");
        if (!anyComponent) {
            out << "None";
        }
        out << "\n  position=";
        appendVec3(out, object.transform.local.position);
        out << " rotationDeg=";
        appendVec3(out, radiansToDegrees(object.transform.local.rotation));
        out << " scale=";
        appendVec3(out, object.transform.local.scale);
        out << "\n";

        if (object.hasMeshRenderer) {
            out << "  mesh visible=" << (object.meshRenderer.visible ? "true" : "false")
                << " handle=" << object.meshRenderer.meshHandle << "\n";
            Mesh* mesh = engine.resources().getMesh(object.meshRenderer.meshHandle);
            if (mesh && mesh->bounds().valid) {
                appendWorldBounds(out, mesh->bounds(), object.transform);
            } else {
                out << "  meshBounds valid=false\n";
            }
        }
        if (object.hasRigidBody) {
            out << "  rigidBody bodyId=" << object.rigidBody.bodyId
                << " syncTransform=" << (object.rigidBody.syncTransform ? "true" : "false")
                << " frozen=" << (object.rigidBody.frozen ? "true" : "false") << "\n";
        }
        if (object.hasLight) {
            out << "  light enabled=" << (object.light.enabled ? "true" : "false")
                << " direction=";
            appendVec3(out, object.light.direction);
            out << " color=";
            appendVec3(out, object.light.color);
            out << " intensity=" << object.light.intensity
                << " ambient=" << object.light.ambient << "\n";
        }
        if (object.hasCamera) {
            out << "  camera enabled=" << (object.camera.enabled ? "true" : "false")
                << " primary=" << (object.camera.primary ? "true" : "false")
                << " fovDeg=" << object.camera.fovY * 180.0f / kAiPi
                << " near=" << object.camera.zNear
                << " far=" << object.camera.zFar << "\n";
        }
        if (object.hasScript) {
            out << "  script module=\"" << object.script.moduleName
                << "\" path=\"" << object.script.scriptPath
                << "\" initialized=" << (object.script.initialized ? "true" : "false") << "\n";
        }
    }

    return out.str();
}

bool GuiLayer::applyAiActions(const std::string& response, std::string& status) {
    std::string actionsJson = extractRealCoreActionsBlock(response);
    if (actionsJson.empty()) {
        status = "No engine actions found in AI response.";
        return false;
    }

    std::vector<std::string> actions = splitJsonObjects(actionsJson);
    if (actions.empty()) {
        status = "No valid AI actions found.";
        return false;
    }

    auto& engine = Engine::instance();
    auto& scene = engine.scene();

    int applied = 0;
    int skipped = 0;
    std::string deferredGlobalAction;
    std::string deferredGlobalPath;
    bool deferredGlobalFlag = false;
    bool hasDeferredGlobalFlag = false;

    auto deferGlobalAction = [&](const std::string& action, const std::string& actionJson) {
        if (!deferredGlobalAction.empty()) {
            skipped++;
            return;
        }
        deferredGlobalAction = action;
        jsonReadStringField(actionJson, "path", deferredGlobalPath);
        hasDeferredGlobalFlag = jsonReadBoolField(actionJson, "enabled", deferredGlobalFlag);
        applied++;
    };

    auto findObject = [&](const std::string& actionJson, SceneObject*& object, SceneEntity& entity) {
        entity = InvalidSceneEntity;
        object = nullptr;
        if (!jsonReadEntityField(actionJson, "id", entity)) {
            skipped++;
            return false;
        }

        object = scene.getObject(entity);
        if (!object || !object->active) {
            skipped++;
            return false;
        }
        return true;
    };

    auto removeRigidBody = [&](SceneObject& object) {
        if (object.hasRigidBody && object.rigidBody.bodyId != PhysicsWorld::InvalidBodyId) {
            engine.physics().removeBody(object.rigidBody.bodyId);
        }
        scene.removeRigidBody(object.id);
    };

    auto applyRigidBodyFields = [&](SceneObject& object, const std::string& actionJson) {
        if (!object.hasRigidBody || object.rigidBody.bodyId == PhysicsWorld::InvalidBodyId) {
            return false;
        }

        bool changed = false;
        bool boolValue = false;
        if (jsonReadBoolField(actionJson, "syncTransform", boolValue)) {
            object.rigidBody.syncTransform = boolValue;
            changed = true;
        }
        if (jsonReadBoolField(actionJson, "frozen", boolValue)) {
            object.rigidBody.frozen = boolValue;
            engine.physics().setFrozen(object.rigidBody.bodyId, object.rigidBody.frozen);
            if (object.rigidBody.frozen) {
                engine.physics().resetMotion(object.rigidBody.bodyId);
            }
            changed = true;
        }

        Vec3 value;
        if (jsonReadVec3Field(actionJson, "velocity", value)) {
            engine.physics().setLinearVelocity(object.rigidBody.bodyId, value);
            changed = true;
        }
        return changed;
    };

    auto addRigidBody = [&](SceneObject& object, const std::string& actionJson) {
        std::string shape = "Box";
        if (!jsonReadStringField(actionJson, "shape", shape)) {
            jsonReadStringField(actionJson, "type", shape);
        }
        shape = normalizePrimitiveType(shape);

        bool dynamic = true;
        jsonReadBoolField(actionJson, "dynamic", dynamic);

        Vec3 position = object.transform.local.position;
        Vec3 halfExtent = defaultBodyHalfExtent(object);
        jsonReadVec3Field(actionJson, "halfExtent", halfExtent);

        float radius = defaultBodyRadius(object);
        float halfHeight = halfExtent.y;
        jsonReadNumberField(actionJson, "radius", radius);
        jsonReadNumberField(actionJson, "halfHeight", halfHeight);
        position = sanitizePosition(position);
        halfExtent = sanitizePhysicsHalfExtent(halfExtent);
        radius = sanitizePositive(radius);
        halfHeight = sanitizePositive(halfHeight);

        uint32_t bodyId = PhysicsWorld::InvalidBodyId;
        if (shape == "Sphere") {
            bodyId = engine.physics().addSphere(position, radius, dynamic);
        } else if (shape == "Capsule") {
            bodyId = engine.physics().addCapsule(position, halfHeight, radius, dynamic);
        } else if (shape == "Cylinder") {
            bodyId = engine.physics().addCylinder(position, halfHeight, radius, dynamic);
        } else {
            bodyId = engine.physics().addBox(position, halfExtent, dynamic);
        }

        if (bodyId == PhysicsWorld::InvalidBodyId) {
            return false;
        }

        removeRigidBody(object);
        RigidBodyComponent* rigidBody = scene.addRigidBody(object.id, bodyId);
        if (!rigidBody) {
            engine.physics().removeBody(bodyId);
            return false;
        }

        rigidBody->shape = shape == "Sphere" ? RigidBodyComponent::Shape::Sphere :
            shape == "Capsule" ? RigidBodyComponent::Shape::Capsule :
            shape == "Cylinder" ? RigidBodyComponent::Shape::Cylinder :
            RigidBodyComponent::Shape::Box;
        rigidBody->halfExtent = halfExtent;
        rigidBody->radius = radius;
        rigidBody->halfHeight = halfHeight;
        rigidBody->dynamic = dynamic;
        applyRigidBodyFields(object, actionJson);
        return true;
    };

    for (const std::string& actionJson : actions) {
        std::string action;
        if (!jsonReadStringField(actionJson, "action", action)) {
            skipped++;
            continue;
        }

        if (action == "set_transform") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            bool changed = false;
            Vec3 value;
            if (jsonReadVec3Field(actionJson, "position", value)) {
                engine.setEntityPosition(entity, value);
                changed = true;
            }
            if (jsonReadVec3Field(actionJson, "rotationDeg", value)) {
                object->transform.local.rotation = degreesToRadians(value);
                changed = true;
            }
            if (jsonReadVec3Field(actionJson, "scale", value)) {
                object->transform.local.scale = value;
                changed = true;
            }

            if (changed) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "move") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            Vec3 delta;
            if (jsonReadVec3Field(actionJson, "delta", delta)) {
                engine.setEntityPosition(entity, object->transform.local.position + delta);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "rename") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            std::string name;
            if (jsonReadStringField(actionJson, "name", name) && !name.empty()) {
                object->name = name;
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "set_active") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            bool active = true;
            if (jsonReadBoolField(actionJson, "active", active)) {
                object->active = active;
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "set_mesh" || action == "set_mesh_renderer" || action == "set_visible" || action == "set_visibility") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }
            if (!object->hasMeshRenderer) {
                skipped++;
                continue;
            }

            bool changed = false;
            bool visible = true;
            if (jsonReadBoolField(actionJson, "visible", visible) ||
                jsonReadBoolField(actionJson, "enabled", visible)) {
                object->meshRenderer.visible = visible;
                changed = true;
            }

            if (changed) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "add_rigidbody") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (addRigidBody(*object, actionJson)) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "set_rigidbody") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (applyRigidBodyFields(*object, actionJson)) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "remove_rigidbody") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (object->hasRigidBody) {
                removeRigidBody(*object);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "add_light") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            LightComponent* light = scene.addLight(entity);
            if (!light) {
                skipped++;
                continue;
            }

            Vec3 value;
            if (jsonReadVec3Field(actionJson, "direction", value)) {
                light->direction = value;
            }
            if (jsonReadVec3Field(actionJson, "color", value)) {
                light->color = value;
            }
            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "intensity", number)) {
                light->intensity = (std::max)(0.0f, number);
            }
            if (jsonReadNumberField(actionJson, "ambient", number)) {
                light->ambient = (std::max)(0.0f, (std::min)(1.0f, number));
            }
            bool boolValue = true;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                light->enabled = boolValue;
            }
            applied++;
        } else if (action == "remove_light") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (object->hasLight) {
                scene.removeLight(entity);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "add_camera") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            CameraComponent* camera = scene.addCamera(entity);
            if (!camera) {
                skipped++;
                continue;
            }

            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "fovDeg", number)) {
                number = (std::max)(1.0f, (std::min)(170.0f, number));
                camera->fovY = number * kAiPi / 180.0f;
            }
            if (jsonReadNumberField(actionJson, "near", number)) {
                camera->zNear = (std::max)(0.001f, number);
            }
            if (jsonReadNumberField(actionJson, "far", number)) {
                camera->zFar = (std::max)(camera->zNear + 0.001f, number);
            }
            bool boolValue = true;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                camera->enabled = boolValue;
            }
            if (jsonReadBoolField(actionJson, "primary", boolValue) && boolValue) {
                makePrimaryCamera(scene, entity);
            }
            applied++;
        } else if (action == "remove_camera") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (object->hasCamera) {
                scene.removeCamera(entity);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "remove_script") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            if (object->hasScript) {
                engine.detachScript(entity);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "set_parent") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            float parentValue = 0.0f;
            if (jsonReadNumberField(actionJson, "parent", parentValue) ||
                jsonReadNumberField(actionJson, "parentId", parentValue)) {
                SceneEntity parent = parentValue <= 0.0f ? InvalidSceneEntity : (SceneEntity)parentValue;
                if (scene.setParent(entity, parent)) {
                    applied++;
                } else {
                    skipped++;
                }
            } else {
                skipped++;
            }
        } else if (action == "create_primitive") {
            std::string type = "Box";
            jsonReadStringField(actionJson, "type", type);
            SceneEntity entity = engine.createPrimitive(normalizePrimitiveType(type));
            SceneObject* object = scene.getObject(entity);
            if (!object || !object->active) {
                skipped++;
                continue;
            }

            std::string name;
            if (jsonReadStringField(actionJson, "name", name) && !name.empty()) {
                object->name = name;
            }
            Vec3 value;
            if (jsonReadVec3Field(actionJson, "position", value)) {
                engine.setEntityPosition(entity, value);
            }
            if (jsonReadVec3Field(actionJson, "rotationDeg", value)) {
                object->transform.local.rotation = degreesToRadians(value);
            }
            if (jsonReadVec3Field(actionJson, "scale", value)) {
                object->transform.local.scale = value;
            }
            applied++;
        } else if (action == "create_empty") {
            std::string name = "Empty Object";
            jsonReadStringField(actionJson, "name", name);
            SceneEntity entity = scene.createObject(name.empty() ? "Empty Object" : name);
            if (SceneObject* object = scene.getObject(entity)) {
                Vec3 value;
                if (jsonReadVec3Field(actionJson, "position", value)) {
                    engine.setEntityPosition(entity, value);
                }
                if (jsonReadVec3Field(actionJson, "rotationDeg", value)) {
                    object->transform.local.rotation = degreesToRadians(value);
                }
                if (jsonReadVec3Field(actionJson, "scale", value)) {
                    object->transform.local.scale = value;
                }
                engine.selectEntity(entity);
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "create_camera") {
            std::string name = "Camera";
            jsonReadStringField(actionJson, "name", name);
            SceneEntity entity = scene.createObject(name.empty() ? "Camera" : name);
            SceneObject* object = scene.getObject(entity);
            CameraComponent* camera = scene.addCamera(entity);
            if (!object || !camera) {
                skipped++;
                continue;
            }

            Vec3 value;
            if (jsonReadVec3Field(actionJson, "position", value)) {
                engine.setEntityPosition(entity, value);
            }
            if (jsonReadVec3Field(actionJson, "rotationDeg", value)) {
                object->transform.local.rotation = degreesToRadians(value);
            }
            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "fovDeg", number)) {
                number = (std::max)(1.0f, (std::min)(170.0f, number));
                camera->fovY = number * kAiPi / 180.0f;
            }
            if (jsonReadNumberField(actionJson, "near", number)) {
                camera->zNear = (std::max)(0.001f, number);
            }
            if (jsonReadNumberField(actionJson, "far", number)) {
                camera->zFar = (std::max)(camera->zNear + 0.001f, number);
            }
            bool boolValue = false;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                camera->enabled = boolValue;
            }
            if (jsonReadBoolField(actionJson, "primary", boolValue)) {
                if (boolValue) {
                    makePrimaryCamera(scene, entity);
                } else {
                    camera->primary = false;
                }
            } else {
                makePrimaryCamera(scene, entity);
            }
            engine.selectEntity(entity);
            applied++;
        } else if (action == "create_light") {
            std::string name = "Directional Light";
            jsonReadStringField(actionJson, "name", name);
            SceneEntity entity = scene.createObject(name.empty() ? "Directional Light" : name);
            LightComponent* light = scene.addLight(entity);
            if (!light) {
                skipped++;
                continue;
            }

            Vec3 value;
            if (jsonReadVec3Field(actionJson, "position", value)) {
                engine.setEntityPosition(entity, value);
            }
            if (jsonReadVec3Field(actionJson, "direction", value)) {
                light->direction = value;
            }
            if (jsonReadVec3Field(actionJson, "color", value)) {
                light->color = value;
            }
            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "intensity", number)) {
                light->intensity = (std::max)(0.0f, number);
            }
            if (jsonReadNumberField(actionJson, "ambient", number)) {
                light->ambient = (std::max)(0.0f, (std::min)(1.0f, number));
            }
            bool boolValue = true;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                light->enabled = boolValue;
            }
            engine.selectEntity(entity);
            applied++;
        } else if (action == "set_light") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }
            if (!object->hasLight) {
                skipped++;
                continue;
            }

            bool changed = false;
            Vec3 value;
            if (jsonReadVec3Field(actionJson, "direction", value)) {
                object->light.direction = value;
                changed = true;
            }
            if (jsonReadVec3Field(actionJson, "color", value)) {
                object->light.color = value;
                changed = true;
            }
            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "intensity", number)) {
                object->light.intensity = (std::max)(0.0f, number);
                changed = true;
            }
            if (jsonReadNumberField(actionJson, "ambient", number)) {
                object->light.ambient = (std::max)(0.0f, (std::min)(1.0f, number));
                changed = true;
            }
            bool boolValue = true;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                object->light.enabled = boolValue;
                changed = true;
            }
            if (changed) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "set_camera") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }
            if (!object->hasCamera) {
                skipped++;
                continue;
            }

            bool changed = false;
            float number = 0.0f;
            if (jsonReadNumberField(actionJson, "fovDeg", number)) {
                number = (std::max)(1.0f, (std::min)(170.0f, number));
                object->camera.fovY = number * kAiPi / 180.0f;
                changed = true;
            }
            if (jsonReadNumberField(actionJson, "near", number)) {
                object->camera.zNear = (std::max)(0.001f, number);
                changed = true;
            }
            if (jsonReadNumberField(actionJson, "far", number)) {
                object->camera.zFar = (std::max)(object->camera.zNear + 0.001f, number);
                changed = true;
            }
            bool boolValue = true;
            if (jsonReadBoolField(actionJson, "enabled", boolValue)) {
                object->camera.enabled = boolValue;
                changed = true;
            }
            if (jsonReadBoolField(actionJson, "primary", boolValue)) {
                if (boolValue) {
                    makePrimaryCamera(scene, entity);
                } else {
                    object->camera.primary = false;
                }
                changed = true;
            }
            if (changed) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "attach_script") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (!findObject(actionJson, object, entity)) {
                continue;
            }

            std::string path;
            if (jsonReadStringField(actionJson, "path", path) && !path.empty() && engine.attachScript(entity, path)) {
                applied++;
            } else {
                skipped++;
            }
        } else if (action == "select") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (findObject(actionJson, object, entity)) {
                engine.selectEntity(entity);
                applied++;
            }
        } else if (action == "focus") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (findObject(actionJson, object, entity)) {
                engine.focusOnEntity(entity);
                applied++;
            }
        } else if (action == "delete") {
            SceneEntity entity = InvalidSceneEntity;
            SceneObject* object = nullptr;
            if (findObject(actionJson, object, entity)) {
                engine.deleteEntity(entity);
                applied++;
            }
        } else if (action == "play") {
            deferGlobalAction(action, actionJson);
        } else if (action == "pause") {
            deferGlobalAction(action, actionJson);
        } else if (action == "stop") {
            deferGlobalAction(action, actionJson);
        } else if (action == "new_scene") {
            deferGlobalAction(action, actionJson);
        } else if (action == "save_scene") {
            deferGlobalAction(action, actionJson);
        } else if (action == "load_scene") {
            deferGlobalAction(action, actionJson);
        } else if (action == "export_game") {
            deferGlobalAction(action, actionJson);
        } else if (action == "use_scene_camera") {
            deferGlobalAction(action, actionJson);
        } else {
            skipped++;
        }
    }

    if (!deferredGlobalAction.empty()) {
        bool ok = true;
        if (deferredGlobalAction == "play") {
            engine.play();
        } else if (deferredGlobalAction == "pause") {
            engine.pause();
        } else if (deferredGlobalAction == "stop") {
            engine.stop();
        } else if (deferredGlobalAction == "new_scene") {
            engine.newScene();
        } else if (deferredGlobalAction == "save_scene") {
            ok = !deferredGlobalPath.empty() ? engine.saveScene(deferredGlobalPath) : engine.saveScene();
        } else if (deferredGlobalAction == "load_scene") {
            ok = !deferredGlobalPath.empty() && engine.loadScene(deferredGlobalPath);
        } else if (deferredGlobalAction == "export_game") {
            std::string exportedExe;
            ok = engine.exportGame(exportedExe);
            if (ok) {
                m_statusMessage = "Exported game: " + exportedExe;
            }
        } else if (deferredGlobalAction == "use_scene_camera") {
            engine.setUseSceneCamera(hasDeferredGlobalFlag ? deferredGlobalFlag : true);
        }

        if (!ok) {
            applied--;
            skipped++;
        }
    }

    if (applied <= 0) {
        status = skipped > 0 ? "No AI actions were applied." : "AI returned no supported actions.";
        return false;
    }

    status = "Applied " + std::to_string(applied) + " AI action";
    if (applied != 1) {
        status += "s";
    }
    if (skipped > 0) {
        status += ", skipped " + std::to_string(skipped);
    }
    status += ".";
    return true;
}

void GuiLayer::showAiAssistant(bool embedded) {
    if (embedded) {
        ImGui::BeginChild("##ai-assistant", ImVec2(320.0f, 0.0f), true);
    }

    ImGui::TextUnformatted("AI Assistant");
    ImGui::Separator();
    ImGui::TextUnformatted("Ollama");
    ImGui::InputText("URL", m_ollamaBaseUrl, sizeof(m_ollamaBaseUrl));

    const char* modePreview = m_aiEngineMode ? "Engine" : "Script";
    if (ImGui::BeginCombo("Mode", modePreview)) {
        if (ImGui::Selectable("Engine", m_aiEngineMode)) {
            m_aiEngineMode = true;
        }
        if (ImGui::Selectable("Script", !m_aiEngineMode)) {
            m_aiEngineMode = false;
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Refresh Models")) {
        m_aiStatus = "Loading Ollama models...";
        m_ollamaModels.clear();
        m_ollamaModelIndex = -1;

        std::string error;
        if (OllamaClient::listModels(m_ollamaBaseUrl, m_ollamaModels, error)) {
            m_ollamaModelIndex = m_ollamaModels.empty() ? -1 : 0;
            m_aiStatus = "Models loaded: " + std::to_string(m_ollamaModels.size());
        } else {
            m_aiStatus = error;
        }
    }

    const char* modelPreview = "No model";
    if (m_ollamaModelIndex >= 0 && m_ollamaModelIndex < (int)m_ollamaModels.size()) {
        modelPreview = m_ollamaModels[(size_t)m_ollamaModelIndex].displayName.c_str();
    }

    if (ImGui::BeginCombo("Model", modelPreview)) {
        for (int i = 0; i < (int)m_ollamaModels.size(); i++) {
            bool selected = i == m_ollamaModelIndex;
            std::string label = m_ollamaModels[(size_t)i].displayName + "##" + m_ollamaModels[(size_t)i].name;
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_ollamaModelIndex = i;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::TextUnformatted("Request");
    ImGui::InputTextMultiline("##ai-prompt", m_aiPromptBuf, sizeof(m_aiPromptBuf), ImVec2(-1.0f, 92.0f));

    const bool hasModel = m_ollamaModelIndex >= 0 && m_ollamaModelIndex < (int)m_ollamaModels.size();
    const bool canAsk = hasModel && m_aiPromptBuf[0] != '\0' && (m_aiEngineMode || !m_codePath.empty());
    ImGui::BeginDisabled(!canAsk);
    if (ImGui::Button("Ask Ollama")) {
        m_aiStatus = "Asking Ollama...";
        m_aiResponse.clear();

        std::string error;
        const std::string& model = m_ollamaModels[(size_t)m_ollamaModelIndex].name;
        bool ok = false;
        if (m_aiEngineMode) {
            ok = OllamaClient::generateEngineHelp(
                m_ollamaBaseUrl,
                model,
                m_aiPromptBuf,
                buildAiEngineContext(),
                currentCodeText(),
                m_aiResponse,
                error);
        } else {
            ok = OllamaClient::generateScriptHelp(
                m_ollamaBaseUrl,
                model,
                m_aiPromptBuf,
                currentCodeText(),
                m_aiResponse,
                error);
        }

        if (ok) {
            m_aiStatus = "Ollama response ready.";
        } else {
            m_aiStatus = error;
        }
    }
    ImGui::EndDisabled();

    if (!m_aiStatus.empty()) {
        ImGui::TextWrapped("%s", m_aiStatus.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Response");

    ImGui::BeginChild("##ai-response", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f), true);
    if (m_aiResponse.empty()) {
        ImGui::TextDisabled("No response yet.");
    } else {
        ImGui::TextWrapped("%s", m_aiResponse.c_str());
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(m_aiResponse.empty() || !m_aiEngineMode);
    if (ImGui::Button("Apply Actions")) {
        try {
            applyAiActions(m_aiResponse, m_aiStatus);
        } catch (const std::exception& e) {
            m_aiStatus = std::string("Failed to apply AI actions: ") + e.what();
        } catch (...) {
            m_aiStatus = "Failed to apply AI actions: unknown error.";
        }
        m_statusMessage = m_aiStatus;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(m_aiResponse.empty() || m_codePath.empty());
    if (ImGui::Button("Insert Code")) {
        applyCodeSuggestionToEditor(m_aiResponse, m_aiStatus);
        m_codeStatus = m_aiStatus;
    }
    ImGui::EndDisabled();

    if (embedded) {
        ImGui::EndChild();
    }
}

void GuiLayer::showAiAssistantWindow() {
    ImGui::SetNextWindowSize(ImVec2(430.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AI Assistant", &m_showAiWindow)) {
        ImGui::End();
        return;
    }

    showAiAssistant(false);
    ImGui::End();
}

void GuiLayer::showCodeEditor() {
    auto& engine = Engine::instance();

    ImGui::BeginDisabled(m_codePath.empty());
    if (ImGui::Button("Save")) {
        saveCodeEditor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        openTextEditor(m_codePath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Script")) {
        if (m_codeDirty && !saveCodeEditor()) {
            m_statusMessage = m_codeStatus;
        } else if (engine.scriptEngine().loadScript(m_codePath)) {
            m_codeStatus = "Loaded script: " + m_codeName;
            m_statusMessage = m_codeStatus;
        } else {
            m_codeStatus = "Failed to load script: " + m_codeName;
            m_statusMessage = m_codeStatus;
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("AI")) {
        m_showAiAssistant = !m_showAiAssistant;
    }

    ImGui::SameLine();
    if (m_codeDirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.30f, 1.0f), "Unsaved");
    } else if (!m_codeName.empty()) {
        ImGui::TextDisabled("%s", m_codeName.c_str());
    } else {
        ImGui::TextDisabled("No script selected");
    }

    if (!m_codeStatus.empty()) {
        ImGui::TextColored(ImVec4(0.2275f, 0.4784f, 0.9961f, 1.0f), "%s", m_codeStatus.c_str());
    }

    ImGui::Separator();

    if (m_codePath.empty()) {
        ImGui::TextDisabled("Select a .as or .lua script in Files.");
    } else {
        size_t textLen = std::strlen(m_codeBuffer.data());
        if (m_codeBuffer.size() - textLen < 1024) {
            m_codeBuffer.resize(m_codeBuffer.size() + 4096, '\0');
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;

        ImVec2 editorSize = ImGui::GetContentRegionAvail();
        if (m_showAiAssistant) {
            editorSize.x = (std::max)(160.0f, editorSize.x - 328.0f);
        }
        if (ImGui::InputTextMultiline(
            "##code-editor-buffer",
            m_codeBuffer.data(),
            m_codeBuffer.size(),
            editorSize,
            flags)) {
            m_codeDirty = true;
        }

        if (m_showAiAssistant) {
            ImGui::SameLine();
            showAiAssistant(true);
        }
    }
}

void GuiLayer::showContentBrowser() {
    auto& engine = Engine::instance();

    if (engine.hasProject()) {
        std::string assetsPath = (fs::path(engine.projectPath()) / "Assets").string();
        if (m_fileBrowser.rootPath() != assetsPath) {
            m_fileBrowser.setRoot(assetsPath);
            m_fileBrowser.setCurrentPath(assetsPath);
        }
    } else if (m_fileBrowser.rootPath().empty()) {
        m_fileBrowser.setRoot(fs::current_path().string());
    }

    if (ImGui::BeginTabBar("ContentTabs")) {
        if (ImGui::BeginTabItem("Files")) {
            if (!m_statusMessage.empty()) {
                ImGui::TextColored(ImVec4(0.2275f, 0.4784f, 0.9961f, 1.0f), "%s", m_statusMessage.c_str());
            }

            m_fileBrowser.showContents();

            if (m_fileBrowser.selectionConfirmed()) {
                std::string ext = fs::path(m_fileBrowser.selectedPath()).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](char c) { return (char)std::tolower((unsigned char)c); });

                const bool isTextFile =
                    ext == ".as" || ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml" ||
                    ext == ".ini" || ext == ".cfg" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
                    ext == ".hpp" || ext == ".cs" || ext == ".lua" || ext == ".js" || ext == ".glsl" ||
                    ext == ".hlsl" || ext == ".vert" || ext == ".frag";
                const bool isModelFile =
                    ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".zip";

                if (ext == ".rcscene") {
                    if (engine.loadScene(m_fileBrowser.selectedPath())) {
                        m_statusMessage = "Scene loaded: " + m_fileBrowser.selectedName();
                        m_showStartupDialog = false;
                    } else {
                        m_statusMessage = engine.lastProjectError();
                    }
                } else if (isTextFile) {
                    if (m_codeDirty && m_fileBrowser.selectedPath() != m_codePath) {
                        m_statusMessage = "Save or reload the current text file before opening another.";
                    } else if (openTextEditor(m_fileBrowser.selectedPath())) {
                        m_statusMessage = "Text opened: " + m_fileBrowser.selectedName();
                    } else {
                        m_statusMessage = "Failed to open text file: " + m_fileBrowser.selectedName();
                    }
                } else if (isModelFile) {
                    SceneEntity entity = engine.importRenderItem(m_fileBrowser.selectedPath());
                    const SceneObject* object = engine.scene().getObject(entity);
                    if (object && object->hasMeshRenderer) {
                        m_loadedAssets.push_back({ m_fileBrowser.selectedName(), object->meshRenderer.meshHandle });
                        engine.setPreviewMesh(object->meshRenderer.meshHandle);
                        m_lastPreviewPath = m_fileBrowser.selectedPath();
                        m_statusMessage = engine.hasProject()
                            ? "Imported to project: " + m_fileBrowser.selectedName()
                            : "Loaded: " + m_fileBrowser.selectedName();
                    } else {
                        m_statusMessage = "Failed to load: " + m_fileBrowser.selectedName();
                    }
                } else {
                    m_statusMessage = "Selected file: " + m_fileBrowser.selectedName();
                }
                m_fileBrowser.clearSelectionConfirmed();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Content")) {
            const std::string& previewPath = m_fileBrowser.selectedPath();
            if (!previewPath.empty() && previewPath != m_lastPreviewPath) {
                std::string ext = fs::path(previewPath).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](char c) { return (char)std::tolower((unsigned char)c); });
                const bool isTextFile =
                    ext == ".as" || ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".xml" ||
                    ext == ".ini" || ext == ".cfg" || ext == ".cpp" || ext == ".c" || ext == ".h" ||
                    ext == ".hpp" || ext == ".cs" || ext == ".lua" || ext == ".js" || ext == ".glsl" ||
                    ext == ".hlsl" || ext == ".vert" || ext == ".frag";
                const bool isModelFile =
                    ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".zip";

                if (isTextFile) {
                    if ((!m_codeDirty || m_codePath.empty()) && previewPath != m_codePath) {
                        openTextEditor(previewPath);
                    }
                } else if (isModelFile) {
                    MeshHandle ph = engine.resources().loadMesh(previewPath);
                    if (ph != 0) {
                        engine.setPreviewMesh(ph);
                        m_lastPreviewPath = previewPath;
                    }
                }
            }

            float availW = ImGui::GetContentRegionAvail().x;
            float previewW = (std::min)(240.0f, availW * 0.32f);
            float listsW = availW - previewW - ImGui::GetStyle().ItemSpacing.x;

            ImGui::BeginChild("##content-lists", ImVec2(listsW, 0.0f), false);

            if (ImGui::CollapsingHeader("Scene Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
                int meshObjectCount = 0;
                for (const auto& object : engine.scene().objects()) {
                    if (object.active && object.hasMeshRenderer) {
                        meshObjectCount++;
                    }
                }

                ImGui::Text("Objects: %d", meshObjectCount);
                ImGui::BeginChild("##scene-meshes", ImVec2(0, 0.0f), true);
                if (meshObjectCount == 0) {
                    ImGui::TextDisabled("No mesh objects in scene.");
                }
                for (const auto& object : engine.scene().objects()) {
                    if (!object.active || !object.hasMeshRenderer) {
                        continue;
                    }

                    Mesh* mesh = engine.resources().getMesh(object.meshRenderer.meshHandle);
                    ImGui::PushID((int)object.id);
                    bool selected = engine.isEntitySelected(object.id);
                    std::string label = object.name + "  #" + std::to_string(object.id);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        if (ImGui::GetIO().KeyCtrl) {
                            engine.toggleEntitySelection(object.id);
                        } else {
                            engine.selectEntity(object.id);
                        }
                        engine.setPreviewMesh(object.meshRenderer.meshHandle);
                    }
                    if (ImGui::IsItemHovered()) {
                        engine.setPreviewMesh(object.meshRenderer.meshHandle);
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            engine.focusOnEntity(object.id);
                        }
                    }
                    if (mesh) {
                        ImGui::SameLine((std::max)(120.0f, ImGui::GetWindowContentRegionMax().x - 78.0f));
                        ImGui::TextDisabled("%d tris", mesh->indexCount() / 3);
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }

            ImGui::EndChild();
            ImGui::SameLine();

            ImGui::BeginChild("##preview-panel", ImVec2(previewW, 0.0f), true);
            ImGui::TextUnformatted("Preview");
            ImGui::Separator();
            if (engine.previewMesh() != 0) {
                uint64_t texID = engine.renderer().previewImTextureID();
                if (texID != 0) {
                    float availImgW = ImGui::GetContentRegionAvail().x;
                    float availImgH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4.0f;
                    float imgSize = (std::min)(availImgW, (std::max)(60.0f, availImgH));
                    ImGui::Image((ImTextureID)texID, ImVec2(imgSize, imgSize));
                }

                Mesh* mesh = engine.resources().getMesh(engine.previewMesh());
                if (mesh) {
                    ImGui::Text("Verts: %d", mesh->vertexCount());
                    ImGui::Text("Tris:  %d", mesh->indexCount() / 3);
                }
            } else {
                ImGui::TextDisabled("Select a model or scene mesh.");
            }
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}
