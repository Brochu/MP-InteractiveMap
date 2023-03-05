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
    virtual void OnMouseMove(short x, short y) override;
    virtual void OnMouseWheel(short z) override;
    virtual void OnMouseLButton(bool state) override;
    virtual void OnMouseRButton(bool state) override;

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

    struct Vertex {
        Vertex(float x, float y, float z) {
            position.x = x;
            position.y = y;
            position.z = z;
        }

        XMFLOAT3 position;
    };

    struct ConstantBuffer {
        XMMATRIX mvp;
        XMMATRIX world;
    };

    // Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12Resource> m_depthTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12PipelineState> m_wirePipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;
    UINT m_dsvDescriptorSize;

    // App resources.
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
    bool m_LDown = false;
    bool m_RDown = false;

    XMMATRIX m_rotation = XMMatrixIdentity();
    FXMVECTOR m_camera = {0.0, 150.0, -600.0, 1.0};
    FXMVECTOR m_lookat = {0.0, 0.0, 0.0, 1.0};
    FXMVECTOR m_updir = {0.0, 1.0, 0.0, 0.0};
    float m_fov = 45.0;

    std::vector<size_t> m_vertOffsets;
    std::vector<size_t> m_indOffsets;

    void LoadPipeline();
    void LoadAssets();
    void PopulateCommandList();
    void MoveToNextFrame();
    void WaitForGpu();
};
