# CLAUDE.md - Project Directives & Architectural Guidelines

## 🚀 Overview
Cross-Platform 2D/2.5D Game Engine built with **C++20**, **SDL3**, and **GLM**. Designed with native multiplatform support (Windows, macOS, iOS) and depth visual effects (Parallax Scrolling, Z-depth scaling, and Y-sorting).

---

## ⚙️ Directivas de Trabajo con la IA (Workflow Rules)

1. **Gestión de Git / Commits:**
   - **NO realizar `git commit` automáticamente** tras cada cambio o archivo editado.
   - Ejecutar `git commit` **únicamente cuando el usuario lo solicite de forma explícita**.

2. **Metodología de Desarrollo Incremental:**
   - Resolver y probar las tareas y optimizaciones **una a una de forma incremental**.
   - Probar y verificar la compilación (`cmake --build build`) tras cada avance antes de proceder al siguiente paso.

3. **Formato de Logs & Telemetría:**
   - Evitar logs ineficientes con decimales innecesarios (ej. `99.9667%`).
   - Imprimir únicamente métricas esenciales, limpias y formateadas (ej. `[RENDER STATS] Rendered: 2420 | Culled: 580 | Draw Calls: 1`).

4. **Estructura del Proyecto:**
   - Mantener `src/` exclusivamente para código propio del motor.
   - Las librerías de terceros residen en `vendor/` (ej. `vendor/stb/stb_image.h` y `stb_impl.cpp`).
   - Los assets están organizados en `assets/` por tipo (`assets/textures/`, etc.).

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
   - Prefer modern C++ idioms: `std::shared_ptr`/`std::unique_ptr`, `glm::vec2`/`glm::vec3`, `const` correctness, `override`, `constexpr`.

3. **Include Paths & Headers:**
   - Use `#pragma once` in all header files.
   - Headers: include `src/` and `vendor/` in CMake include directories.
   - CMake exports `compile_commands.json` automatically to the workspace root for IDE language server indexation (clangd / IntelliSense).

4. **Naming Conventions:**
   - **Classes / Structs:** `PascalCase` (e.g., `Camera2D`, `Renderer2D`, `AssetManager`).
   - **Functions / Methods:** `snake_case` (e.g., `world_to_screen`, `submit_sprite`, `load_texture`).
   - **Member Variables:** `m_snake_case` prefix (e.g., `m_position`, `m_viewport_size`, `m_textures`).
   - **Constants / Enums:** `UPPER_SNAKE_CASE` or scoped enums.

---

## 🏗️ Core Architecture & Implemented Systems

### 1. Platform & Application Layer
- **Window Management (`Engine::Window`):** Built on **SDL3**, supports high-DPI (Retina), VSync and Virtual Presentation (`LETTERBOX`).
- **Application Loop (`Engine::Application`):** Fixed/variable time-stepping with precise `delta_time` calculation.
- **Resource Management (`Engine::AssetManager`):** Centralized texture caching using `std::unordered_map` and `std::shared_ptr<Texture>` to prevent duplicate RAM/VRAM loads.

### 2. 2.5D Renderer & Performance Optimizations (`Engine::Renderer2D`)
- **2.5D Depth & Parallax (`Engine::Camera2D`):** Converts 3D world $(X, Y, Z)$ to 2D screen coordinates with parallax displacement and focal depth scaling.
- **Sprite Batching / Instancing (Optimization 2.1):** Uses `SDL_RenderGeometry` to group quad geometry per texture into single VBO/VAO draw calls. Reduces 3,000 individual draw calls to **1 single GPU draw call** per frame (99.9% reduction).
- **2D Frustum Culling (Optimization 2.2):** $O(1)$ early discard check (`Camera2D::is_visible`) before pushing out-of-screen sprites to render queues.
- **Cropping & Animations (`SpriteSheet` & `Animation2D`):** Full support for texture cropping (`src_rect`), grid sprite sheets, FPS control and clip loops.

### 3. Testbed & Active Sandbox Scenes (`Engine::SceneManager`)
- **Scene 1 (`Sandbox::AnimatedSpriteDemoScene`):** 2D sprite sheet animations (*Idle*, *Run*, *Spin*) loaded via PNG asset `assets/textures/player.png`.
- **Scene 2 (`Sandbox::DepthDemoScene`):** 2.5D Parallax & Depth Layers.
- **Scene 3 (`Sandbox::StressTestScene`):** Performance stress test rendering 3,000 animated sprites with real-time GPU/Culling telemetry.
