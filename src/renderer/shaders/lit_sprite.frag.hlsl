cbuffer LitSpriteUniforms : register(b0, space3)
{
    float4 light_direction_intensity;
    float4 light_color_ambient;
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
    float3 light_direction = normalize(light_direction_intensity.xyz);
    float diffuse = max(dot(normal, light_direction), 0.0);
    float3 lighting = light_color_ambient.www +
        light_color_ambient.rgb * light_direction_intensity.w * diffuse;
    float3 reflected = reflect(-light_direction, normal);
    float highlight = pow(max(reflected.z, 0.0), max(surface.z, 1.0));
    float3 specular = specular_map * surface.y * highlight *
        light_color_ambient.rgb * light_direction_intensity.w;
    float3 emission = emissive * emissive_tint_intensity.rgb * emissive_tint_intensity.w;
    return float4(albedo.rgb * lighting + specular + emission, albedo.a);
}
