#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <dxgi1_6.h>
#include <iostream>
#include <d3dcompiler.h>

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::runtime_error("HRESULT failed");
    }
}

class Renderer {
public:
    Renderer();
    ~Renderer();

    
    void Initialize(HWND hwnd);
    void Render();
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
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

    void CreateDevice();
    void CreateDescriptorHeaps();
    void CreateFence();
    void CreateConstantBuffer();
    void CreateDepthStencilBuffer();
    void CreateRenderTargetView();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void SetViewportAndScissor(UINT width, UINT height);
    void FlushCommandQueue();

    HRESULT hr;
    Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencil;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap; 
    Microsoft::WRL::ComPtr<ID3D12Resource> m_swapChainBuffer[SwapChainBufferCount];
};

