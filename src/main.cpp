#include <core/engine.h>
#include <sokol_app.h>
#include <algorithm>
#include <filesystem>

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

struct InitialWindowSize {
    int width = 1280;
    int height = 720;
};

static void setWorkingDirectoryToExecutable() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        std::error_code ec;
        fs::current_path(fs::path(buffer).parent_path(), ec);
    }
#endif
}

static InitialWindowSize calculateInitialWindowSize() {
    InitialWindowSize size;

#if defined(_WIN32)
    RECT workArea = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        int workWidth = (std::max)(1L, workArea.right - workArea.left);
        int workHeight = (std::max)(1L, workArea.bottom - workArea.top);

        int targetWidth = (workWidth * 90) / 100;
        int targetHeight = (workHeight * 90) / 100;

        size.width = (std::min)(workWidth, (std::max)(1280, targetWidth));
        size.height = (std::min)(workHeight, (std::max)(720, targetHeight));
    }
#endif

    return size;
}

static void init() {
    Engine::instance().init();
}

static void frame() {
    Engine::instance().frame();
}

static void event(const sapp_event* ev) {
    Engine::instance().event(ev);
}

static void cleanup() {
    Engine::instance().shutdown();
}

int main(int argc, char* argv[]) {
    setWorkingDirectoryToExecutable();
    Engine::instance().configureLaunch(argc, argv);
    InitialWindowSize windowSize = calculateInitialWindowSize();

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.event_cb = event;
    desc.cleanup_cb = cleanup;
    desc.width = windowSize.width;
    desc.height = windowSize.height;
    desc.window_title = Engine::instance().windowTitle().c_str();
    desc.sample_count = 4;
    desc.enable_clipboard = true;
    desc.clipboard_size = 65536;
    desc.win32.console_utf8 = true;

    sapp_run(&desc);
    return 0;
}
