# Render materials

## Contract

`FragmentShader` is a backend-neutral shader asset. It contains equivalent
fragment variants for the APIs supported by the project:

- SPIR-V for Vulkan targets.
- DXIL for D3D12 targets.
- MSL or a Metal library for Metal targets.

`Material` references one shader and owns the uniform values for one visual
instance. A material does not know whether it will render a sprite or a full
screen quad. `Renderer2D` binds it while flushing sprite batches, and
`MaterialPostProcessPass` adapts that same material to `PostProcessPipeline`.

This separation is intentional:

```text
shader algorithm + material parameters
                  |
          +-------+--------+
          |                |
     sprite quad      fullscreen quad
```

Effects that need neighboring pixels, such as blur and bloom, can use the same
shader code on a sprite, but the result is limited to the pixels available in
that sprite's source texture. A multi-pass fullscreen effect still belongs in
`PostProcessPipeline`; a local multi-pass effect will eventually render the
entity to an intermediate target before applying the same material chain.

## Runtime example

```cpp
auto shader = std::make_shared<Engine::FragmentShader>("NightVision");
shader->set_uniform_buffer_count(1);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::Msl,
    "assets/shaders/night_vision.frag.msl",
    "main0"
);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::SpirV,
    "assets/shaders/night_vision.frag.spv"
);
shader->add_variant_from_file(
    Engine::ShaderBinaryFormat::Dxil,
    "assets/shaders/night_vision.frag.dxil"
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

The canonical source is `assets/shaders/night_vision.frag.hlsl`. The checked-in
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

```sh
cmake --build build --target stage_runtime_assets
```

The target copies `assets/`, `ProjectSettings/`, and the standalone executable
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

On Windows, create the portable folder and its runtime DLL set with:

```powershell
cmake --build build --target package_windows_portable
```

The output is `build/dist/Engine2D/`. It includes the staged runtime files and
all non-system DLL dependencies discovered from the PE import table.

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
