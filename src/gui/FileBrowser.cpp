#include <gui/FileBrowser.h>
#include <platform/NativeDialogs.h>
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace fs = std::filesystem;

static bool hasExtension(const std::string& name, const std::string& filter) {
    if (filter.empty()) return true;

    std::string ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](char c) { return (char)std::tolower((unsigned char)c); });

    if (ext == filter) return true;

    size_t pos = 0;
    while (pos < filter.size()) {
        size_t next = filter.find(';', pos);
        std::string token = filter.substr(pos, next - pos);
        if (token == ext) return true;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return false;
}

static const char* fileIcon(const std::string& name) {
    if (fs::is_directory(name)) return ">";
    std::string ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](char c) { return (char)std::tolower((unsigned char)c); });
    if (ext == ".as") return "S";
    if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp") return "C";
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") return "M";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") return "I";
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return "A";
    if (ext == ".txt" || ext == ".md") return "T";
    if (ext == ".rcscene") return "S";
    return ".";
}

static bool isSimpleFileName(const char* name) {
    if (!name || name[0] == '\0') return false;

    fs::path p(name);
    if (p.has_parent_path() || p.is_absolute()) return false;

    std::string filename = p.filename().string();
    return filename == name && filename != "." && filename != "..";
}

FileBrowser::FileBrowser() {
    m_rootPath = fs::current_path().string();
    m_currentPath = m_rootPath;
    refresh();
}

void FileBrowser::setRoot(const std::string& rootPath) {
    m_rootPath = rootPath;
    if (m_currentPath.find(m_rootPath) != 0 || m_rootPath.empty()) {
        m_currentPath = m_rootPath;
    }
    refresh();
}

void FileBrowser::refresh() {
    m_entries.clear();
    m_isDir.clear();
    m_selected.clear();
    m_selectedPath.clear();
    m_selectedName.clear();

    try {
        for (auto& entry : fs::directory_iterator(m_currentPath)) {
            std::string name = entry.path().filename().string();
            bool isDir = entry.is_directory();
            if (!isDir && !hasExtension(name, m_filter)) continue;
            m_entries.push_back(name);
            m_isDir.push_back(isDir);
        }
    } catch (...) {}
}

void FileBrowser::navigateTo(const std::string& path) {
    m_currentPath = path;
    refresh();
}

void FileBrowser::confirmExternalFile(const std::string& path) {
    if (path.empty()) return;

    fs::path filePath(path);
    m_currentPath = filePath.parent_path().string();
    refresh();
    m_selected = filePath.filename().string();
    m_selectedPath = filePath.lexically_normal().string();
    m_selectedName = m_selected;
    m_selectionConfirmed = true;
}

bool FileBrowser::createFolder(const std::string& name) {
    if (name.empty()) return false;
    std::error_code ec;
    return fs::create_directory(fs::path(m_currentPath) / name, ec);
}

bool FileBrowser::createScript(const std::string& name) {
    if (name.empty()) return false;
    std::string fullName = name;
    fs::path namePath(fullName);
    std::string ext = namePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](char c) { return (char)std::tolower((unsigned char)c); });
    if (ext.empty()) {
        fullName += ".as";
        ext = ".as";
    }

    fs::path filePath = fs::path(m_currentPath) / fullName;
    if (fs::exists(filePath)) return false;

    FILE* f = nullptr;
    fopen_s(&f, filePath.string().c_str(), "w");
    if (!f) return false;

    std::string content;
    if (ext == ".lua") {
        content =
            "-- " + fullName + "\n\n"
            "function init()\n"
            "end\n\n"
            "function update(dt)\n"
            "    -- Example: scene:load(\"Game\")\n"
            "end\n\n"
            "function destroy()\n"
            "end\n";
    } else {
        content =
            "// " + fullName + "\n\n"
            "void init() {\n"
            "}\n\n"
            "void update(float dt) {\n"
            "    // Example: loadScene(\"Game\");\n"
            "}\n\n"
            "void destroy() {\n"
            "}\n";
    }
    fwrite(content.c_str(), 1, content.size(), f);
    fclose(f);
    return true;
}

void FileBrowser::showAddressBar() {
    fs::path current(m_currentPath);
    fs::path root(m_rootPath);
    std::vector<std::string> segments;

    for (auto it = current.begin(); it != current.end(); ++it) {
        segments.push_back(it->string());
    }

    float availX = ImGui::GetContentRegionAvail().x;
    float startX = ImGui::GetCursorPosX();

    for (size_t i = 0; i < segments.size(); i++) {
        bool isClickable = true;
        std::string seg = segments[i];

        if (seg.size() > 1 && seg.back() == ':') seg += '\\';

        ImVec2 textSize = ImGui::CalcTextSize(seg.c_str());
        float nextX = startX + textSize.x + ImGui::GetStyle().ItemSpacing.x;
        if (nextX > availX && i > 0) {
            ImGui::TextUnformatted("...");
            ImGui::SameLine();
            startX = ImGui::GetCursorPosX();
            continue;
        }

        if (ImGui::SmallButton(seg.c_str())) {
            fs::path p;
            for (size_t j = 0; j <= i; j++) {
                p /= segments[j];
            }
            navigateTo(p.lexically_normal().string());
        }

        if (i < segments.size() - 1) {
            ImGui::SameLine();
            ImGui::TextUnformatted("/");
            ImGui::SameLine();
        }

        startX = ImGui::GetCursorPosX();
    }
}

void FileBrowser::showContextMenu() {
    bool openNewFolder = false;
    bool openNewScript = false;

    if (ImGui::BeginPopupContextWindow("##fb-context", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("New Folder")) {
            openNewFolder = true;
        }
        if (ImGui::MenuItem("New Script")) {
            openNewScript = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", nullptr, false, !m_selected.empty())) {
            requestRename(m_selected);
        }
        if (ImGui::MenuItem("Delete", nullptr, false, !m_selected.empty())) {
            requestDelete(m_selected);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Refresh")) {
            refresh();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer")) {
            fs::path p(m_currentPath);
            std::string cmd = "explorer \"" + p.lexically_normal().string() + "\"";
            system(cmd.c_str());
        }
        ImGui::EndPopup();
    }

    if (openNewFolder) {
        m_showNewFolder = true;
        m_showNewScript = false;
        std::memset(m_newItemName, 0, sizeof(m_newItemName));
        m_openNewFolderPopup = true;
    }
    if (openNewScript) {
        m_showNewScript = true;
        m_showNewFolder = false;
        std::memset(m_newItemName, 0, sizeof(m_newItemName));
        m_openNewScriptPopup = true;
    }
}

int FileBrowser::showNewItemPopup() {
    int result = 0;

    if (m_openNewFolderPopup) {
        ImGui::OpenPopup("New Folder");
        m_openNewFolderPopup = false;
    }
    if (m_openNewScriptPopup) {
        ImGui::OpenPopup("New Script");
        m_openNewScriptPopup = false;
    }

    if (m_showNewFolder) {
        ImGui::SetNextWindowSize(ImVec2(300, 0));
        if (ImGui::BeginPopupModal("New Folder", &m_showNewFolder, ImGuiWindowFlags_NoResize)) {
            ImGui::Text("Folder name:");
            bool enter = ImGui::InputText("##new-folder-name", m_newItemName, sizeof(m_newItemName), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            bool createClicked = ImGui::Button("Create") || enter;
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showNewFolder = false;
                ImGui::CloseCurrentPopup();
            }
            if (createClicked && std::strlen(m_newItemName) > 0) {
                if (createFolder(m_newItemName)) {
                    refresh();
                    result = 1;
                }
                m_showNewFolder = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    if (m_showNewScript) {
        ImGui::SetNextWindowSize(ImVec2(300, 0));
        if (ImGui::BeginPopupModal("New Script", &m_showNewScript, ImGuiWindowFlags_NoResize)) {
            ImGui::Text("Script name:");
            bool enter = ImGui::InputText("##new-script-name", m_newItemName, sizeof(m_newItemName), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            bool createClicked = ImGui::Button("Create") || enter;
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showNewScript = false;
                ImGui::CloseCurrentPopup();
            }
            if (createClicked && std::strlen(m_newItemName) > 0) {
                if (createScript(m_newItemName)) {
                    refresh();
                    result = 2;
                }
                m_showNewScript = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    return result;
}

void FileBrowser::requestRename(const std::string& name) {
    if (name.empty()) return;

    m_renameOldName = name;
    std::memset(m_renameItemName, 0, sizeof(m_renameItemName));
    std::snprintf(m_renameItemName, sizeof(m_renameItemName), "%s", name.c_str());
    m_showRenameItem = true;
    m_openRenameItemPopup = true;
    m_fileOperationStatus.clear();
}

void FileBrowser::requestDelete(const std::string& name) {
    if (name.empty()) return;

    m_deleteItemName = name;
    m_showDeleteItem = true;
    m_openDeleteItemPopup = true;
    m_fileOperationStatus.clear();
}

void FileBrowser::showFileOperationPopups() {
    if (m_openRenameItemPopup) {
        ImGui::OpenPopup("Rename Item");
        m_openRenameItemPopup = false;
    }
    if (m_openDeleteItemPopup) {
        ImGui::OpenPopup("Delete Item");
        m_openDeleteItemPopup = false;
    }

    if (m_showRenameItem) {
        ImGui::SetNextWindowSize(ImVec2(340, 0));
        if (ImGui::BeginPopupModal("Rename Item", &m_showRenameItem, ImGuiWindowFlags_NoResize)) {
            ImGui::Text("Old name: %s", m_renameOldName.c_str());
            bool enter = ImGui::InputText("New name", m_renameItemName, sizeof(m_renameItemName), ImGuiInputTextFlags_EnterReturnsTrue);
            if (!m_fileOperationStatus.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", m_fileOperationStatus.c_str());
            }
            ImGui::Spacing();

            bool renameClicked = ImGui::Button("Rename") || enter;
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showRenameItem = false;
                ImGui::CloseCurrentPopup();
            }

            if (renameClicked) {
                if (!isSimpleFileName(m_renameItemName)) {
                    m_fileOperationStatus = "Enter a file or folder name, not a path.";
                    ImGui::EndPopup();
                    return;
                }

                fs::path oldPath = fs::path(m_currentPath) / m_renameOldName;
                fs::path newPath = fs::path(m_currentPath) / m_renameItemName;

                std::error_code ec;
                if (fs::exists(newPath, ec) && oldPath != newPath) {
                    m_fileOperationStatus = "That name already exists.";
                    ImGui::EndPopup();
                    return;
                }

                if (oldPath != newPath) {
                    fs::rename(oldPath, newPath, ec);
                    if (!ec) {
                        refresh();
                        m_selected = newPath.filename().string();
                        m_selectedPath = newPath.lexically_normal().string();
                        m_selectedName = m_selected;
                    } else {
                        m_fileOperationStatus = "Rename failed.";
                        ImGui::EndPopup();
                        return;
                    }
                }

                m_showRenameItem = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    if (m_showDeleteItem) {
        ImGui::SetNextWindowSize(ImVec2(340, 0));
        if (ImGui::BeginPopupModal("Delete Item", &m_showDeleteItem, ImGuiWindowFlags_NoResize)) {
            ImGui::TextWrapped("Delete '%s'?", m_deleteItemName.c_str());
            ImGui::TextDisabled("This removes it from disk.");
            if (!m_fileOperationStatus.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "%s", m_fileOperationStatus.c_str());
            }
            ImGui::Spacing();

            if (ImGui::Button("Delete")) {
                fs::path target = fs::path(m_currentPath) / m_deleteItemName;

                std::error_code ec;
                if (fs::is_directory(target, ec)) {
                    fs::remove_all(target, ec);
                } else {
                    fs::remove(target, ec);
                }

                if (!ec) {
                    if (m_selected == m_deleteItemName) {
                        m_selected.clear();
                        m_selectedPath.clear();
                        m_selectedName.clear();
                    }
                    refresh();
                } else {
                    m_fileOperationStatus = "Delete failed.";
                    ImGui::EndPopup();
                    return;
                }

                m_showDeleteItem = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                m_showDeleteItem = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
}

bool FileBrowser::show(const char* title, const char* filter) {
    if (!m_open) return false;

    std::string newFilter = filter ? filter : "";
    if (newFilter != m_filter) {
        m_filter = newFilter;
        refresh();
    }

    bool opened = true;
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title, &opened)) {
        ImGui::End();
        if (!opened) m_open = false;
        return false;
    }

    showAddressBar();

    if (ImGui::Button("..")) {
        fs::path parent = fs::path(m_currentPath).parent_path();
        if (parent != m_currentPath) navigateTo(parent.string());
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refresh();
    ImGui::SameLine();
    ImGui::TextUnformatted("Filter: ");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##filtercombo", m_filter.empty() ? "All Files" : m_filter.c_str())) {
        if (ImGui::Selectable("All Files", m_filter.empty())) { m_filter = ""; refresh(); }
        if (ImGui::Selectable(".fbx", m_filter == ".fbx")) { m_filter = ".fbx"; refresh(); }
        if (ImGui::Selectable(".obj", m_filter == ".obj")) { m_filter = ".obj"; refresh(); }
        if (ImGui::Selectable(".gltf;.glb", m_filter == ".gltf;.glb")) { m_filter = ".gltf;.glb"; refresh(); }
        if (ImGui::Selectable("Models + Zip", m_filter == ".fbx;.obj;.gltf;.glb;.zip")) { m_filter = ".fbx;.obj;.gltf;.glb;.zip"; refresh(); }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    showContextMenu();
    showNewItemPopup();
    showFileOperationPopups();

    ImGui::BeginChild("files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), true);

    for (size_t i = 0; i < m_entries.size(); i++) {
        std::string icon = std::string(fileIcon(m_entries[i])) + " ";
        std::string label = icon + m_entries[i];

        if (ImGui::Selectable(label.c_str(), m_selected == m_entries[i])) {
            if (m_isDir[i]) {
                navigateTo((fs::path(m_currentPath) / m_entries[i]).string());
            } else {
                m_selected = m_entries[i];
                m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
                m_selectedName = m_entries[i];
            }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !m_isDir[i]) {
            m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
            m_selectionConfirmed = true;
            m_open = false;
        }
    }

    ImGui::EndChild();
    ImGui::Separator();

    bool confirmed = false;
    if (!m_selected.empty()) {
        ImGui::Text("Selected: %s", m_selected.c_str());
        if (ImGui::Button("Open")) {
            m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
            confirmed = true;
            m_open = false;
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Cancel")) {
        m_selected.clear();
        m_selectedPath.clear();
        m_selectedName.clear();
        m_open = false;
    }

    ImGui::End();
    return confirmed;
}

void FileBrowser::showContents(const char* filter) {
    m_selectionConfirmed = false;

    std::string newFilter = filter ? filter : "";
    if (newFilter != m_filter) {
        m_filter = newFilter;
        refresh();
    }

    ImGui::BeginChild("##fb-nav", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 4), false);
    if (ImGui::Button("<- Back")) {
        fs::path parent = fs::path(m_currentPath).parent_path();
        if (parent != m_currentPath && m_currentPath.find(m_rootPath) == 0) {
            navigateTo(parent.string());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("R")) refresh();
    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        confirmExternalFile(NativeDialogs::openAnyFile());
    }
    ImGui::SameLine();
    float filterW = 120.0f;
    ImGui::SetNextItemWidth(filterW);
    if (ImGui::BeginCombo("##filter", m_filter.empty() ? "All" : m_filter.c_str())) {
        if (ImGui::Selectable("All", m_filter.empty())) { m_filter = ""; refresh(); }
        if (ImGui::Selectable("Text", m_filter == ".txt;.md;.json;.xml;.ini;.cfg;.as;.cpp;.c;.h;.hpp;.cs;.lua;.js")) { m_filter = ".txt;.md;.json;.xml;.ini;.cfg;.as;.cpp;.c;.h;.hpp;.cs;.lua;.js"; refresh(); }
        if (ImGui::Selectable(".fbx", m_filter == ".fbx")) { m_filter = ".fbx"; refresh(); }
        if (ImGui::Selectable(".obj", m_filter == ".obj")) { m_filter = ".obj"; refresh(); }
        if (ImGui::Selectable(".gltf;.glb", m_filter == ".gltf;.glb")) { m_filter = ".gltf;.glb"; refresh(); }
        if (ImGui::Selectable("Models + Zip", m_filter == ".fbx;.obj;.gltf;.glb;.zip")) { m_filter = ".fbx;.obj;.gltf;.glb;.zip"; refresh(); }
        if (ImGui::Selectable("Scenes", m_filter == ".rcscene")) { m_filter = ".rcscene"; refresh(); }
        if (ImGui::Selectable("Scripts (.as;.lua)", m_filter == ".as;.lua")) { m_filter = ".as;.lua"; refresh(); }
        if (ImGui::Selectable("Images", m_filter == ".png;.jpg;.jpeg;.tga;.bmp;.hdr")) { m_filter = ".png;.jpg;.jpeg;.tga;.bmp;.hdr"; refresh(); }
        if (ImGui::Selectable("Audio", m_filter == ".wav;.mp3;.ogg")) { m_filter = ".wav;.mp3;.ogg"; refresh(); }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        ImGui::OpenPopup("##fb-create");
    }
    if (ImGui::BeginPopup("##fb-create")) {
        if (ImGui::MenuItem("New Folder")) {
            m_showNewFolder = true;
            m_showNewScript = false;
            std::memset(m_newItemName, 0, sizeof(m_newItemName));
            m_openNewFolderPopup = true;
        }
        if (ImGui::MenuItem("New Script")) {
            m_showNewScript = true;
            m_showNewFolder = false;
            std::memset(m_newItemName, 0, sizeof(m_newItemName));
            m_openNewScriptPopup = true;
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    ImGui::BeginChild("##fb-path", ImVec2(0, ImGui::GetFrameHeightWithSpacing()), false);
    showAddressBar();
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::BeginChild("##fb-files", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), true);
    for (size_t i = 0; i < m_entries.size(); i++) {
        ImGui::PushID((int)i);
        std::string icon(1, fileIcon(m_entries[i])[0]);
        std::string label = icon + "  " + m_entries[i];
        bool isSelected = (m_selected == m_entries[i]);

        ImGui::PushStyleColor(ImGuiCol_Text, m_isDir[i] ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f) : ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            m_selected = m_entries[i];
            m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
            m_selectedName = m_entries[i];
            m_selectionConfirmed = false;
        }
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (m_isDir[i]) {
                navigateTo((fs::path(m_currentPath) / m_entries[i]).string());
            } else {
                m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
                m_selectionConfirmed = true;
            }
        }

        if (ImGui::BeginPopupContextItem("##fb-item-ctx")) {
            if (ImGui::MenuItem("Rename")) {
                requestRename(m_entries[i]);
            }
            if (ImGui::MenuItem("Delete")) {
                requestDelete(m_entries[i]);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    showContextMenu();
    ImGui::EndChild();

    showNewItemPopup();
    showFileOperationPopups();

    ImGui::Separator();
    ImGui::BeginChild("##fb-bottom", ImVec2(0, 0), false);
    if (!m_selected.empty()) {
        ImGui::TextUnformatted(m_selected.c_str());
        ImGui::SameLine();
        bool isDir = fs::is_directory(fs::path(m_currentPath) / m_selected);
        if (!isDir && ImGui::Button("Open")) {
            m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
            m_selectionConfirmed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
            requestRename(m_selected);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            requestDelete(m_selected);
        }
        if (!isDir) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Load Script")) {
                std::string ext = fs::path(m_selected).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](char c) { return (char)std::tolower((unsigned char)c); });
                if (ext == ".as" || ext == ".lua") {
                    m_selectedPath = (fs::path(m_currentPath) / m_selected).lexically_normal().string();
                    m_selectionConfirmed = true;
                }
            }
        }
    }
    ImGui::EndChild();
}
