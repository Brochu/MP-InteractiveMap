Texture2D energyT : register(t0);
Texture2D missileT : register(t1);

SamplerState s : register(s0);

// TODO: Need to bind the PerFrame Constant Buffer for mvp matrix

struct IconVert {
    float3 pos[6];
    float2 uvs[6];
};

StructuredBuffer<IconVert> vertexBuffer : register(t2);
StructuredBuffer<uint> typeBuffer : register(t3); // Might need a mask for 8bit values

cbuffer PerDraw : register(b0) { uint instanceOffset; };

struct VSIn {
    uint VertId : SV_VertexID;
    uint InstId : SV_InstanceID;
};

struct PSIn {
    float4 Pos : SV_Position;
    float2 Uvs : TEXCOORD0;
    float Type : TEXCOORD1;
};

PSIn VSMain(VSIn input) {
    // TODO: Implement getting the right vertex data based on InstanceId
    // Get the vertex position and UVs to send to pixel shader
    uint vertIdx = instanceOffset + input.InstId;
    IconVert v = vertexBuffer.Load(vertIdx);

    PSIn output;
    output.Pos = float4(v.pos[input.VertId], 1.0);
    output.Uvs = v.uvs[input.VertId];
    output.Type = vertIdx;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET {
    // TODO: Use the item type and UVs to sample the right texture for the current icon
    // Maybe look into some effects later on?
    // Will the transparency work here?

    uint idx = (uint)input.Type;
    uint t = typeBuffer.Load(idx);
    float4 albedo;

    if (t == 0) {
        albedo = energyT.Sample(s, input.Uvs);
    } else {
        albedo = missileT.Sample(s, input.Uvs);
    }

    return float4(1.0, 1.0, 1.0, 0.0);
}
