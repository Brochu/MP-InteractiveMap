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

#include "MapViewer.h"
#include "DXSampleHelper.h"
#include "ImageIO.h"

#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "stdafx.h"

#include <format>
#include <fstream>
#include <sstream>

void ShaderCompile(std::wstring path, const char *entry, const char *target, UINT flags,
                   ComPtr<ID3DBlob> &shader) {
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr, nullptr, entry, target, flags, 0, &shader, &error);

#if _DEBUG
    if (!SUCCEEDED(hr)) {
        auto errorMessage = static_cast<unsigned char *>(error->GetBufferPointer());
        printf("[SHADER][ERROR] %s\n", errorMessage);
    }
#endif
    ThrowIfFailed(hr);
}

MapViewer::MapViewer(UINT width, UINT height, std::wstring name)
    : DXSample(width, height, name), m_frameIndex(0), m_width(width), m_height(height),
      m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
      m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)), m_fenceValues{},
      m_rtvDescriptorSize(0) {

    ::CoInitializeEx(nullptr, ::COINIT_APARTMENTTHREADED | ::COINIT_DISABLE_OLE1DDE);
}

void MapViewer::OnInit() {
    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void MapViewer::LoadPipeline() {
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the
    // active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice) {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    } else {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(
            D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    NAME_D3D12_OBJECT(m_commandQueue);

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(m_commandQueue.Get(), // Swap chain needs the queue so that
                                                                        // it can force a flush on it.
                                                  Win32Application::GetHwnd(), &swapChainDesc, nullptr,
                                                  nullptr, &swapChain));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount * 3;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
        NAME_D3D12_OBJECT(m_rtvHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = FrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
        NAME_D3D12_OBJECT(m_dsvHeap);

        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.NumDescriptors = 2 + (FrameCount * 2); // Two icon textures + intermediate RTs
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));
        NAME_D3D12_OBJECT(m_srvHeap);

        m_srvDescriptorSize =
            m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE normalRtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        normalRtvHandle.Offset(FrameCount, m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE colorRtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        colorRtvHandle.Offset(FrameCount * 2, m_rtvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE normalSrvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
        normalSrvHandle.Offset(FrameCount, m_srvDescriptorSize);
        CD3DX12_CPU_DESCRIPTOR_HANDLE colorSrvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());
        colorSrvHandle.Offset(FrameCount * 2, m_srvDescriptorSize);

        D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        // Depth targets creation settings
        D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthClear{DXGI_FORMAT_D32_FLOAT, {1.f, 0}};

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++) {
            // Main render targets, post process will render to those
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            // Intermediate render targets, the main render pass will go there
            // Post process step takes this as input SRV
            D3D12_RESOURCE_DESC interRTDesc = m_renderTargets[n]->GetDesc();
            D3D12_CLEAR_VALUE interRTClear{interRTDesc.Format};
            interRTClear.Color[0] = 0.f;
            interRTClear.Color[1] = 0.f;
            interRTClear.Color[2] = 0.f;
            interRTClear.Color[3] = 1.f;
            ThrowIfFailed(m_device->CreateCommittedResource(
                &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &interRTDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &interRTClear, IID_PPV_ARGS(&m_normalRTs[n])));
            m_device->CreateRenderTargetView(m_normalRTs[n].Get(), nullptr, normalRtvHandle);
            normalRtvHandle.Offset(1, m_rtvDescriptorSize);
            m_device->CreateShaderResourceView(m_normalRTs[n].Get(), nullptr, normalSrvHandle);
            normalSrvHandle.Offset(1, m_srvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommittedResource(
                &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &interRTDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &interRTClear, IID_PPV_ARGS(&m_colorRTs[n])));
            m_device->CreateRenderTargetView(m_colorRTs[n].Get(), nullptr, colorRtvHandle);
            colorRtvHandle.Offset(1, m_rtvDescriptorSize);
            m_device->CreateShaderResourceView(m_colorRTs[n].Get(), nullptr, colorSrvHandle);
            colorSrvHandle.Offset(1, m_srvDescriptorSize);

            // Depth stencil targets
            ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE,
                                                            &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                            &depthClear, IID_PPV_ARGS(&m_depthTargets[n])));
            m_device->CreateDepthStencilView(m_depthTargets[n].Get(), nullptr, dsvHandle);
            NAME_D3D12_OBJECT_INDEXED(m_depthTargets, n);
            dsvHandle.Offset(1, m_dsvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           IID_PPV_ARGS(&m_commandAllocators[n])));
            NAME_D3D12_OBJECT_INDEXED(m_commandAllocators, n);
        }
    }
}

// Load the sample assets.
void MapViewer::LoadAssets() {
    // Create root signatures.
    {
        CD3DX12_ROOT_PARAMETER1 constBufferParam;
        constBufferParam.InitAsConstantBufferView(0);

        CD3DX12_ROOT_PARAMETER1 baseParams[]{constBufferParam};
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(1, baseParams, 0, nullptr,
                                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
        NAME_D3D12_OBJECT(m_rootSignature);

        // ---------------------------------------------
        CD3DX12_DESCRIPTOR_RANGE1 srvRanges[2]{};
        srvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                          FrameCount * 2);
        srvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                          FrameCount);
        CD3DX12_ROOT_PARAMETER1 srvTableParam;
        srvTableParam.InitAsDescriptorTable(2, srvRanges, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_ROOT_PARAMETER1 cbvParam;
        cbvParam.InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC sampleDesc;
        sampleDesc.Init(0);

        CD3DX12_ROOT_PARAMETER1 params[]{srvTableParam, cbvParam};
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC postRootSignatureDesc;
        postRootSignatureDesc.Init_1_1(2, params, 1, &sampleDesc, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> postSignature;
        ComPtr<ID3DBlob> postError;
        ThrowIfFailed(
            D3D12SerializeVersionedRootSignature(&postRootSignatureDesc, &postSignature, &postError));
        ThrowIfFailed(m_device->CreateRootSignature(0, postSignature->GetBufferPointer(),
                                                    postSignature->GetBufferSize(),
                                                    IID_PPV_ARGS(&m_postRootSignature)));
        NAME_D3D12_OBJECT(m_postRootSignature);

        // ---------------------------------------------
        CD3DX12_DESCRIPTOR_RANGE1 srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0);
        CD3DX12_ROOT_PARAMETER1 srvIconTable;
        srvIconTable.InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_ROOT_PARAMETER1 srvIconVertices;
        srvIconVertices.InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                                 D3D12_SHADER_VISIBILITY_VERTEX);
        CD3DX12_ROOT_PARAMETER1 srvIconTypes;
        srvIconTypes.InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
                                              D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_ROOT_PARAMETER1 iconParams[]{srvIconTable, srvIconVertices, srvIconTypes};
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC overRootSignatureDesc;
        overRootSignatureDesc.Init_1_1(3, iconParams, 1, &sampleDesc, D3D12_ROOT_SIGNATURE_FLAG_NONE);

        ComPtr<ID3DBlob> overSignature;
        ComPtr<ID3DBlob> overError;
        ThrowIfFailed(
            D3D12SerializeVersionedRootSignature(&overRootSignatureDesc, &overSignature, &overError));
        ThrowIfFailed(m_device->CreateRootSignature(0, overSignature->GetBufferPointer(),
                                                    overSignature->GetBufferSize(),
                                                    IID_PPV_ARGS(&m_overRootSignature)));
        NAME_D3D12_OBJECT(m_overRootSignature);
    }

    // Create Constant Buffer for per-frame data
    {
        ConstantBuffer cb{XMMatrixIdentity(), XMMatrixIdentity()};

        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(cb));

        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&m_constBuffer)));

        UINT8 *p;
        CD3DX12_RANGE readRange(0, 0);
        ThrowIfFailed(m_constBuffer->Map(0, &readRange, reinterpret_cast<void **>(&p)));
        memcpy(p, &cb, sizeof(cb));
        m_constBuffer->Unmap(0, nullptr);
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> postVertexShader;
        ComPtr<ID3DBlob> postPixelShader;
        ComPtr<ID3DBlob> iconVertexShader;
        ComPtr<ID3DBlob> iconPixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ShaderCompile(GetAssetFullPath(L"shaders/basepass.hlsl"), "VSMain", "vs_5_1", compileFlags,
                      vertexShader);
        ShaderCompile(GetAssetFullPath(L"shaders/basepass.hlsl"), "PSMain", "ps_5_1", compileFlags,
                      pixelShader);
        ShaderCompile(GetAssetFullPath(L"shaders/post.hlsl"), "VSMain", "vs_5_1", compileFlags,
                      postVertexShader);
        ShaderCompile(GetAssetFullPath(L"shaders/post.hlsl"), "PSMain", "ps_5_1", compileFlags,
                      postPixelShader);
        ShaderCompile(GetAssetFullPath(L"shaders/overlay.hlsl"), "VSMain", "vs_5_1", compileFlags,
                      iconVertexShader);
        ShaderCompile(GetAssetFullPath(L"shaders/overlay.hlsl"), "PSMain", "ps_5_1", compileFlags,
                      iconPixelShader);

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
             0},
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 2;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
        NAME_D3D12_OBJECT(m_pipelineState);

        // Post process PSO creation
        D3D12_GRAPHICS_PIPELINE_STATE_DESC postpsoDesc{};
        postpsoDesc.InputLayout = {nullptr, 0};
        postpsoDesc.pRootSignature = m_postRootSignature.Get();
        postpsoDesc.VS = CD3DX12_SHADER_BYTECODE(postVertexShader.Get());
        postpsoDesc.PS = CD3DX12_SHADER_BYTECODE(postPixelShader.Get());
        postpsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        postpsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        postpsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        postpsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        postpsoDesc.DepthStencilState.DepthEnable = false;
        postpsoDesc.DepthStencilState.StencilEnable = false;
        postpsoDesc.SampleMask = UINT_MAX;
        postpsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        postpsoDesc.NumRenderTargets = 1;
        postpsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        postpsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        postpsoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(
            m_device->CreateGraphicsPipelineState(&postpsoDesc, IID_PPV_ARGS(&m_postPipelineState)));
        NAME_D3D12_OBJECT(m_postPipelineState);

        // Overlay pass PSO creation
        D3D12_GRAPHICS_PIPELINE_STATE_DESC overpsoDesc{};
        overpsoDesc.InputLayout = {nullptr, 0};
        overpsoDesc.pRootSignature = m_overRootSignature.Get();
        overpsoDesc.VS = CD3DX12_SHADER_BYTECODE(iconVertexShader.Get());
        overpsoDesc.PS = CD3DX12_SHADER_BYTECODE(iconPixelShader.Get());
        overpsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        overpsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        overpsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        overpsoDesc.DepthStencilState.DepthEnable = false;
        overpsoDesc.DepthStencilState.StencilEnable = false;
        overpsoDesc.SampleMask = UINT_MAX;
        overpsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        overpsoDesc.NumRenderTargets = 1;
        overpsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        overpsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        overpsoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(
            m_device->CreateGraphicsPipelineState(&overpsoDesc, IID_PPV_ARGS(&m_overPipelineState)));
        NAME_D3D12_OBJECT(m_overPipelineState);
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              m_commandAllocators[m_frameIndex].Get(), nullptr,
                                              IID_PPV_ARGS(&m_commandList)));
    NAME_D3D12_OBJECT(m_commandList);

    // Create the vertex buffer.
    {
        // Load 3d model maps for each worlds
        struct Geometry {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;
        };
        Geometry worldGeo;
        m_worldDraws = {};

        static const std::array<std::string, WorldCount> worlds{
            "IntroWorld", "RuinsWorld", "IceWorld", "OverWorld", "MinesWorld", "LavaWorld", "CraterWorld"};

        for (int i = 0; i < WorldCount; i++) {
            std::string filepath = std::format("data/{}.obj", worlds[i]);

            Assimp::Importer importer;
            const aiScene *scene = importer.ReadFile(filepath.c_str(), aiProcess_ConvertToLeftHanded);

            for (unsigned int j = 0; j < scene->mNumMeshes; j++) {
                m_worldDraws[i].indexStarts.emplace_back(worldGeo.indices.size());
                m_worldDraws[i].vertexStarts.emplace_back(worldGeo.vertices.size());

                aiMesh *mesh = scene->mMeshes[j];

                for (UINT i = 0; i < mesh->mNumVertices; i++) {
                    aiVector3D vert = mesh->mVertices[i];
                    aiVector3D norm = mesh->mNormals[i];
                    worldGeo.vertices.push_back(
                        {{vert.x, vert.y, vert.z, 1.f}, {norm.x, norm.y, norm.z, 0.f}});
                }
                for (UINT i = 0; i < mesh->mNumFaces; i++) {
                    for (UINT j = 0; j < mesh->mFaces[i].mNumIndices; j++) {
                        worldGeo.indices.emplace_back(mesh->mFaces[i].mIndices[j]);
                    }
                }

                m_worldDraws[i].indexCount.emplace_back(worldGeo.indices.size() -
                                                        m_worldDraws[i].indexStarts[j]);
                m_worldDraws[i].drawCount++;
            }
        }

        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from these resources on the CPU.

        const UINT vertexBufferSize = sizeof(Vertex) * (UINT)worldGeo.vertices.size();
        const UINT indexBufferSize = sizeof(unsigned int) * (UINT)worldGeo.indices.size();

        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC uploadBufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize + indexBufferSize);
        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
                                                        &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(&m_uploadBuffer)));
        NAME_D3D12_OBJECT(m_uploadBuffer);

        // TODO: Look into using placed resources for vertex/index buffers
        D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE,
                                                        &vertexBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        nullptr, IID_PPV_ARGS(&m_vertexBuffer)));
        NAME_D3D12_OBJECT(m_vertexBuffer);

        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

        D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
        ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE,
                                                        &indexBufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                        nullptr, IID_PPV_ARGS(&m_indexBuffer)));
        NAME_D3D12_OBJECT(m_indexBuffer);

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        m_indexBufferView.SizeInBytes = indexBufferSize;

        unsigned char *pUpload;
        ThrowIfFailed(m_uploadBuffer->Map(0, &readRange, (void **)&pUpload));
        memcpy(pUpload, worldGeo.vertices.data(), vertexBufferSize);
        memcpy(pUpload + vertexBufferSize, worldGeo.indices.data(), indexBufferSize);
        m_uploadBuffer->Unmap(0, nullptr);

        m_commandList->CopyBufferRegion(m_vertexBuffer.Get(), 0, m_uploadBuffer.Get(), 0, vertexBufferSize);
        m_commandList->CopyBufferRegion(m_indexBuffer.Get(), 0, m_uploadBuffer.Get(), vertexBufferSize,
                                        indexBufferSize);

        const CD3DX12_RESOURCE_BARRIER barriers[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
            CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                 D3D12_RESOURCE_STATE_INDEX_BUFFER)};

        m_commandList->ResourceBarrier(2, barriers);
    }

    // Load map metadata for icons overlay
    {
        std::ifstream f("data/items.data");
        std::string line;

        while (std::getline(f, line)) {
            std::stringstream ss(line);
            unsigned char itemType = 0;
            ss >> itemType;
            ss.ignore();
            UINT worldIndex = 0;
            ss >> worldIndex;
            ss.ignore();
            UINT roomIndex = 0;
            ss >> roomIndex;

            ss.ignore();

            float x, y, z;
            ss >> x;
            ss.ignore(2);
            ss >> y;
            ss.ignore(2);
            ss >> z;

            m_worldItems[worldIndex - 1].push_back({itemType, worldIndex, roomIndex, {x, y, z}});
        }

        struct IconGeometry {
            XMVECTOR pos[6];
            XMVECTOR uvs[6]; // X, Y = uvs
        };
        std::vector<IconGeometry> iconGeometry;
        std::vector<unsigned char> iconTypes;
        m_iconDraws = {};

        for (int i = 0; i < WorldCount; i++) {
            std::vector<ItemMetadata> &icons = m_worldItems[i];

            m_iconDraws[i].instanceCount = icons.size();
            m_iconDraws[i].instanceStart = iconTypes.size();

            for (int j = 0; j < icons.size(); j++) {
                iconGeometry.emplace_back();
                iconTypes.emplace_back(icons[j].type);
            }
        }

        const UINT geometrySize = sizeof(IconGeometry) * (UINT)iconGeometry.size();
        const UINT iconTypeSize = sizeof(char) * (UINT)iconTypes.size();

        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC iconBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(geometrySize);
        D3D12_RESOURCE_DESC typesBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(iconTypeSize);

        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
                                                        &iconBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(&m_iconVertices)));
        NAME_D3D12_OBJECT(m_iconVertices);
        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE,
                                                        &typesBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(&m_iconTypes)));
        NAME_D3D12_OBJECT(m_iconTypes);

        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from these resources on the CPU.
        unsigned char *pGeoUpload;
        ThrowIfFailed(m_iconVertices->Map(0, &readRange, (void **)&pGeoUpload));
        memcpy(pGeoUpload, iconGeometry.data(), geometrySize);
        m_iconVertices->Unmap(0, nullptr);

        // TODO: This buffer should not change and could be copied to default heap
        unsigned char *pTypesUpload;
        ThrowIfFailed(m_iconTypes->Map(0, &readRange, (void **)&pTypesUpload));
        memcpy(pTypesUpload, iconTypes.data(), iconTypeSize);
        m_iconTypes->Unmap(0, nullptr);
    }

    // Load icons used for items overlay
    {
        static const std::array<std::string, 2> iconfiles{"energytankIcon.png", "missileIcon.png"};

        static const D3D12_HEAP_PROPERTIES defaultProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        static const D3D12_HEAP_PROPERTIES uploadProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_srvHeap->GetCPUDescriptorHandleForHeapStart());

        for (int i = 0; i < iconfiles.size(); i++) {
            int width, height = 0;
            std::vector<uint8_t> texData =
                LoadImageFromFile(std::format("data/{}", iconfiles[i]).c_str(), 1, &width, &height);
            printf("[IMG][%s] (%i, %i) -> %lld\n", iconfiles[i].c_str(), width, height, texData.size());

            auto imgDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, width, height, 1, 1);
            m_device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &imgDesc,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&m_img[i]));

            auto uploadBufferSize = GetRequiredIntermediateSize(m_img[i].Get(), 0, 1);
            auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
            m_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&m_imgUploadBuffer[i]));

            D3D12_SUBRESOURCE_DATA srcData;
            srcData.pData = texData.data();
            srcData.RowPitch = width * 4;
            srcData.SlicePitch = width * height * 4;

            UpdateSubresources(m_commandList.Get(), m_img[i].Get(), m_imgUploadBuffer[i].Get(), 0, 0, 1,
                               &srcData);
            const auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
                m_img[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_commandList->ResourceBarrier(1, &transition);

            D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
            shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shaderResourceViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            shaderResourceViewDesc.Texture2D.MipLevels = 1;
            shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
            shaderResourceViewDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            m_device->CreateShaderResourceView(m_img[i].Get(), &shaderResourceViewDesc, srvHandle);
            srvHandle.Offset(1, m_srvDescriptorSize);
        }
        // TODO: Find a way to upload texture data in one batch
    }

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList *commandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, commandLists);

    // Create synchronization objects and wait until assets have been uploaded
    // to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(&m_fence)));
        NAME_D3D12_OBJECT(m_fence);
        m_fenceValues[m_frameIndex]++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command
        // list in our main loop but for now, we just want to wait for setup to
        // complete before continuing.
        WaitForGpu();
    }
}

// Update frame-based values.
void MapViewer::OnUpdate() {
    XMMATRIX model = XMMatrixIdentity();
    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(XMConvertToRadians((float)-m_xmap),
                                                     XMConvertToRadians((float)-m_ymap), 0.f);

    XMVECTOR camera = XMVector4Transform(m_camera, rotation);
    XMVECTOR lookat = XMVector4Transform(m_lookat, rotation);
    XMVECTOR translate{-(float)m_xt, -(float)m_yt, (float)m_zt, 0.f};
    camera += translate;
    lookat += translate;
    XMMATRIX view = XMMatrixLookAtLH(camera, lookat, m_updir);

    float aspect = (float)m_width / m_height;
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), aspect, 0.1f, 100000.0f);

    XMMATRIX mvp = XMMatrixMultiply(model, view);
    mvp = XMMatrixMultiply(mvp, projection);
    XMMATRIX world = XMMatrixTranspose(model);

    ConstantBuffer cb{mvp, world};

    UINT8 *p;
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_constBuffer->Map(0, &readRange, reinterpret_cast<void **>(&p)));
    memcpy(p, &cb, sizeof(cb));
    m_constBuffer->Unmap(0, nullptr);

    // -----------------------------------------
    // TODO: Convert this process to a compute shader
    // Maybe also look into setting up the overlay pass as an indirect draw
    // TODO: Update icon vertices and uvs based on view matrix
    for (int i = 0; i < WorldCount; i++) {
        auto &items = m_worldItems[i];

        for (int j = 0; j < items.size(); j++) {
            // TODO: Update vertices for all items based on camera position and look vector
            // We need to convert this logic to use DxMath + Dx12
            // D3DMATRIX mat;
            // lpDevice->GetTransform(D3DTRANSFORMSTATE_VIEW,&mat);
            // D3DVECTOR rightVect=Normalize(D3DVECTOR(mat._11,mat._21,mat._31))*size*0.5f;
            // D3DVECTOR upVect=Normalize(D3DVECTOR(mat._12,mat._22,mat._32))*size*0.5f;

            // verts[0]=D3DLVERTEX(loc-rightVect, color, 0, 0.0f, 0.0f);
            // verts[1]=D3DLVERTEX(loc+upVect, color, 0, 0.0f, 1.0f);
            // verts[2]=D3DLVERTEX(loc-upVect, color, 0, 1.0f, 0.0f);
            // verts[3]=D3DLVERTEX(loc+rightVect, color, 0, 1.0f, 1.0f);
            //  Except for our case, we will generate the full 6 vertices with duplicates
            //  to avoid using index buffers for first version, see to add index buffer later
        }
    }
}

// Render the scene.
void MapViewer::OnRender() {
    // Record all the commands we need to render the scene into the command
    // list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    ThrowIfFailed(m_swapChain->Present(1, 0));

    MoveToNextFrame();
}

void MapViewer::OnDestroy() {
    // Ensure that the GPU is no longer referencing resources that are about to
    // be cleaned up by the destructor.
    WaitForGpu();

    CloseHandle(m_fenceEvent);
}

void MapViewer::OnKeyDown(UINT8 key) {
    if (key >= '1' && key <= '7') {
        m_mapIndex = key - '0' - 1;
    }
}

void MapViewer::OnMouseMove(short x, short y, bool LButton, bool RButton, bool ctrl) {
    if (LButton) {
        // TODO: Look into a way to avoid gimble locks for rotations, using quats?
        m_ymap += (m_mx - x);
        if (m_ymap > 360)
            m_ymap -= 360;
        if (m_ymap < -360)
            m_ymap += 360;

        m_xmap += (m_my - y);
        if (m_xmap > 89)
            m_xmap = 89;
        if (m_xmap < -89)
            m_xmap = -89;
    }

    if (RButton && !ctrl) {
        m_xt -= (m_mx - x);
        m_zt -= (m_my - y);
    } else if (RButton) {
        m_yt += (m_my - y);
    }

    m_mx = x;
    m_my = y;
}

void MapViewer::OnMouseWheel(short deltaz) {
    static const short divisor = -60;
    m_fov += (float)deltaz / divisor;
}

void MapViewer::PopulateCommandList() {
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get()));

    // Set necessary state.
    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constBuffer->GetGPUVirtualAddress());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    D3D12_RESOURCE_BARRIER render_barrier[]{
        CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(),
                                             D3D12_RESOURCE_STATE_PRESENT,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_colorRTs[m_frameIndex].Get(),
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(m_normalRTs[m_frameIndex].Get(),
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    m_commandList->ResourceBarrier(3, render_barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                            (FrameCount * 2) + m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE normalRTV(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                            FrameCount + m_frameIndex, m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
                                            m_dsvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvs[2]{rtvHandle, normalRTV};
    m_commandList->OMSetRenderTargets(2, rtvs, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = {0.f, 0.f, 0.f, 1.f};
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearRenderTargetView(normalRTV, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    Draws &draw = m_worldDraws[m_mapIndex];
    for (size_t i = 0; i < draw.drawCount; i++) {
        m_commandList->DrawIndexedInstanced((UINT)draw.indexCount[i], 1, (UINT)draw.indexStarts[i],
                                            (UINT)draw.vertexStarts[i], 0);
    }

    D3D12_RESOURCE_BARRIER post_barrier[]{
        CD3DX12_RESOURCE_BARRIER::Transition(m_colorRTs[m_frameIndex].Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(m_normalRTs[m_frameIndex].Get(),
                                             D3D12_RESOURCE_STATE_RENDER_TARGET,
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    m_commandList->ResourceBarrier(2, post_barrier);

    m_commandList->SetPipelineState(m_postPipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_postRootSignature.Get());
    ID3D12DescriptorHeap *ppHeap[]{m_srvHeap.Get()};
    m_commandList->SetDescriptorHeaps(1, ppHeap);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRoot32BitConstant(1, m_width, 0);
    m_commandList->SetGraphicsRoot32BitConstant(1, m_height, 1);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
                                      m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    m_commandList->DrawInstanced(3, 1, 0, 0);

    // TODO: Add a new instanced draw to handle icons overlay here
    // Render icons on top of the final render target
    m_commandList->SetGraphicsRootSignature(m_overRootSignature.Get());
    m_commandList->SetDescriptorHeaps(1, ppHeap);
    m_commandList->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->SetGraphicsRootShaderResourceView(1, m_iconVertices->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootShaderResourceView(2, m_iconTypes->GetGPUVirtualAddress());

    // Indicate that the back buffer will now be used to present.
    D3D12_RESOURCE_BARRIER present_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &present_barrier);

    ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void MapViewer::WaitForGpu() {
    // Schedule a Signal command in the queue.
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void MapViewer::MoveToNextFrame() {
    // Schedule a Signal command in the queue.
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

    // Update the frame index.
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is
    // ready.
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}
