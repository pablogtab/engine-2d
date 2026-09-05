# Surface Generator

Editor-only texture synthesis from one to six imported textures. It bakes a PNG;
there is no synthesis shader or generation cost at runtime. Open **Panels >
Surface Generator**, or the **Tilemap** workspace (reset its layout if you already
have a saved layout).

## Workflow

1. Import the original textures into the current project's `Assets/` directory.
2. Drag textures from the Asset Browser into the source fields, or enter their
   asset paths. Use compatible scale, palette and perspective.
3. Select output dimensions and an `Assets/.../*.png` destination. A source image
   cannot also be the output destination.
4. Generate a preview. Inspect at 1x for pixel detail and zoom out for repetition.
   Generation runs in the background and can be cancelled.
5. Save to Assets. This imports the PNG and a versioned `.surface.json` recipe.
   Existing PNGs or recipes require confirmation. Saving uses the settings that
   produced the preview, even if controls were edited afterward.
6. Place the generated texture using the usual sprite/material workflow. Saving
   does **not** replace any scene floor automatically.

Load saved settings reads the recipe beside the selected output PNG. Source
handles are preferred over paths, allowing imported assets to be moved. Loading
clears the old preview; generate again to see the loaded settings. Invalid recipes
leave the current settings intact.

## Controls

- **Patch size:** source region size in pixels; must fit inside every source.
- **Overlap:** shared strip where a minimum-error cut joins adjacent patches.
- **Candidates:** candidate regions evaluated per placement; higher costs more.
- **Repeat radius:** penalizes nearby reuse of similar source regions.
- **Weight:** source sampling preference, not an exact output-area percentage.
- **Flip / Rotate 90:** allowed transforms. Keep rotation off for directional art.
- **Luminance match:** adjusts local brightness at joins. Set to zero to retain
  original palette values; nonzero values may introduce new palette colors.
- **Seed:** repeatable output for identical settings, files and tool build.

Start with a 128-pixel patch, 32-pixel overlap and 48 candidates. Use source
variations to break up large recognizable features; increasing candidates alone
cannot invent missing art.

## Limits

This is overlapping-patch image quilting, not a map or prop generator. It does
not guarantee seamless wrapping at the **outer** output edges, perfect crack
continuity, or the absence of recognizable repeated features. Preview seam error
and repetition correlation are heuristics, not visual acceptance tests. Source
usage reports patch counts, not final pixel coverage. Transparency is preserved,
but ground textures should normally be opaque.

PNG replacement is atomic. The recipe is written separately and failures are
reported; the pair is not a crash-atomic transaction. Keep both files in version
control. Asset import is separate from scene placement and visual approval.

## CLI and checks

```sh
cmake --build build --target Engine2D SurfaceGenerateTool SurfaceGenerationTests
./build/SurfaceGenerateTool /tmp/surface.png 2048 1024 7319 /absolute/source.png
ctest --test-dir build --output-on-failure -R 'SurfaceGeneration|Workspace'
```

The CLI accepts up to six sources with equal sampling weights and default
synthesis settings. It only writes the PNG: it does not import assets, save a
recipe or modify scenes. Use the editor for the complete authoring workflow.
