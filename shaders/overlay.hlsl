Texture2D energyT : register(t0);
Texture2D missileT : register(t1);

SamplerState s : register(s0);

struct IconVert {
    float3 pos[6];
    float2 uvs[6];
};

StructuredBuffer<IconVert> vertexBuffer : register(t2);
StructuredBuffer<uint> typeBuffer : register(t3);

cbuffer PerDraw : register(b0) { uint instanceOffset; };
cbuffer PerFrame : register(b1) {
    float4x4 mvp;
    float4x4 world;
};

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
    uint vertIdx = instanceOffset + input.InstId;
    IconVert v = vertexBuffer.Load(vertIdx);

    PSIn output;
    output.Pos = mul(mvp, float4(v.pos[input.VertId], 1.0));
    output.Uvs = v.uvs[input.VertId];
    output.Type = vertIdx;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET {
    uint vertIdx = (uint)input.Type;
    uint type = typeBuffer.Load(vertIdx);

    if (type == 0) {
        return energyT.Sample(s, input.Uvs);
    } else {
        return missileT.Sample(s, input.Uvs);
    }
}
