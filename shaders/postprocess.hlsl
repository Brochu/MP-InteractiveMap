//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard
//
// A vertex shader for full-screen effects without a vertex buffer.  The
// intent is to output an over-sized triangle that encompasses the entire
// screen.  By doing so, we avoid rasterization inefficiency that could
// result from drawing two triangles with a shared edge.
//
// Use null input layout
// Draw(3)

// TODO: Bind srvs for intermediate RTs
Texture2D colorRT : register(t0);
Texture2D normalRT : register(t1);

SamplerState s : register(s0);

void VSMain(in uint VertID : SV_VertexID, out float4 Pos : SV_Position, out float2 Tex : TexCoord0) {
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    Tex = float2(uint2(VertID, VertID << 1) & 2);
    Pos = float4(lerp(float2(-1, 1), float2(1, -1), Tex), 0, 1);
}

float4 PSMain(float4 pos : SV_Position, float2 tex : TEXCOORD0) : SV_TARGET {
    return float4(0.71, 0.43, 0.30, 1.0);
}
