#include "Renderer.h"
#include "d3dx12.h"
#include "d3dUtil.h"
#include <initguid.h>
#include "Camera.h"
#include "GeometryGenerator.h"

using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;
using namespace DirectX;
const int gNumFrameResources = 3;

Renderer::Renderer() : m_width(1280), m_height(720), mCurrBackBuffer(0), mCurrentFence(0){}
Renderer::~Renderer()
{
    if (m_device != nullptr)
        FlushCommandQueue();
}

void Renderer::Initialize(HWND hwnd)
{

#if defined(DEBUG) || defined(_DEBUG)  
{
    ComPtr<ID3D12Debug> debugController;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();
}
#endif

    CreateDevice();
    CreateFence();
    CreateCommandQueue();
    CreateSwapChain(hwnd);
    CreateDescriptorHeaps(); 
    CreateRenderTargetView();
    CreateDepthStencilBuffer();

    //draw
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
    
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildRenderItem();
    BuildFrameResources();
    BuildPSO();

    ThrowIfFailed(m_commandList->Close());

    ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

}

void Renderer::CreateDevice()
{
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory))); 
    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,                    // 默认适配器
        D3D_FEATURE_LEVEL_11_0,      // 最低特性级别
        IID_PPV_ARGS(&m_device)); // 创建的设备

    if (FAILED(hardwareResult)) {
        ComPtr<IDXGIAdapter> pWarpAdapter;
        ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
        ThrowIfFailed(D3D12CreateDevice(
            pWarpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_device)));
    }
}

void Renderer::CreateFence()
{
    // 创建一个 fence
    ThrowIfFailed(m_device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

    // 查询描述符大小并缓存
    mRtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Renderer::CreateCommandQueue()
{
    //命令队列 (Command Queue)
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    //命令分配器 (Command Allocator)
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator.GetAddressOf()))); 
    //命令列表 (Command List)
    ThrowIfFailed(m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(m_commandList.GetAddressOf())));
    //命令列表关闭 (Close Command List)
    m_commandList->Close();
}

void Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = m_width;
    sd.BufferDesc.Height = m_height;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = hwnd;
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


    ComPtr<IDXGISwapChain> swapChain;

    // 创建 SwapChain
    ThrowIfFailed(mdxgiFactory->CreateSwapChain(
        m_commandQueue.Get(),
        &sd,
        swapChain.GetAddressOf()
    ));

    // 获取 IDXGISwapChain4 接口
    ThrowIfFailed(swapChain.As(&m_swapChain));

}

void Renderer::CreateDescriptorHeaps()
{
    // 创建 RTV 堆
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = 2;  // 堆中描述符数量
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;  // 描述符类型是 RTV
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // 无特殊标志
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(m_device->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(m_rtvHeap.GetAddressOf())));

    // 创建 DSV 堆
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;  // 只需要一个深度/模板视图
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;  // 描述符类型是 DSV
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;  // 无特殊标志
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(m_device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(m_dsvHeap.GetAddressOf())));
}

// 获取当前后备缓冲区的渲染目标视图（RTV）句柄
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::CurrentBackBufferView() const
{
    // 通过 CD3DX12 构造函数计算偏移量，得到当前后备缓冲区的 RTV 描述符句柄
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart(),  // 堆的起始句柄
        mCurrBackBuffer,  // 当前后备缓冲区的索引
        mRtvDescriptorSize);  // RTV 描述符的字节大小
}

ID3D12Resource* Renderer::CurrentBackBuffer()const
{
    return m_swapChainBuffer[mCurrBackBuffer].Get();
}

// 获取深度/模板视图（DSV）的句柄
D3D12_CPU_DESCRIPTOR_HANDLE Renderer::DepthStencilView() const
{
    return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Renderer::CreateRenderTargetView()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart()); // 获取渲染目标视图堆的起始句柄

    for (UINT i = 0; i < SwapChainBufferCount; i++) {
        // 获取交换链中的第 i 个缓冲区
        ThrowIfFailed(m_swapChain->GetBuffer(
            i, IID_PPV_ARGS(&m_swapChainBuffer[i])));

        // 创建一个渲染目标视图 (RTV) 给当前的缓冲区
        m_device->CreateRenderTargetView(
            m_swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);

        // 移动到堆中的下一个描述符位置
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }
}

void Renderer::CreateDepthStencilBuffer()
{
    // 创建深度/模板缓冲区描述
    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 说明这是一个二维纹理资源
    depthStencilDesc.Alignment = 0;  // 默认对齐方式
    depthStencilDesc.Width = m_width;  // 缓冲区的宽度，通常为窗口的宽度
    depthStencilDesc.Height = m_height;  // 缓冲区的高度，通常为窗口的高度
    depthStencilDesc.DepthOrArraySize = 1;  // 资源的深度或数组大小，这里是 1，因为只有一个层
    depthStencilDesc.MipLevels = 1;  // 使用的 Mipmap 层数，通常深度缓冲区只有一个 Mipmap 层
    depthStencilDesc.Format = mDepthStencilFormat;  // 设置深度/模板缓冲区的格式
    depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;  // 如果开启 4x MSAA，样本数为 4，否则为 1
    depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;  // MSAA 的质量
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;  // 使用默认的纹理布局
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;  // 设置资源标志，表示这是一个深度/模板缓冲区

    // 设置清除值，用于初始化深度/模板缓冲区
    D3D12_CLEAR_VALUE optClear;
    optClear.Format = mDepthStencilFormat;  // 使用与缓冲区相同的格式
    optClear.DepthStencil.Depth = 1.0f;  // 深度值的初始化为 1.0
    optClear.DepthStencil.Stencil = 0;  // 模板值初始化为 0

    // 创建深度/模板缓冲区资源
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),  // 默认堆类型
        D3D12_HEAP_FLAG_NONE,  // 无堆标志
        &depthStencilDesc,  // 资源描述
        D3D12_RESOURCE_STATE_COMMON,  // 初始资源状态为 COMMON
        &optClear,  // 清除值
        IID_PPV_ARGS(m_depthStencil.GetAddressOf())));  // 获取深度/模板缓冲区的指针

    // 创建深度/模板视图
    m_device->CreateDepthStencilView(
        m_depthStencil.Get(),  // 深度/模板缓冲区
        nullptr,  // 使用默认格式
        DepthStencilView());  // 深度/模板视图

    // 过渡资源状态，从 COMMON 状态转换为 DEPTH_WRITE 状态
    m_commandList->ResourceBarrier(
        1, 
        &CD3DX12_RESOURCE_BARRIER::Transition(
            m_depthStencil.Get(),  // 深度/模板缓冲区资源
            D3D12_RESOURCE_STATE_COMMON,  // 初始状态
            D3D12_RESOURCE_STATE_DEPTH_WRITE));  // 目标状态
}

void Renderer::BuildRootSignature(){

    //定义根参数
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);

    //定义根签名描述符
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    //序列化根签名
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
    //创建根签名
	ThrowIfFailed(m_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&m_rootSignature)));
}

void Renderer::BuildShadersAndInputLayout(){

    mvsByteCode = d3dUtil::CompileShader(L"D:\\Personal Project\\D3D12book_code\\Chapter7 Land and Waves\\Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = d3dUtil::CompileShader(L"D:\\Personal Project\\D3D12book_code\\Chapter7 Land and Waves\\Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    m_InputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}
    
void Renderer::BuildRenderItem()
{
    /*
	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());
    */

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 1;
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	//mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	//mAllRitems.push_back(std::move(wavesRitem));
	mAllRitems.push_back(std::move(gridRitem));
    for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());

}

void Renderer::BuildShapeGeometry(){

    //创建几何体，并将顶点和索引列表存储在MeshData中
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

    //创建顶点缓存
	std::vector<Vertex> vertices(grid.Vertices.size());
	for(size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);

        // Color the vertex based on its height.
        if(vertices[i].Pos.y < -10.0f)
        {
            // Sandy beach color.
            vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
        }
        else if(vertices[i].Pos.y < 5.0f)
        {
            // Light yellow-green.
            vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
        }
        else if(vertices[i].Pos.y < 12.0f)
        {
            // Dark yellow-green.
            vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
        }
        else if(vertices[i].Pos.y < 20.0f)
        {
            // Dark brown.
            vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
        }
        else
        {
            // White snow.
            vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
	}
    
    //计算vector和indices的总size
    std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_device.Get(),
		m_commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_device.Get(),
		m_commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);

}

float Renderer::GetHillsHeight(float x, float z)const
{
    return 0.3f*(z*sinf(0.1f*x) + x*cosf(0.1f*z));
}

void Renderer::BuildPSO(){

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { m_InputLayout.data(), (UINT)m_InputLayout.size() };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), 
		mvsByteCode->GetBufferSize() 
	};
    psoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), 
		mpsByteCode->GetBufferSize() 
	};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}

void Renderer::SetViewportAndScissor(UINT width, UINT height) {
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = width;
    scissorRect.bottom = height;
    m_commandList->RSSetScissorRects(1, &scissorRect);
}

void Renderer::Update()
{
    UpdateCamera();
    //每帧遍历一个帧资源（多帧的话就是环形遍历）
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    //如果GPU端围栏值小于CPU端围栏值，即CPU速度快于GPU，则令CPU等待
    if (mCurrFrameResource->Fence != 0 && m_fence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(m_fence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
    UpdateObjectCBs();
    UpdateMainPassCB();

}

void Renderer::UpdateCamera(){
    ProcessInput(); 
    XMMATRIX view = m_camera.GetViewMatrix();  // 获取视图矩阵

}

void Renderer::UpdateObjectCBs(){
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for(auto& e : mAllRitems){
        if(e->NumFramesDirty > 0){
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world));
            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
            e->NumFramesDirty--;
        }
    }
}

void Renderer::UpdateMainPassCB(){
    PassConstants passConstants;
    XMMATRIX view = m_camera.GetViewMatrix();
    float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
    float fov = XMConvertToRadians(60.0f);
    float nearZ = 1.0f;
    float farZ = 1000.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
    XMStoreFloat4x4(&mProj, proj);
    XMMATRIX ViewProj =  view * proj;
    XMStoreFloat4x4(&passConstants.viewProj, XMMatrixTranspose(ViewProj));
    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, passConstants);
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* m_commandList,const std::vector<RenderItem*>& ritems){

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    //遍历渲染项数组
	for (size_t i = 0; i < ritems.size(); i++)
	{
		auto ritem = ritems[i];

		m_commandList->IASetVertexBuffers(0, 1, &ritem->Geo->VertexBufferView());
		m_commandList->IASetIndexBuffer(&ritem->Geo->IndexBufferView());
		m_commandList->IASetPrimitiveTopology(ritem->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ritem->ObjCBIndex*objCBByteSize;
    	m_commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);
		m_commandList->DrawIndexedInstanced(ritem->IndexCount, 1, ritem->StartIndexLocation, ritem->BaseVertexLocation, 0);

	}
}

void Renderer::Render()
{
    std::cout << "Render" << std::endl;
    // 重置命令分配器和命令列表
    auto currCmdAllocator = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(currCmdAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(currCmdAllocator.Get(), m_pipelineState.Get()));

    //设置视口和裁剪矩形
    SetViewportAndScissor(m_width,m_height);

    // 指定渲染目标
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    // 清屏
    m_commandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    m_commandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 设置我们要渲染的buffer
    m_commandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView()); //RTV
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get()); //RootSignature

    //绑定passCbv
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    auto passCB = mCurrFrameResource->PassCB->Resource();
    m_commandList->SetGraphicsRootConstantBufferView(1,passCB->GetGPUVirtualAddress());

    //渲染几何体
    DrawRenderItems(m_commandList.Get(),mOpaqueRitems);

    // 过渡到 PRESENT 状态
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT));

    // 关闭命令列表并执行
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    // 交换缓冲区
	ThrowIfFailed(m_swapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // 等待 GPU 完成
    mCurrFrameResource->Fence = ++mCurrentFence;
    m_commandQueue->Signal(m_fence.Get(), mCurrentFence);
}


void Renderer::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(m_device.Get(),
            1, (UINT)mAllRitems.size()));
    }
}


void Renderer::FlushCommandQueue()
{
    mCurrentFence++;

    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), mCurrentFence));

    if (m_fence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
        ThrowIfFailed(m_fence->SetEventOnCompletion(mCurrentFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void Renderer::ProcessInput()
{
    // 获取当前的键盘和鼠标输入
    if (GetAsyncKeyState('W') & 0x8000) {  // W 键（前进）
        m_camera.MoveForward(0.1f);
    }
    if (GetAsyncKeyState('S') & 0x8000) {  // S 键（后退）
        m_camera.MoveForward(-0.1f);
    }
    if (GetAsyncKeyState('A') & 0x8000) {  // A 键（左移）
        m_camera.MoveRight(-0.1f);
    }
    if (GetAsyncKeyState('D') & 0x8000) {  // D 键（右移）
        m_camera.MoveRight(0.1f);
    }
    if (GetAsyncKeyState('Q') & 0x8000) {  // Q 键（上移）
        m_camera.MoveUp(0.1f);
    }
    if (GetAsyncKeyState('E') & 0x8000) {  // E 键（下移）
        m_camera.MoveUp(-0.1f);
    }

    // 获取鼠标移动，控制相机旋转
    POINT cursorPos;
    GetCursorPos(&cursorPos);  // 获取鼠标相对屏幕的位置

    // 计算鼠标的偏移量
    static POINT lastCursorPos = cursorPos;  // 记录上一帧的鼠标位置
    int deltaX = cursorPos.x - lastCursorPos.x;
    int deltaY = cursorPos.y - lastCursorPos.y;

    // 设置一个灵敏度因子来调节鼠标移动的幅度
    float sensitivity = 0.1f;

    // 调用相机的旋转函数
    m_camera.Rotate(deltaX * sensitivity, -deltaY * sensitivity);  // 鼠标水平移动影响yaw，垂直移动影响pitch

    // 更新上一帧的鼠标位置
    lastCursorPos = cursorPos;
}