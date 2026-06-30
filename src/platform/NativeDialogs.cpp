#include <platform/NativeDialogs.h>

#include <sokol_app.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <filesystem>
#include <vector>

namespace {

std::wstring toWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    int count = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (count <= 0) {
        return {};
    }

    std::wstring wide((size_t)count - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), count);
    return wide;
}

std::string toUtf8(const wchar_t* value) {
    if (!value || value[0] == 0) {
        return {};
    }

    int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return {};
    }

    std::string utf8((size_t)count - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8.data(), count, nullptr, nullptr);
    return utf8;
}

HWND ownerWindow() {
    return (HWND)sapp_win32_get_hwnd();
}

struct ComInit {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComInit() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
    bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

std::string shellItemPath(IShellItem* item) {
    if (!item) {
        return {};
    }

    PWSTR widePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath))) {
        return {};
    }

    std::string result = toUtf8(widePath);
    CoTaskMemFree(widePath);
    return result;
}

std::string openFileDialog(const COMDLG_FILTERSPEC* filters, UINT filterCount, const wchar_t* title) {
    ComInit com;
    if (!com.ok()) {
        return {};
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    dialog->SetTitle(title);
    dialog->SetFileTypes(filterCount, filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

    std::string result;
    if (SUCCEEDED(dialog->Show(ownerWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            result = shellItemPath(item);
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

} // namespace

namespace NativeDialogs {

std::string openModelFile() {
    const COMDLG_FILTERSPEC filters[] = {
        { L"RealCore models", L"*.fbx;*.obj;*.gltf;*.glb;*.zip" },
        { L"Model files", L"*.fbx;*.obj;*.gltf;*.glb" },
        { L"Zip archives", L"*.zip" },
        { L"All files", L"*.*" }
    };
    return openFileDialog(filters, (UINT)std::size(filters), L"Import Model");
}

std::string openAnyFile() {
    const COMDLG_FILTERSPEC filters[] = {
        { L"All files", L"*.*" },
        { L"Text files", L"*.txt;*.md;*.json;*.xml;*.ini;*.cfg;*.as;*.lua" },
        { L"Model files", L"*.fbx;*.obj;*.gltf;*.glb;*.zip" },
        { L"Scene files", L"*.rcscene" }
    };
    return openFileDialog(filters, (UINT)std::size(filters), L"Open File");
}

std::string openSceneFile() {
    const COMDLG_FILTERSPEC filters[] = {
        { L"RealCore scene", L"*.rcscene" },
        { L"All files", L"*.*" }
    };
    return openFileDialog(filters, (UINT)std::size(filters), L"Load Scene");
}

std::string saveSceneFile(const std::string& defaultPath) {
    ComInit com;
    if (!com.ok()) {
        return {};
    }

    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    const COMDLG_FILTERSPEC filters[] = {
        { L"RealCore scene", L"*.rcscene" },
        { L"All files", L"*.*" }
    };
    dialog->SetTitle(L"Save Scene");
    dialog->SetFileTypes((UINT)std::size(filters), filters);
    dialog->SetDefaultExtension(L"rcscene");
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_OVERWRITEPROMPT);

    std::filesystem::path path(defaultPath);
    if (!path.filename().empty()) {
        std::wstring filename = toWide(path.filename().string());
        dialog->SetFileName(filename.c_str());
    }

    std::string result;
    if (SUCCEEDED(dialog->Show(ownerWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            result = shellItemPath(item);
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

std::string selectFolder() {
    ComInit com;
    if (!com.ok()) {
        return {};
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    dialog->SetTitle(L"Select Folder");
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    std::string result;
    if (SUCCEEDED(dialog->Show(ownerWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            result = shellItemPath(item);
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

}
