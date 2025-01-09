#include "Renderer.h"
#include "d3dx12.h"
#include "d3dUtil.h"
#include <initguid.h>
#include "Camera.h"
#include "GeometryGenerator.h"

using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;
using namespace DirectX;

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

    //画正方体
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
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

void Renderer::BuildDescriptorHeaps(){
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 2;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;

    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc,IID_PPV_ARGS(&mCbvHeap)));
}

void Renderer::BuildConstantBuffers(){
    //创建第一个cbv
    objCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress0 = objCB->Resource()->GetGPUVirtualAddress();

    int boxCBufIndex = 0;
	cbAddress0 += boxCBufIndex*objCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc0;
	cbvDesc0.BufferLocation = cbAddress0;
	cbvDesc0.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    int heapIndex0 = 0;
    auto handle0 = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    handle0.Offset(heapIndex0, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	m_device->CreateConstantBufferView(&cbvDesc0, handle0);

    //创建第二个cbv
    passCB  = std::make_unique<UploadBuffer<PassConstants>>(m_device.Get(), 1, true);
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress1 = passCB->Resource()->GetGPUVirtualAddress();

    int passCBufIndex = 0;
    cbAddress1 += passCBufIndex*passCBByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc1;
	cbvDesc1.BufferLocation = cbAddress1;
	cbvDesc1.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    int heapIndex1 = 1;
    auto handle1 = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
    handle1.Offset(heapIndex1, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	m_device->CreateConstantBufferView(&cbvDesc1, handle1);

}

void Renderer::BuildRootSignature(){

    //定义根参数
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

    CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);

    CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);
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

    mvsByteCode = d3dUtil::CompileShader(L"D:\\Personal Project\\D3D12book_code\\Chapter7 Drawing in Direct3D Part2\\Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = d3dUtil::CompileShader(L"D:\\Personal Project\\D3D12book_code\\Chapter7 Drawing in Direct3D Part2\\Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

    m_InputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void Renderer::BuildBoxGeometry(){

    GeometryGenerator proceGeo;
    GeometryGenerator::MeshData box = proceGeo.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = proceGeo.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = proceGeo.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = proceGeo.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //计算单个几何体顶点在总顶点数组中的偏移量,顺序为：box、grid、sphere、cylinder
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = (UINT)grid.Vertices.size() + gridVertexOffset;
    UINT cylinderVertexOffset = (UINT)sphere.Vertices.size() + sphereVertexOffset;

    //计算单个几何体索引在总索引数组中的偏移量,顺序为：box、grid、sphere、cylinder
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = (UINT)grid.Indices32.size() + gridIndexOffset;
    UINT cylinderIndexOffset = (UINT)sphere.Indices32.size() + sphereIndexOffset;

    //创建子物体
    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.BaseVertexLocation = boxVertexOffset;
    boxSubmesh.StartIndexLocation = boxIndexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.BaseVertexLocation = gridVertexOffset;
    gridSubmesh.StartIndexLocation = gridIndexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;

    //创建一个总的vertices
    size_t totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size() + cylinder.Vertices.size();
    std::vector<Vertex> vertices(totalVertexCount);	//给定顶点数组大小
    //传入4个子物体的顶点数据
    int k = 0;
    for (int i = 0; i < box.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
    }
    for (int i = 0; i < grid.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Brown);
    }
    for (int i = 0; i < sphere.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Green);
    }
    for (int i = 0; i < cylinder.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Blue);
    }

    //创建一个总的index索引
    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
    indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
    indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
    indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

    //然后我们计算出vertices和indices这两个缓存的各自大小,并传给全局变量。
    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));

    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize); 
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_device.Get(),
		m_commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader); //创建 GPU 顶点缓冲区 将顶点数据从 CPU 上传到 GPU 的默认缓冲区中
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_device.Get(),
		m_commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader); //创建 GPU 顶点缓冲区 将索引数据从 CPU 上传到 GPU 的默认缓冲区中

    // 设置缓冲区属性
	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    //mGeometries[geo->Name] = std::move(geo);

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
    ProcessInput(); 
/*
    // 获取当前帧资源
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();


    // 同步 GPU 操作（等到当前帧资源的命令完成）
    if (mCurrFrameResource->Fence != 0 && m_fence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(m_fence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
*/

    XMMATRIX world = XMLoadFloat4x4(&mWorld);
    XMMATRIX view = m_camera.GetViewMatrix();  // 获取视图矩阵
    float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);
    float fov = XMConvertToRadians(60.0f);
    float nearZ = 1.0f;
    float farZ = 1000.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspectRatio, nearZ, farZ);
    XMStoreFloat4x4(&mProj, proj);

    // 计算世界、视图和投影矩阵的组合
    XMMATRIX ViewProj =  view * proj;

    // 传递常量到着色器
    ObjectConstants objConstants;
    PassConstants passConstants;
    
    XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world)); 
    XMStoreFloat4x4(&passConstants.viewProj, XMMatrixTranspose(ViewProj));

    objCB->CopyData(0, objConstants);  // 更新常量缓冲区
    passCB->CopyData(0, passConstants);
}

void Renderer::Render()
{
    // 重置命令分配器和命令列表
    std::cout << "Begin Render Function..." << std::endl;
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

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
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps); //CBV
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get()); //RootSignature

    //渲染几何体
    m_commandList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
    m_commandList->IASetIndexBuffer(&geo->IndexBufferView());
    m_commandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);

    //这里需要绑定两次
    int objCbvIndex = 0;
    auto handle0 = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    handle0.Offset(objCbvIndex, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    m_commandList->SetGraphicsRootDescriptorTable(0, handle0);
    int passCbvIndex = 1;
    auto handle1 = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    handle1.Offset(passCbvIndex, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    m_commandList->SetGraphicsRootDescriptorTable(1, handle1);

    //绘制四个物体  
    m_commandList->DrawIndexedInstanced(geo->DrawArgs["box"].IndexCount, 1, geo->DrawArgs["box"].StartIndexLocation, geo->DrawArgs["box"].BaseVertexLocation, 0);
    m_commandList->DrawIndexedInstanced(geo->DrawArgs["grid"].IndexCount, 1, geo->DrawArgs["grid"].StartIndexLocation, geo->DrawArgs["grid"].BaseVertexLocation, 0);
    m_commandList->DrawIndexedInstanced(geo->DrawArgs["sphere"].IndexCount, 1, geo->DrawArgs["sphere"].StartIndexLocation, geo->DrawArgs["sphere"].BaseVertexLocation, 0);
    m_commandList->DrawIndexedInstanced(geo->DrawArgs["cylinder"].IndexCount, 1, geo->DrawArgs["cylinder"].StartIndexLocation, geo->DrawArgs["cylinder"].BaseVertexLocation, 0);

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
    FlushCommandQueue();
}

/*
void Renderer::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(m_device.Get(),
            1, (UINT)mAllRitems.size()));
    }
}
*/

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