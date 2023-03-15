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

float step = 1.0;
float intensity(float3 color) {
    return sqrt((color.x * color.x) + (color.y * color.y) + (color.z * color.z));
}

// Src: https://www.shadertoy.com/view/Xdf3Rf
float4 sobel(float stepx, float stepy, float2 center) {
    // get samples around pixel
    float tleft = intensity(colorRT.Sample(s, center + float2(-stepx, stepy)).xyz);
    float left = intensity(colorRT.Sample(s, center + float2(-stepx, 0)).xyz);
    float bleft = intensity(colorRT.Sample(s, center + float2(-stepx, -stepy)).xyz);
    float top = intensity(colorRT.Sample(s, center + float2(0, stepy)).xyz);
    float bottom = intensity(colorRT.Sample(s, center + float2(0, -stepy)).xyz);
    float tright = intensity(colorRT.Sample(s, center + float2(stepx, stepy)).xyz);
    float right = intensity(colorRT.Sample(s, center + float2(stepx, 0)).xyz);
    float bright = intensity(colorRT.Sample(s, center + float2(stepx, -stepy)).xyz);

    // Sobel masks (see http://en.wikipedia.org/wiki/Sobel_operator)
    //        1 0 -1     -1 -2 -1
    //    X = 2 0 -2  Y = 0  0  0
    //        1 0 -1      1  2  1

    // You could also use Scharr operator:
    //        3 0 -3        3 10   3
    //    X = 10 0 -10  Y = 0  0   0
    //        3 0 -3        -3 -10 -3

    float x = tleft + 2.0 * left + bleft - tright - 2.0 * right - bright;
    float y = -tleft - 2.0 * top - tright + bleft + 2.0 * bottom + bright;
    float color = sqrt((x * x) + (y * y));
    return float4(color, color, color, 1.0);
}

float4 PSMain(float4 pos : SV_Position, float2 tex : TEXCOORD0) : SV_TARGET {
    // Need to look into a edge detection algorithm
    float dx = 1.0 / screenWidth;
    float dy = 1.0 / screenHeight;

    float4 albedo = colorRT.Sample(s, tex);
    return sobel(dx, dy, tex) + albedo;
}
