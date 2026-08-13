# CLAUDE.md - Project Directives & Architectural Guidelines

## 🚀 Overview
Cross-Platform 2D/2.5D Game Engine built with **C++20**, **SDL3**, and **GLM**. Designed with native multiplatform support (Windows, macOS, iOS) and depth visual effects (Parallax Scrolling, Z-depth scaling, and Y-sorting).

---

## 🛠️ Build & Run Commands

- **Configure & Generate Build Files:**
  ```bash
  cmake -B build -S .
  ```
- **Live Auto-Reload Development Mode (Watch & Rebuild):**
  ```bash
  ./dev.sh
  ```
- **Manual Build & Run:**
  ```bash
  cmake --build build && ./build/Engine2D
  ```

---

## 📐 Code Style & Conventions

1. **Language & Comments:**
   - Write **all** source code, comments, docstrings, variable names, function names, and log output strictly in **English**.
   
2. **C++ Standard:**
   - Target **C++20**.
   - Prefer modern C++ idioms: `std::unique_ptr`, `glm::vec2`/`glm::vec3`, `const` correctness, `override`, `constexpr`.

3. **Include Paths & Headers:**
   - Use `#pragma once` in all header files.
   - Use relative include headers within local module directories (e.g. `#include "Camera2D.hpp"` inside `src/scene/Camera2D.cpp`).
   - CMake exports `compile_commands.json` automatically to the workspace root for IDE language server indexation (clangd / IntelliSense).

4. **Naming Conventions:**
   - **Classes / Structs:** `PascalCase` (e.g., `Camera2D`, `Renderer2D`, `SpriteCommand`).
   - **Functions / Methods:** `snake_case` (e.g., `world_to_screen`, `submit_sprite`, `begin_scene`).
   - **Member Variables:** `m_snake_case` prefix (e.g., `m_position`, `m_viewport_size`, `m_sdl_renderer`).
   - **Constants / Enums:** `UPPER_SNAKE_CASE` or scoped enums.

---

## 🏗️ Core Architecture & Depth Engine (2.5D)

- **Platform Layer:** Encapsulated in `Engine::Window` using **SDL3**. Uses GPU backends natively (**Metal** on macOS/iOS, **Direct3D11/OpenGL** on Windows).
- **Application Loop:** Managed by `Engine::Application` providing fixed time-stepping, `delta_time` calculation, and lifecycle events (`on_init`, `on_update`, `on_render`, `on_shutdown`).
- **2.5D Depth & Parallax (`Engine::Camera2D`):**
  - Uses 3D world coordinates $(X, Y, Z)$.
  - Objects with $Z < 0$ represent distant backgrounds (moving slower with parallax and scaling down).
  - Objects with $Z = 0$ represent the primary game plane.
  - Objects with $Z > 0$ represent foreground elements (moving faster and scaling up).
- **Testbed & Scene Manager (`Engine::Scene` & `Engine::SceneManager`):**
  - Manages scalable, decoupled test scenes.
  - Allows adding new test scenes inside `src/sandbox/` without modifying core engine logic.
  - Runtime scene switching via `TAB` key (or numeric hotkeys).
  - Built-in Scenes:
    - **Scene 1 (`Sandbox::DepthDemoScene`):** 2.5D Parallax & Depth Layers.
    - **Scene 2 (`Sandbox::StressTestScene`):** Performance stress test rendering 3,000 animated sprites.
