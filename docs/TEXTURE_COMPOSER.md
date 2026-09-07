# Texture Composer

Texture Composer flattens imported texture layers into a transparent PNG while keeping an editable recipe beside the output.

## From the scene viewport

1. In Edit mode, select at least two entities with Sprite Renderer components.
2. Right-click a selected sprite and choose **Compose Selected Sprites...**.
3. Inspect the generated layers in **Texture Composer**. Position, size, pivot, rotation, flips, tint, crop and visual order are captured from the scene.
4. Adjust layers, rebuild the preview, and export below the project's `Assets/` directory.

The source entities are never changed. The exported `.png` is imported into the Asset Database, and a `.texturecompose.json` recipe is saved beside it. Enter the PNG path later and choose **Load Recipe** to continue editing.

Textures can also be dragged from Asset Browser onto the drop target for manual composition.

## Occlusion pieces

A flattened texture can occupy only one sorting layer. Build separate composites for architecture behind a character and architecture in front of it, then place those two exported sprites on the corresponding sorting layers.

Materials, dynamic lighting, outlines and skew are not baked. The panel warns when selected sprites use unsupported presentation data. Pixel sampling is nearest-neighbor and alpha remains transparent.
