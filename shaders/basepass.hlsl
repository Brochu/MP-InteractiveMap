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

struct PSInput {
    float4 position : SV_POSITION;
    float4 normal : NORMAL;
};

struct PSOutput {
    float4 color : SV_TARGET0;
    float4 normal : SV_TARGET1;
};

PSInput VSMain(float4 position : POSITION, float4 normal : NORMAL) {
    PSInput result;

    result.position = mul(mvp, position);
    result.normal = mul(mvp, normal);

    return result;
}

PSOutput PSMain(PSInput input) {
    float3 norm = input.normal.xyz;

    PSOutput output;
    output.color = float4(0.61, 0.33, 0.0, 0.75);
    output.normal = float4(norm.x, norm.y, norm.z, 1.0);
    return output;
}
