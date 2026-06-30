#pragma once
#include <string>
#include <vector>

class FileBrowser {
public:
    FileBrowser();

    // popup mode
    bool show(const char* title, const char* filter = nullptr);
    bool isOpen() const { return m_open; }
    void setOpen(bool v) { m_open = v; }

    // embedded mode (call inside an existing ImGui context)
    void showContents(const char* filter = nullptr);
    bool selectionConfirmed() const { return m_selectionConfirmed; }
    const std::string& selectedPath() const { return m_selectedPath; }
    const std::string& selectedName() const { return m_selectedName; }
    void clearSelectionConfirmed() { m_selectionConfirmed = false; }
    void confirmExternalFile(const std::string& path);

    void setRoot(const std::string& rootPath);
    const std::string& rootPath() const { return m_rootPath; }
    const std::string& currentPath() const { return m_currentPath; }
    void setCurrentPath(const std::string& path) { navigateTo(path); }

    bool createFolder(const std::string& name);
    bool createScript(const std::string& name);

private:
    void refresh();
    void navigateTo(const std::string& path);
    void showContextMenu();
    void showAddressBar();
    int showNewItemPopup();
    void requestRename(const std::string& name);
    void requestDelete(const std::string& name);
    void showFileOperationPopups();

    bool m_open = false;
    std::string m_rootPath;
    std::string m_currentPath;
    std::string m_selected;
    std::string m_selectedPath;
    std::string m_selectedName;
    std::string m_filter;
    std::vector<std::string> m_entries;
    std::vector<bool> m_isDir;
    char m_pathBuf[1024] = {};
    bool m_selectionConfirmed = false;

    char m_newItemName[128] = {};
    bool m_showNewFolder = false;
    bool m_showNewScript = false;
    bool m_openNewFolderPopup = false;
    bool m_openNewScriptPopup = false;

    char m_renameItemName[128] = {};
    std::string m_renameOldName;
    std::string m_deleteItemName;
    std::string m_fileOperationStatus;
    bool m_showRenameItem = false;
    bool m_showDeleteItem = false;
    bool m_openRenameItemPopup = false;
    bool m_openDeleteItemPopup = false;
};
