cbuffer NightVisionUniforms : register(b0, space3)
{
    float4 tint;
    float4 shadow_tint;
};

Texture2D source_texture : register(t0, space2);
SamplerState source_sampler : register(s0, space2);

struct FragmentInput
{
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target
{
    float4 source = source_texture.Sample(source_sampler, input.uv) * input.color;
    float3 graded = source.rgb * tint.rgb;
    graded = lerp(graded, shadow_tint.rgb, saturate(shadow_tint.a));
    return float4(graded, source.a * tint.a);
}
