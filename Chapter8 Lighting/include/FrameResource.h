#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 world = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct PassConstants
{
    //DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    //DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    //DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    //DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    //DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 eyePosW = { 0.0f, 0.0f, 0.0f };
    float gTotalTime;  
    DirectX::XMFLOAT4 ambientLight = { 0.0f,0.0f,0.0f,1.0f };
    Light Lights[MaxLights];
    
    //DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    //DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    //float cbPerObjectPad1 = 0.0f;
    //DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    //DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    //float NearZ = 0.0f;
    //float FarZ = 0.0f;
    //float TotalTime = 0.0f;
    //float DeltaTime = 0.0f;
};

struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT3 Normal;
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 Fence = 0;
};