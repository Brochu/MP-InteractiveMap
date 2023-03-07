//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

cbuffer PerFrame : register(b0) {
    float4x4 mvp;
    float4x4 world;
};

cbuffer PerDraw : register(b1) { uint val; }

struct PSInput {
    float4 position : SV_POSITION;
    float4 normal : NORMAL;
};

PSInput VSMain(float4 position : POSITION, float4 normal : NORMAL) {
    PSInput result;

    result.position = mul(mvp, position);
    result.normal = normal;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
    if (val > 0) {
        return float4(0.71, 0.43, 0.30, 1.0);
    }
    return float4(input.normal.xyz, 1.0);
    // return float4(0.41, 0.13, 0.0, 0.5);
}
