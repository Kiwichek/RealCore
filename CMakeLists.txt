cmake_minimum_required(VERSION 3.20)
project(RealCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DSOKOL_D3D11 -DSOKOL_WIN32 -DSOKOL_NO_ENTRY)

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "C:/vcpkg/installed/x64-windows")
find_package(assimp REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Angelscript CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Lua REQUIRED)

add_executable(RealCore
    src/main.cpp
    src/sokol_impl.cpp
    src/core/engine.cpp
    src/scene/Scene.cpp
    src/graphics/renderer.cpp
    src/graphics/Gizmo.cpp
    src/core/Camera.cpp
    src/resources/Mesh.cpp
    src/resources/Texture.cpp
    src/resources/ResourceManager.cpp
    src/physics/PhysicsWorld.cpp
    src/gui/GuiLayer.cpp
    src/gui/FileBrowser.cpp
    src/audio/AudioEngine.cpp
    src/script/ScriptEngine.cpp
    src/script/LuaScriptEngine.cpp
    src/ai/OllamaClient.cpp
    src/platform/NativeDialogs.cpp
)

target_include_directories(RealCore PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/sokol
    ${CMAKE_SOURCE_DIR}/third_party/stb
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio
    ${CMAKE_SOURCE_DIR}/src
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(RealCore PRIVATE
    assimp::assimp
    Jolt::Jolt
    imgui::imgui
    Angelscript::angelscript
    ZLIB::ZLIB
    ${LUA_LIBRARIES}
    ole32.lib
    shell32.lib
    winhttp.lib
    d3d11.lib
    dxgi.lib
)

# Copy all vcpkg runtime DLLs to output directory
file(GLOB VCPKG_DLLS "C:/vcpkg/installed/x64-windows/bin/*.dll")
add_custom_command(TARGET RealCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_DLLS} "$<TARGET_FILE_DIR:RealCore>"
    COMMAND_EXPAND_LISTS
)
