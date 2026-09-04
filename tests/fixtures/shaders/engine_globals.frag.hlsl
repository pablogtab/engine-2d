cbuffer MaterialProbe : register(b0, space3)
{
    float4 probe;
};

cbuffer EngineGlobals : register(b1, space3)
{
    float4 engine_time;
    float4 engine_viewport;
};

struct FragmentInput
{
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(FragmentInput input) : SV_Target
{
    return float4(
        input.uv.x,
        engine_time.z * 10.0,
        probe.x,
        input.color.a
    );
}
