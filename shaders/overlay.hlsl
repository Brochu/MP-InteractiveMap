Texture2D colorRT : register(t0);
Texture2D normalRT : register(t1);

SamplerState s : register(s0);

cbuffer PerDraw : register(b0) {
    uint screenWidth;
    uint screenHeight;
}

void VSMain(in uint VertID : SV_VertexID, out float4 Pos : SV_Position, out float2 Tex : TexCoord0) {
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    Tex = float2(uint2(VertID, VertID << 1) & 2);
    Pos = float4(lerp(float2(-1, 1), float2(1, -1), Tex), 0, 1);
}

float4 PSMain(float4 pos : SV_Position, float2 tex : TEXCOORD0) : SV_TARGET {
    float4 albedo = float4(0.0, 0.0, 0.0, 0.0);
    return albedo;
}
