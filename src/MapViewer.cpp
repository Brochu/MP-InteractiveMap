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

#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "stdafx.h"

#include <array>
#include <format>

MapViewer::MapViewer(UINT width, UINT height, std::wstring name)
    : DXSample(width, height, name), m_frameIndex(0), m_width(width), m_height(height),
      m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
      m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)), m_fenceValues{},
      m_rtvDescriptorSize(0) {}

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
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = FrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

        m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

        // Depth targets creation settings
        D3D12_HEAP_PROPERTIES depthProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthClear{DXGI_FORMAT_D32_FLOAT, {1.f, 0}};

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++) {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);

            // TODO: Need to create an extra render target per active frames
            // We store the main render pass in the intermediate RT
            // Then we render the full screen effect with edge detection after
            // full screen effect is an empty draw with 3 verts
            // we then use SV_VertexID to create UVs and positions for a full screen tri

            ThrowIfFailed(m_device->CreateCommittedResource(&depthProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                                            IID_PPV_ARGS(&m_depthTargets[n])));
            m_device->CreateDepthStencilView(m_depthTargets[n].Get(), nullptr, dsvHandle);
            dsvHandle.Offset(1, m_dsvDescriptorSize);

            ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           IID_PPV_ARGS(&m_commandAllocators[n])));
        }
    }
}

// Load the sample assets.
void MapViewer::LoadAssets() {
    // Create an empty root signature.
    {
        CD3DX12_ROOT_PARAMETER1 constBufferParam;
        constBufferParam.InitAsConstantBufferView(0);

        CD3DX12_ROOT_PARAMETER1 constParam;
        constParam.InitAsConstants(4, 1);
        CD3DX12_ROOT_PARAMETER1 params[]{constBufferParam, constParam};

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(2, params, 0, nullptr,
                                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
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

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders/shaders.hlsl").c_str(), nullptr, nullptr,
                                         "VSMain", "vs_5_1", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders/shaders.hlsl").c_str(), nullptr, nullptr,
                                         "PSMain", "ps_5_1", compileFlags, 0, &pixelShader, nullptr));

        //  Define the vertex input layout.
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
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

        D3D12_RASTERIZER_DESC wireRaster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        wireRaster.FillMode = D3D12_FILL_MODE_WIREFRAME;
        psoDesc.RasterizerState = wireRaster;
        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_wirePipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              m_commandAllocators[m_frameIndex].Get(), m_pipelineState.Get(),
                                              IID_PPV_ARGS(&m_commandList)));

    // Load 3D model map data
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    m_vertOffsets = {};
    m_indOffsets = {};
    {
        static const std::array<std::string, 7> worlds{"IntroWorld", "RuinsWorld", "IceWorld",   "OverWorld",
                                                       "MinesWorld", "LavaWorld",  "CraterWorld"};

        for (auto &world : worlds) {
            std::string filepath = std::format("data/{}.obj", world);

            Assimp::Importer importer;
            const aiScene *scene = importer.ReadFile(
                filepath.c_str(), aiProcess_ConvertToLeftHanded | aiProcessPreset_TargetRealtime_MaxQuality |
                                      aiProcess_PreTransformVertices);
            printf("[SCENE] numMeshes = %i (%s)\n", scene->mNumMeshes, filepath.c_str());
            aiMesh *mesh = scene->mMeshes[0]; // For the map models, we only have one mesh

            for (UINT i = 0; i < mesh->mNumVertices; i++) {
                aiVector3D vert = mesh->mVertices[i];
                aiVector3D norm = mesh->mNormals[i];
                vertices.push_back({{vert.x, vert.y, vert.z, 1.f}, {norm.x, norm.y, norm.z, 0.f}});
            }
            for (UINT i = 0; i < mesh->mNumFaces; i++) {
                for (UINT j = 0; j < mesh->mFaces[i].mNumIndices; j++) {
                    indices.emplace_back(mesh->mFaces[i].mIndices[j]);
                }
            }
            m_vertOffsets.emplace_back(vertices.size());
            m_indOffsets.emplace_back(indices.size());
        }
    }

    // Create the vertex buffer.
    {
        const UINT vertexBufferSize = sizeof(Vertex) * (UINT)vertices.size();
        const UINT indexBufferSize = sizeof(unsigned int) * (UINT)indices.size();

        // Note: using upload heaps to transfer static data like vert buffers is
        // not recommended. Every time the GPU needs it, the upload heap will be
        // marshalled over. Please read up on Default Heap usage. An upload heap
        // is used here for code simplicity and because there are very few verts
        // to actually transfer.
        // TODO: Copy the vertex data over to default heap later
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC vBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vBufferDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&m_vertexBuffer)));

        D3D12_RESOURCE_DESC iBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);

        ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &iBufferDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&m_indexBuffer)));
        // TODO: Copy vertex data to default vram heap
        // TODO: Also prepare, upload and copy indices data to default vram for
        // loaded models
        // TODO: Load all different map regions, only render a selected one from
        // the group

        // Copy the triangle data to the vertex buffer.
        UINT8 *pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
        m_vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

        UINT8 *pIndexDataBegin;
        ThrowIfFailed(m_indexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pIndexDataBegin)));
        memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
        m_indexBuffer->Unmap(0, nullptr);

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        m_indexBufferView.SizeInBytes = indexBufferSize;
    }

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());

    // TODO: Execute command list to start upload process

    // Create synchronization objects and wait until assets have been uploaded
    // to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE,
                                            IID_PPV_ARGS(&m_fence)));
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
    XMVECTOR translate = {(float)m_xt, (float)m_yt, (float)m_zt, 0.f};
    XMMATRIX translateMat = XMMatrixTranslationFromVector(translate);
    XMMATRIX rotationMat = XMMatrixRotationRollPitchYaw(XMConvertToRadians((float)-m_xmap),
                                                        XMConvertToRadians((float)-m_ymap), 0.f);

    XMMATRIX model = XMMatrixMultiply(XMMatrixIdentity(), translateMat);
    XMVECTOR campos = XMVector4Transform(m_camera, rotationMat);
    XMMATRIX view = XMMatrixLookAtLH(campos, m_lookat, m_updir);

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
        m_yt += (m_my - y);
    } else if (RButton) {
        m_zt -= (m_my - y);
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
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRoot32BitConstant(1, 0, 0);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that the back buffer will be used as a render target.
    D3D12_RESOURCE_BARRIER render_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &render_barrier);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
                                            m_rtvDescriptorSize);
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
                                            m_dsvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Record commands.
    const float clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    UINT indexStart = 0;
    UINT vertexStart = 0;
    if (m_mapIndex > 0) {
        indexStart = (UINT)m_indOffsets[m_mapIndex - 1];
        vertexStart = (UINT)m_vertOffsets[m_mapIndex - 1];
    }
    m_commandList->DrawIndexedInstanced((UINT)m_indOffsets[m_mapIndex] - indexStart, 1, indexStart,
                                        vertexStart, 0);
    // TODO: Find a better way to package draw calls
    //  Do we need to split each map in a draw call per room?

    // TODO: Implement the wireframe render with a full screen effect
    // Need to look into a edge detection algorithm
    m_commandList->SetPipelineState(m_wirePipelineState.Get());
    m_commandList->SetGraphicsRoot32BitConstant(1, 1, 0);
    m_commandList->DrawIndexedInstanced((UINT)m_indOffsets[m_mapIndex] - indexStart, 1, indexStart,
                                        vertexStart, 0);
    m_commandList->SetPipelineState(m_pipelineState.Get());

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
