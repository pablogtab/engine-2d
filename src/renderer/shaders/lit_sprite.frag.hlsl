cbuffer LitSpriteUniforms : register(b0, space3)
{
    float4 light_directions_intensity[4];
    float4 light_colors[4];
    float4 material;
    float4 emissive_tint_intensity;
    float4 surface;
};

Texture2D material_pack : register(t0, space2);
SamplerState material_sampler : register(s0, space2);

struct FragmentInput
{
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target
{
    float4 albedo = material_pack.Sample(material_sampler, input.uv) * input.color;
    float3 encoded_normal = material_pack.Sample(material_sampler, input.uv + float2(0.5, 0.0)).rgb;
    float3 emissive = material_pack.Sample(material_sampler, input.uv + float2(0.0, 0.5)).rgb;
    float3 specular_map = material_pack.Sample(material_sampler, input.uv + float2(0.5, 0.5)).rgb;
    float3 decoded_normal = normalize(encoded_normal * 2.0 - 1.0);
    float3 normal = normalize(lerp(float3(0.0, 0.0, 1.0), decoded_normal, surface.x));
    float3 realtime = float3(0.0, 0.0, 0.0);
    float3 specular = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint index = 0; index < 4; ++index) {
        if (index >= (uint)material.w) break;
        float3 light_direction = normalize(light_directions_intensity[index].xyz);
        float intensity = light_directions_intensity[index].w;
        float diffuse = max(dot(normal, light_direction), 0.0);
        realtime += light_colors[index].rgb * intensity * diffuse;
        float3 reflected = reflect(-light_direction, normal);
        float highlight = pow(max(reflected.z, 0.0), max(surface.z, 1.0));
        specular += specular_map * surface.y * highlight * light_colors[index].rgb * intensity;
    }
    float3 emission = emissive * emissive_tint_intensity.rgb * emissive_tint_intensity.w;
    // Composition contract: baked modulation, then realtime diffuse, then
    // specular and emissive terms. The last two are never baked into albedo.
    float3 baked = albedo.rgb * material.rgb;
    return float4(baked + albedo.rgb * realtime + specular + emission, albedo.a);
}
