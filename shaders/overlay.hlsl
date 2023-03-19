Texture2D energyT : register(t0);
Texture2D missileT : register(t1);

SamplerState s : register(s0);

struct IconVert {
    float3 pos[6];
    float2 uvs[6];
};

StructuredBuffer<IconVert> vertexBuffer : register(t2);
Buffer<uint> typeBuffer : register(t3); // Might need a mask for 8bit values

void VSMain(in uint VertID : SV_VertexID, out float4 Pos : SV_Position, out float2 Tex : TexCoord0) {
    // TODO: Implement getting the right vertex data based on InstanceId
    // Get the vertex position and UVs to send to pixel shader
    Pos = float4(0.0, 0.0, 0.0, 1.0);
    Tex = float2(0.0, 0.0);
}

float4 PSMain(float4 pos : SV_Position, float2 tex : TEXCOORD0) : SV_TARGET {
    // TODO: Use the item type and UVs to sample the right texture for the current icon
    // Maybe look into some effects later on?
    // Will the transparency work here?
    float4 albedo = float4(0.0, 0.0, 0.0, 0.0);
    return albedo;
}
