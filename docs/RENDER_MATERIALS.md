# Render materials

## Contract

`FragmentShader` is a backend-neutral shader asset. It contains equivalent
fragment variants for the APIs supported by the project:

- SPIR-V for Vulkan targets.
- DXIL for D3D12 targets.
- MSL or a Metal library for Metal targets.

`Material` references one shader and owns its authored uniform values. A
material does not know whether it will render a world sprite, a UI
image, a text glyph, or a full screen quad. `Renderer2D` binds it while flushing
sprite batches, and `MaterialPostProcessPass` adapts that same material to
`PostProcessPipeline`.

For shaders with zero or one sampler, repeated `ShaderAsset::load` calls share
an immutable shader when its name,
layout, variant paths, entry points and bytes are identical. The reuse table
holds weak references only; materials and renderer caches own the lifetime.
Loads still validate and read their sources, so changed or missing files are
observed without requiring a database rescan. GPU resources remain per backend.
Shaders with zero or one sampler share a render state across material instances;
uniform changes are uploaded at bind time and do not recreate the shader.

This separation is intentional:

```text
shader algorithm + material parameters
                  |
          +-------+--------+
          |                |
 world/UI/text quad   fullscreen quad
```

Effects that need neighboring pixels, such as blur and bloom, can use the same
shader code on a sprite, but the result is limited to the pixels available in
that sprite's source texture. A multi-pass fullscreen effect still belongs in
`PostProcessPipeline`; a local multi-pass effect will eventually render the
entity to an intermediate target before applying the same material chain.

## Engine shader globals

A shader can opt into one engine-owned uniform buffer by declaring
`"source": "engine"` in its `.shader` descriptor. The buffer is uploaded by
the renderer and is deliberately absent from the `.material` `uniforms` array:

```json
{
  "name": "EngineGlobals",
  "size": 32,
  "source": "engine",
  "parameters": []
}
```

Its portable HLSL layout is two `float4` rows:

```hlsl
cbuffer EngineGlobals : register(b1, space3)
{
    float4 engine_time;     // game time, real time, game delta, frame index
    float4 engine_viewport; // output size xy, inverse output size zw
};
```

The binding number must match the buffer's position in `uniform_buffers`.
There can be at most one engine buffer, it must be exactly 32 bytes, and the
existing four-buffer GPU limit includes it. `engine_time.x` follows the scaled
game clock and therefore stops while gameplay is paused; `engine_time.y`
continues on the real/editor clock.

Shaders may set `sampler_count` to `0`. A world `SpriteComponent` that has such
a material is a material-only quad and does not require a texture asset. Its
UVs, vertex color, transform, render layer, sorting and batching still use the
normal sprite path. Shaders with one or more samplers retain the white-texture
fallback when no texture is authored.

## Trail Renderer 2D

`TrailRenderer2DComponent` records a bounded history of an entity's world
position. `TrailSystem2D` joins those points into a continuous tapered ribbon
and submits its segments as GPU quads through the normal material, sorting,
render-layer and post-process path.

The component owns only authored settings: material, colors, lifetime, sample
distance, widths, point budget and blend mode. Point history is runtime-only,
so prefab and scene serialization remain deterministic. Turning `emitting` off
lets the existing ribbon fade; turning the component off removes it
immediately. `smoothing` adds Catmull-Rom subdivisions only while building the
render geometry; it never changes the authored point history. `texture_scale`
repeats the procedural pattern along the ribbon
and `flow_speed` scrolls it over time. The frame profiler reports `trails` and
`trail_points` counters.

Particle effects can complement a ribbon with `distance_rate` emission,
`start_stretch` / `end_stretch`, and `align_to_velocity`. Distance emission is
measured in particles per world pixel and produces world-space particles along
the path actually travelled during the frame.

## Runtime example

```cpp
auto shader = std::make_shared<Engine::FragmentShader>("NightVision");
shader->set_uniform_buffer_count(1);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::Msl,
    "Assets/Shaders/night_vision.frag.msl",
    "main0"
);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::SpirV,
    "Assets/Shaders/night_vision.frag.spv"
);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::Dxil,
    "Assets/Shaders/night_vision.frag.dxil"
);

auto material = std::make_shared<Engine::Material>("NightVisionEnemy", shader);
material->set_uniform(0, uniforms); // Use an explicitly aligned POD struct.
sprite.material = material;

// The identical material can be used for the complete camera image.
pipeline.add_pass(std::make_shared<Engine::MaterialPostProcessPass>(
    material,
    renderer.get_material_backend()
));
```

## Validation scene

The isolated post-processing demo contains the first real shared GPU material:

- `7` applies the NightVision color grade only to the center sprite.
- `8` replaces the original profile's color-grading pass with that identical
  material, while preserving its fullscreen vignette, bloom and film grain.
- `9` composes the reusable profile at entity and layer scope before global
  post-processing, then draws the HUD without post-processing.

The canonical project source is `Assets/Shaders/night_vision.frag.hlsl`. The
MSL variant makes the vertical slice executable on Metal today. The pinned
SDL_shadercross build toolchain compiles and structurally validates matching
MSL, SPIR-V, and DXIL artifacts with:

```sh
cmake --build build --target validate_compiled_shaders
```

The toolchain is deliberately separate from the normal engine build. It is
downloaded and built only by that target, or a developer can point
`ENGINE_SHADERCROSS_EXECUTABLE` to a prebuilt `shadercross` executable. The
generated files live under `build/shader_assets`. Stage a runnable project
root with the platform-specific variant using:

The selected project's `project.engine2d` declares the canonical source,
shader descriptor, and variant base under `runtime_shader`; export tooling does
not assume a shader filename from the engine source tree.

```sh
cmake --build build --target stage_runtime_assets
```

Run the native GPU pixel probes only on machines with graphics hardware:

```sh
cmake -S . -B build -DENGINE_ENABLE_HARDWARE_TESTS=ON
cmake --build build --target GPURenderSmokeTests
ctest --test-dir build -L hardware --output-on-failure
```

Hardware tests are disabled by default. Unsupported renderers or missing
native shader variants report a CTest skip instead of failing the headless
suite. Non-Apple hardware builds generate the SPIR-V and DXIL variants before
building the smoke executable.

The target copies `project.engine2d`, `Assets/`, `ProjectSettings/`, and the standalone executable
into `build/runtime/`. It keeps the stable source handle and replaces its file
with MSL on macOS, DXIL on Windows, or SPIR-V on Linux; the staged descriptor
declares that corresponding format. Native library bundling and code-signing
remain separate standalone-export tasks on Windows and Linux.

On macOS, package the staged runtime as a self-contained application bundle
with:

```sh
cmake --build build --target package_macos_app
```

This produces `build/dist/Engine2D.app`. It places assets and project settings
under `Contents/Resources`, copies non-system dynamic libraries to
`Contents/Frameworks`, rewrites their load paths, and ad-hoc signs the bundle.
Verify the complete bundle contract with:

```sh
cmake --build build --target verify_macos_app
```

Run the standalone executable from an isolated copy of the bundle and verify
that it resolves the packaged scene and shader without opening a window:

```sh
cmake --build build --target smoke_macos_export
```

Create an arm64+x86_64 universal macOS bundle with:

```sh
cmake --build build --target package_macos_universal
```

The output is `build/dist/Engine2D-universal.app`. The Intel slice is built
from the local x64-osx vcpkg dependencies, then merged with the arm64 slice
and ad-hoc signed again.

On Windows, create the portable folder and its runtime DLL set with:

```powershell
cmake --build build --target package_windows_portable
```

The output is `build/dist/Engine2D/`. It includes the staged runtime files and
all non-system DLL dependencies discovered from the PE import table.

Validate the portable folder and create a distributable ZIP with:

```powershell
cmake --build build --target verify_windows_portable
cmake --build build --target package_windows_zip
```

The archive is `build/dist/Engine2D-windows-x64.zip`.

From macOS, cross-compile the Windows x64 build after installing the local
cross dependencies once:

```sh
VCPKG_ROOT="$PWD/build/toolchains/vcpkg" vcpkg install \
  sdl3:x64-mingw-dynamic glm:x64-mingw-dynamic
cmake -S . -B build-windows \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
cmake --build build-windows --target package_windows_portable
```

The MinGW toolchain emits an x64 Windows executable. Validate every new
release artifact on Windows (or Wine) before distribution.

## Asset pipeline boundary

Shader compilation is deliberately not performed at runtime. Production builds
must compile one source shader into the platform variants above and package
only the required variants. SDL_shadercross is a pinned build tool, not a
runtime dependency. Missing or unsupported variants are reported as errors and
never silently interpreted as another binary format.

The current slice supports the primary sprite texture and up to four fragment
uniform slots. Additional textures and reflected parameter metadata belong in
the next asset-pipeline increment; they should not be added as hardcoded fields
to `SpriteComponent`.

`UIImageComponent` and `UITextComponent` store the reusable material asset
handle plus a runtime-resolved material. `UIButtonComponent` remains an input
and state component; its visual target is the `UIImageComponent` on the same
entity, matching the separation between interaction and graphics. Material
refresh reloads world sprites and UI graphics without duplicating shader data.
