{
  "name": "Night Vision",
  "sampler_count": 1,
  "uniform_buffers": [
    {
      "name": "NightVisionUniforms",
      "size": 32,
      "parameters": [
        {
          "name": "Tint",
          "type": "color",
          "offset": 0,
          "default": [1.0, 1.0, 1.0, 1.0]
        },
        {
          "name": "Shadow Tint",
          "type": "color",
          "offset": 16,
          "default": [0.0, 0.0, 0.0, 0.15]
        }
      ]
    }
  ],
  "variants": [
    {
      "format": "msl",
      "source_handle": "7215eb4de35c643b",
      "entry_point": "main0"
    }
  ]
}
