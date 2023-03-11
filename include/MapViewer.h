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

#pragma once

#include "DXSample.h"
#include <DirectXMath.h>

#include <array>

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the
// CPU, it has no understanding of the lifetime of resources on the GPU. Apps
// must account for the GPU lifetime of resources to avoid destroying objects
// that may still be referenced by the GPU. An example of this can be found in
// the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class MapViewer : public DXSample {
public:
    MapViewer(UINT width, UINT height, std::wstring name);

    virtual void OnInit() override;
    virtual void OnUpdate() override;
    virtual void OnRender() override;
    virtual void OnDestroy() override;

    virtual void OnKeyDown(UINT8 key) override;
    virtual void OnMouseMove(short x, short y, bool LButton, bool RButton, bool ctrl) override;
    virtual void OnMouseWheel(short z) override;

private:
    // In this sample we overload the meaning of FrameCount to mean both the
    // maximum number of frames that will be queued to the GPU at a time, as
    // well as the number of back buffers in the DXGI swap chain. For the
    // majority of applications, this is convenient and works well. However,
    // there will be certain cases where an application may want to queue up
    // more frames than there are back buffers available. It should be noted
    // that excessive buffering of frames dependent on user input may result
    // in noticeable latency in your app.
    static const UINT FrameCount = 2;
    static const UINT WorldCount = 7;

    struct Vertex {
        Vertex(XMFLOAT4 p, XMFLOAT4 n) {
            position = p;
            normal = n;
        }

        XMFLOAT4 position;
        XMFLOAT4 normal;
    };

    struct Geometry {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
    };

    struct Draws {
        std::vector<size_t> indexStarts;
        std::vector<size_t> vertexStarts;
        std::vector<size_t> indexCount;
    };

    struct ConstantBuffer {
        XMMATRIX mvp;
        XMMATRIX world;
    };

    struct ItemMetadata {
        UINT worldIndex;
        UINT roomIndex;
        XMVECTOR position;
    };

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_interRTs[FrameCount];
    ComPtr<ID3D12Resource> m_depthTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12RootSignature> m_postRootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_postPipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;
    UINT m_srvDescriptorSize;

    // App resources.
    ComPtr<ID3D12Resource> m_uploadBuffer;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

    ComPtr<ID3D12Resource> m_constBuffer;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount];

    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_mapIndex = 0;
    UINT m_mx = 0;
    UINT m_my = 0;

    int m_ymap = 0;
    int m_xmap = 0;
    int m_xt = 0;
    int m_yt = 0;
    int m_zt = 0;
    XMVECTOR m_camera = {0.0, 0.0, -600.0, 1.0};
    XMVECTOR m_lookat = {0.0, 0.0, 0.0, 1.0};
    XMVECTOR m_updir = {0.0, 1.0, 0.0, 0.0};
    float m_fov = 45.0;

    std::array<Draws, WorldCount> m_worldDraws;
    std::array<std::vector<ItemMetadata>, WorldCount> m_worldItems;

    void LoadPipeline();
    void LoadAssets();
    void PopulateCommandList();
    void MoveToNextFrame();
    void WaitForGpu();
};
