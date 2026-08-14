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
   - **NO utilizar emoticones / emojis en los logs**. Mantener todos los logs en texto ASCII limpio y profesional.

4. **Estructura del Proyecto:**
   - Mantener `src/` exclusivamente para código propio del motor.
   - Las librerías de terceros residen en `vendor/` (ej. `vendor/stb/stb_image.h` y `stb_impl.cpp`).
   - Los assets están organizados en `assets/` por tipo (`assets/textures/`, etc.).

5. **Sin Código Legacy ni Retrocompatibilidad en Desarrollo Activo:**
   - **NO mantener capas de compatibilidad hacia atrás o soporte legacy** mientras el motor esté en desarrollo activo.
   - Cuando se refactorice o mejore una estructura, API o componente del motor (ej. `BodyType` en lugar de booleanos, nuevas firmas de funciones), **refactorizar inmediatamente todas las escenas y llamadas existentes en el proyecto** para adoptar la nueva API limpia y eliminar cualquier código obsoleto.

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

### 4. Native 2D Particle Engine (`Engine::ParticleSystem2D`)
- **Zero-Allocation Memory Pool**: Pre-allocated circular ring buffer (`reserve_capacity(n)`). Zero dynamic allocations during runtime.
- **Configurable Pool Size**: `ParticleSystem2D(size_t max_particles = 2500)` - tuneable per scene.
- **Particle Properties (`ParticleProps`)**: Full control over position, velocity, gravity, acceleration, color gradients (start→end), size curves, rotation speed, lifetime variation, and per-particle collision.
- **Physics-Aware Bouncing**: `collide_with_world = true` activates per-particle `PhysicsSystem::raycast` reflection: $\vec{v}' = \text{reflect}(\vec{v}, \vec{n}) \times \text{bounciness}$.
- **Batch GPU Rendering**: All active particles are submitted directly into `Renderer2D` batching pipeline — zero extra draw calls.
- **Default Procedural Textures**: `init_default_textures(SDL_Renderer*)` generates a soft radial disc and a 4-point diamond star. These are used automatically when `props.texture == nullptr`.
- **Custom Textures**: Any external PNG (`Texture::load_from_file`) or procedural texture can be assigned to `ParticleProps::texture`.

**Architecture Rule — Content Agnosticism:**
`ParticleSystem2D` is **pure engine**: `emit()`, `update()`, `render()`, `clear()`. It has no knowledge of game concepts.
Game-specific particle recipes live in `Engine::Presets` (`src/particles/ParticlePresets.hpp`) — a **header-only, optional helper** that any game can use, extend, or entirely replace with its own.

```
Engine::ParticleSystem2D      ← Pure engine core (pool, kinematics, rendering)
Engine::Presets::explosion()  ← Optional builtins (game-context recipes, header-only)
Sandbox / Game Code           ← Calls Engine::Presets:: or defines its own ParticleProps
```

**Do not add game-semantic presets to `ParticleSystem2D` directly.**

### 5. Procedural Texture System (`Engine::Texture` – Generators)
**Design Principle**: All procedural generators in the engine are **content-agnostic**. They operate purely on mathematical parameters — they do not know "what" they are generating (grass, fire, smoke, water). The calling code or a future GUI layer decides the semantic meaning.

**Existing Generators:**
- `Texture::create_solid(w, h, r, g, b, a)` — flat color fill.
- `Texture::create_circle(radius, r, g, b, a)` — hard-edge anti-aliased circle with 1.5px feathering.
- `Texture::create_soft_circle(radius, r, g, b)` — radial alpha falloff disc: $\alpha(d) = (1 - d/R)^{1.4}$. Suitable for any soft-edged radial shape.
- `Texture::create_spark(radius, r, g, b)` — 4-point diamond star with bright core. Suitable for any high-contrast point-light shape.
- `Texture::create_grid(size, r, g, b)` — bordered grid tile.

**Planned Generators (content-agnostic):**
```
Texture::create_procedural(renderer, ProceduralTextureDesc desc)
```
With `ProceduralTextureDesc` containing:
- **Type**: `NoiseGradient`, `Voronoi`, `LinearGradient`, `RadialGradient`, `Checkerboard`, `Custom`
- **color_a / color_b**: Two colors to interpolate between
- **scale**: Frequency / zoom level of the pattern
- **octaves**: Detail layers (for noise types)
- **seed**: Integer seed for full reproducibility
- **blend_fn**: Optional `std::function<float(float x, float y)>` lambda for custom generators

**GUI Binding Intent**: `ProceduralTextureDesc` is a plain struct — fully serializable to JSON/YAML. A future editor GUI exposes sliders and color pickers that write directly into this struct. The engine regenerates the texture on demand. The GUI does not need to know any math; the engine encapsulates it all.

### 6. Agnostic Post-Processing & Shader Pipeline (`Engine::PostProcessPipeline`)
**Design Principle**: The engine core does not hardcode visual post-processing presets. It provides an extensible **Render Target & Pass-Chaining Pipeline** (`PostProcessPipeline`) driven by pure abstract contracts (`IPostProcessPass`).

```
Engine::PostProcessPipeline   ← Pure engine runtime (ping-pong FBOs, pass dispatcher)
Engine::IPostProcessPass      ← Abstract contract: apply(renderer, source, destination, w, h)
Engine::PostProcessPasses     ← Decoupled reference passes (Vignette, ChromaticAberration, ScreenFlash)
Custom User Passes            ← Inherit IPostProcessPass without modifying the engine core
```

- **Ping-Pong Render Targets**: Dual off-screen hardware textures (`SDL_TEXTUREACCESS_TARGET`) in VRAM. Zero CPU round-trips.
- **Dynamic Pass Management**: Passes can be registered (`add_pass`), removed, enabled/disabled in real time.
- **Sprite Outline & Material Highlighting**: Supported directly via `SpriteComponent::has_outline`, `outline_color`, `outline_thickness`, and `outline_pulse` within `RenderSystem`.
