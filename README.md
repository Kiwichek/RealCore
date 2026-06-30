# RealCore Engine

A lightweight 3D game engine written in C++20 with an integrated editor, dual scripting system, physics simulation, and AI-assisted development.

## Overview

RealCore is a Windows-focused 3D game engine built on modern graphics and physics libraries. It provides a full editor interface for scene authoring, gameplay scripting, and game export. The engine supports both AngelScript and Lua for gameplay logic, includes Jolt-based physics, PBR rendering with shadow mapping, and an integrated AI assistant powered by Ollama.

## Features

- **3D Rendering** - PBR shading with normal mapping, metallic/roughness workflows, and shadow mapping via D3D11
- **Scene Editor** - Hierarchy panel, inspector, content browser, and 3D viewport with transform gizmos
- **Physics** - Jolt Physics integration with Box, Sphere, Capsule, Cylinder, and Plane colliders
- **Scripting** - Dual language support: AngelScript (.as) and Lua (.lua) with identical engine APIs
- **Resource Pipeline** - Assimp-based loading of FBX, OBJ, GLTF, GLB models; ZIP archive support; texture loading via stb_image
- **Audio** - Cross-platform audio playback via miniaudio (WAV, MP3, OGG)
- **AI Assistant** - In-editor Ollama integration for script generation and scene editing through natural language
- **Game Export** - One-click export to standalone Windows executable with all assets
- **Scene Serialization** - Custom `.rcscene` text format with full component persistence

## Requirements

- Windows 10 or later
- CMake 3.20+
- Visual Studio 2022 or compatible compiler with C++20 support
- vcpkg package manager

### Dependencies (via vcpkg)

| Library | Purpose |
|---------|---------|
| Assimp | 3D model loading (FBX, OBJ, GLTF, GLB) |
| Jolt Physics | Physics simulation |
| Dear ImGui | Editor interface |
| AngelScript | Scripting language |
| ZLIB | ZIP archive extraction |
| Lua | Scripting language |

### Bundled (in `third_party/`)

| Library | Purpose |
|---------|---------|
| Sokol | Cross-platform app/gfx/glue layer |
| stb_image | Image loading |
| miniaudio | Audio playback |

## Building

1. Install vcpkg and the required packages:

```bash
vcpkg install assimp:jolt:imgui:angelscript:zlib:lua
```

2. Configure and build:

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The build process copies all required vcpkg DLLs to the output directory automatically.

## Project Structure

```
RealCore/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                    # Entry point, window setup
│   ├── sokol_impl.cpp              # Sokol library implementations
│   ├── core/
│   │   ├── engine.cpp/h            # Main engine singleton, game loop, scene I/O
│   │   ├── Camera.cpp/h            # Editor and runtime camera
│   │   └── math.h                  # Vec3, Mat4 math library
│   ├── graphics/
│   │   ├── renderer.cpp/h          # D3D11 renderer, shaders, shadow mapping
│   │   └── Gizmo.cpp/h             # Transform, rotation, camera, and light gizmos
│   ├── scene/
│   │   └── Scene.cpp/h             # Entity-component scene system
│   ├── resources/
│   │   ├── Mesh.cpp/h              # Mesh loading, primitives, materials
│   │   ├── Texture.cpp/h           # Texture loading (stb_image)
│   │   └── ResourceManager.cpp/h   # Asset management, Assimp loading, ZIP extraction
│   ├── physics/
│   │   └── PhysicsWorld.cpp/h      # Jolt Physics wrapper
│   ├── audio/
│   │   └── AudioEngine.cpp/h       # miniaudio wrapper
│   ├── script/
│   │   ├── ScriptEngine.cpp/h      # AngelScript VM and API bindings
│   │   └── LuaScriptEngine.cpp/h   # Lua VM and API bindings
│   ├── gui/
│   │   ├── GuiLayer.cpp/h          # ImGui editor interface
│   │   └── FileBrowser.cpp/h       # Embedded file browser widget
│   ├── ai/
│   │   └── OllamaClient.cpp/h      # Ollama HTTP client for AI assistant
│   └── platform/
│       └── NativeDialogs.cpp/h     # Win32 file/folder dialogs
├── third_party/
│   ├── sokol/                      # Sokol headers
│   ├── stb/                        # stb_image header
│   └── miniaudio/                  # miniaudio header
└── LICENSE                         # 0BSD license
```

## Usage

### Editor Mode

Run `RealCore.exe` directly. On startup, you can:

- Create a new project (specifies root folder and project name)
- Load an existing `.rcscene` file

The editor provides:

- **Hierarchy** - Scene object list with multi-select, rename, and delete
- **Inspector** - Component editing (Transform, Mesh Renderer, RigidBody, Light, Camera, Script)
- **Content Browser** - File navigation with filters, new folder/script creation, rename, and delete
- **Viewport** - 3D scene view with selection gizmos, grid, and camera frustum visualization
- **Menu Bar** - File operations, view toggles, and Play/Pause/Stop transport controls

### Runtime Mode

Run with `--game` flag or place a `realcore.game` manifest next to the executable:

```bash
RealCore.exe --game --scene Scenes/Main.rcscene --title "My Game"
```

The `realcore.game` manifest format:

```
RealCoreGame 1
Title "Game Title"
Scene "Scenes/Main.rcscene"
```

### Scene Format (`.rcscene`)

Text-based format:

```
RealCoreScene 1
Project "path/to/project"
Object "Object Name"
Transform px py pz rx ry rz sx sy sz
Mesh "kind" "source" visible
RigidBody syncTransform frozen Shape dynamic hx hy hz radius halfHeight
Light dx dy dz cr cg cb intensity ambient enabled
Camera fovY zNear zFar primary enabled
Script "path/to/script.as"
EndObject
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| WASD | Camera movement |
| Q/E | Camera down/up |
| Shift | Sprint |
| Right-click (hold) | Camera look mode |
| Arrow keys | Move selected entity |
| Alt+Arrow keys | Rotate selected entity |
| Page Up/Down | Move vertically |
| Ctrl+S | Save scene |
| Escape | Stop play / release mouse |
| Delete | Delete selected entity |

## Scripting API

Both AngelScript and Lua share the same engine API. Scripts must implement three lifecycle functions:

```
init()       - Called when play starts or script attaches
update(dt)   - Called every frame with delta time
destroy()    - Called when play stops or script detaches
```

### Global Objects

| Object | Description |
|--------|-------------|
| `node` | The scene object this script is attached to |
| `scene` | Scene query and manipulation |
| `input` | Mouse and screen input |
| `keyboard` | Keyboard state (Lua) |
| `physics` | Physics world control |
| `audio` | Sound playback |
| `time` | Delta time and frame info |

### Node API (AngelScript)

```angelscript
node.getPosition()              // returns Vector3
node.setPosition(Vector3)       // sets position
node.velocity                   // get/set physics velocity
node.valid()                    // check if entity exists
node.transform                  // Transform component
node.camera                     // Camera component
node.light                      // Light component
node.rigidBody                  // RigidBody component
node.hasCamera()                // component checks
node.hasLight()
node.hasRigidBody()
```

### Node API (Lua)

```lua
node:getPosition()              -- returns {x, y, z}
node:setPosition({x, y, z})    -- sets position
node.position                   -- get/set property
node.velocity                   -- get/set property
node.name                       -- get/set property
node:applyForce({x, y, z})     -- apply force to rigid body
node:applyImpulse({x, y, z})   -- apply impulse to rigid body
node:valid()                    -- check if entity exists
node.worldBounds                -- world-space bounding box
node.localBounds                -- local-space bounding box
```

### Scene API

```angelscript
// AngelScript
scene.findNode("name")          // returns Node@
scene.find("name")              // returns GameObject
scene.selected()                // returns selected GameObject
scene.createPrimitive("Box")    // creates primitive mesh
scene.load("Scenes/Level.rcscene")
```

```lua
-- Lua
scene:findNode("name")          -- returns node table
scene:load("Scenes/Level.rcscene")
```

### Input API

```angelscript
input.getMouseDeltaX()          // mouse delta this frame
input.getMouseDeltaY()
input.setMouseVisible(bool)
input.isMouseDown(int)          // 0=left, 1=right, 2=middle
input.isMousePressed(int)
input.getScreenWidth()
input.getScreenHeight()
```

```lua
-- Lua
input:getMouseDeltaX()
input:getMouseDeltaY()
input:setMouseVisible(bool)
input:isMouseDown(button)
input:isKeyPressed(button)
input.mouseX                    -- current mouse position
input.mouseY
input.screenWidth
input.screenHeight
keyboard.isDown("A")
keyboard.isPressed("Space")
keyboard.isReleased("Escape")
keyboard.A                      -- shorthand: keyboard.A == keyboard.isDown("A")
keyboard.Space
```

### Physics API

```angelscript
physics.gravity                 // get/set Vector3
physics.addKinematicBox(pos, halfExtent)
physics.screenToPlane(mouse, planeY, outPos)
```

```lua
-- Lua
physics.gravity                 -- get/set table {x, y, z}
```

### Audio API

```angelscript
audio.playSound("path.wav")     // returns uint64 handle
audio.playSound("path.wav", 0.5)
audio.stopSound(handle)
audio.stopAll()
audio.masterVolume              // get/set float
```

```lua
-- Lua
audio:playSound("path.wav")
audio:playSound("path.wav", 0.5)
audio:stopSound(handle)
audio:stopAll()
```

### Time

```
time.deltaTime                  // frame delta time
time.frame                      // current frame number (Lua)
```

### Utility Functions

```angelscript
print("text")                   // console output
loadScene("Scenes/Game.rcscene")
quitGame()
```

## AI Assistant

The editor includes an integrated AI assistant that communicates with a local Ollama server. Two modes are available:

1. **Script Mode** - Helps write and modify AngelScript/Lua scripts with engine-aware context
2. **Engine Mode** - Manipulates the scene through JSON actions (create objects, modify components, control playback)

To use: View > AI Assistant, select a model, and ask questions in natural language. Responses in Russian by default.

## Export

File > Export Game creates a standalone Windows executable:

- Copies the engine executable and all DLLs
- Copies Assets, Scripts, and Scenes folders
- Writes a `realcore.game` manifest
- Remaps external asset paths

## License

This project is licensed under the [BSD Zero Clause License](LICENSE).
