#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <dxgi1_6.h>
#include <iostream>
#include <d3dcompiler.h>

#include "MathHelper.h"
#include "d3dUtil.h"
#include "UploadBuffer.h"
#include "Camera.h"
#include "FrameResource.h"

struct RenderItem
{
	RenderItem() = default;

    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4(); //该几何体的世界矩阵
	UINT ObjCBIndex = -1; //该几何体的常量数据在objConstantBuffer中的索引
	MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
    int NumFramesDirty = gNumFrameResources;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    
    void Initialize(HWND hwnd);
    void Render();
    void Update();
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    ID3D12Resource* CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

private:
    UINT m_width = 1280;  
    UINT m_height = 720; 
    UINT mRtvDescriptorSize = 0;     
    UINT mDsvDescriptorSize = 0;    
    UINT mCbvSrvDescriptorSize = 0; 
    UINT mCurrentFence = 0;
    int mCurrBackBuffer = 0;
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    bool m4xMsaaState = false; // 是否启用 MSAA
    UINT m4xMsaaQuality = 0;   // MSAA 质量级别
    static const UINT SwapChainBufferCount = 2; 


    //3缓冲
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;
    void BuildFrameResources();
 

    //初始化
    void CreateDevice();
    void CreateDescriptorHeaps();
    void CreateFence();
    void CreateConstantBuffer();
    void CreateDepthStencilBuffer();
    void CreateRenderTargetView();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildPSO();
    void SetViewportAndScissor(UINT width, UINT height);
    void FlushCommandQueue();
    void ProcessInput();

    HRESULT hr;
    Camera m_camera;
    Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap; 
    Microsoft::WRL::ComPtr<ID3D12Resource> m_swapChainBuffer[SwapChainBufferCount];

    //画多个物体
    void BuildRenderItem();
    void DrawRenderItems(ID3D12GraphicsCommandList* m_commandList,const std::vector<RenderItem*>& ritems);
    void UpdateCamera();
    void UpdateObjectCBs();
    void UpdateMainPassCB();
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::vector<RenderItem*> mOpaqueRitems;
    //std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    //std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> objCB = nullptr;
    std::unique_ptr<UploadBuffer<PassConstants>> passCB = nullptr;
    std::unique_ptr<MeshGeometry> geo = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayout;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3DBlob> mvsByteCode = nullptr; 
    Microsoft::WRL::ComPtr<ID3DBlob> mpsByteCode = nullptr; 
    DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();
};

