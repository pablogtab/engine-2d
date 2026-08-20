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
generated files live under `build/shader_assets`; packaging them into each
platform artifact is the next export-pipeline step.

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
