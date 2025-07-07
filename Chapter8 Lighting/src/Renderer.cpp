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

    //画正方体
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));
    
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildMaterials();
    BuildRenderItem();
    BuildFrameResources();
    //BuildDescriptorHeaps();
    //BuildConstantBuffers();
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
    UINT objectCount = (UINT)mAllRitems.size();

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = (objectCount + 1)*gNumFrameResources; ////此处一个堆中包含(几何体个数（包含实例）+1)个CBV
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;

    ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvHeapDesc,IID_PPV_ARGS(&mCbvHeap)));
}

void Renderer::BuildConstantBuffers(){
    //创建objCB
    UINT objectCount = (UINT)mAllRitems.size();
    UINT objConstSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    //循环遍历
    for(int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex){
        std::cout << objectCount << std::endl;
        //auto objectCB = FrameResourcesArray[frameIndex]->ObjectCB->Resource();

        for(UINT i = 0; i < objectCount; ++i){
            D3D12_GPU_VIRTUAL_ADDRESS objCB_Address = mFrameResources[frameIndex]->ObjectCB->Resource()->GetGPUVirtualAddress();
            int objCbElementIndex = i;
            objCB_Address += objCbElementIndex * objConstSize;//子物体在常量缓冲区中的地址
            int heapIndex = frameIndex*objectCount + i;	//CBV堆中的CBV元素索引
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());//获得CBV堆首地址
            handle.Offset(heapIndex, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));	//CBV句柄（CBV堆中的CBV元素地址）

            //创建CBV描述符
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = objCB_Address;
            cbvDesc.SizeInBytes = objConstSize;
            m_device->CreateConstantBufferView(&cbvDesc, handle);
        }
    }

    //创建passCB
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    for(int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex){
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress1;
        cbAddress1 = mFrameResources[frameIndex]->PassCB->Resource()->GetGPUVirtualAddress();

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc1;
        cbvDesc1.BufferLocation = cbAddress1;
        cbvDesc1.SizeInBytes = passCBByteSize;

        int heapIndex1 = objectCount * gNumFrameResources + frameIndex;
        auto handle1 = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
        handle1.Offset(heapIndex1, m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
        m_device->CreateConstantBufferView(&cbvDesc1, handle1);
    }
}

void Renderer::BuildRootSignature(){

    //定义根参数
    CD3DX12_ROOT_PARAMETER slotRootParameter[3];

    //0号槽：cbuffer cbPerObject : register(b0)
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsConstantBufferView(2); 

    //定义根签名描述符
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    
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
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void Renderer::BuildRenderItem(){
    
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    boxRitem->ObjCBIndex = 0;//BOX常量数据（world矩阵）在objConstantBuffer索引0上
    boxRitem->Geo = mGeometries["shapeGeo"].get();
    boxRitem->Mat = mMaterials["stone0"].get();
    boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    mAllRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;//BOX常量数据（world矩阵）在objConstantBuffer索引1上
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->Mat = mMaterials["tile0"].get();
    gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	mAllRitems.push_back(std::move(gridRitem));

    UINT objCBIndex = 2;//接下去的几何体常量数据在CB中的索引从2开始
    XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);

    for(int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
        XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
        leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
        XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
        rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
        leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
        rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

    for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());

}

void Renderer::BuildShapeGeometry(){

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
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
    }
    for (int i = 0; i < grid.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Brown);
    }
    for (int i = 0; i < sphere.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].Color = XMFLOAT4(DirectX::Colors::Green);
    }
    for (int i = 0; i < cylinder.Vertices.size(); i++, k++)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
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

    mGeometries[geo->Name] = std::move(geo);

}

void Renderer::BuildMaterials()
{
    auto boxMat = std::make_unique<Material>();
    boxMat->Name = "bricks0";
    boxMat->MatCBIndex = 0;
    boxMat->DiffuseSrvHeapIndex = 0;
    boxMat->DiffuseAlbedo = XMFLOAT4(Colors::Yellow);
    boxMat->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    boxMat->Roughness = 0.25f;

    auto gridMat = std::make_unique<Material>();
    gridMat->Name = "stone0";
    gridMat->MatCBIndex = 1;
    gridMat->DiffuseSrvHeapIndex = 1;
    gridMat->DiffuseAlbedo = XMFLOAT4(Colors::Brown);
    gridMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    gridMat->Roughness = 0.3f;

    auto sphereMat = std::make_unique<Material>();
    sphereMat->Name = "tile0";
    sphereMat->MatCBIndex = 2;
    sphereMat->DiffuseSrvHeapIndex = 2;
    sphereMat->DiffuseAlbedo = XMFLOAT4(Colors::Green);
    sphereMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    sphereMat->Roughness = 0.2f;

    auto cylinderMat = std::make_unique<Material>();
    cylinderMat->Name = "skullMat";
    cylinderMat->MatCBIndex = 3;
    cylinderMat->DiffuseSrvHeapIndex = 3;
    cylinderMat->DiffuseAlbedo = XMFLOAT4(Colors::Blue);
    cylinderMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    cylinderMat->Roughness = 0.3f;

    mMaterials["bricks0"] = std::move(boxMat);
    mMaterials["stone0"] = std::move(gridMat);
    mMaterials["tile0"] = std::move(sphereMat);
    mMaterials["skullMat"] = std::move(cylinderMat);
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
    OnKeyboardInput();
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
    UpdateMaterialCB();
    UpdateMainPassCB();

}

void Renderer::UpdateMaterialCB(){
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for(auto& e : mMaterials){
        Material* mat = e.second.get();
        if(mat->NumFramesDirty > 0){
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
        }
    }
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
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.world, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
            e->NumFramesDirty--;
        }
    }
}

void Renderer::UpdateMainPassCB(){
    PassConstants passConstants;

	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&passConstants.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&passConstants.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&passConstants.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&passConstants.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&passConstants.InvViewProj, XMMatrixTranspose(invViewProj));

    passConstants.eyePosW = m_camera.GetPosition();

    //在这里设置光照
    passConstants.ambientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
    passConstants.Lights[0].Strength = { 1.0f,1.0f,0.9f };
    XMVECTOR sunDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);
    XMStoreFloat3(&passConstants.Lights[0].Direction, sunDir);

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, passConstants);
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* m_commandList,const std::vector<RenderItem*>& ritems){
    // 常量缓冲区字节对齐大小（例如 256 字节对齐）
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    // 获取当前帧的材质常量缓冲资源
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    //遍历渲染项数组
	for (size_t i = 0; i < ritems.size(); i++)
	{
		auto ritem = ritems[i];

        // 设置顶点/索引缓冲区和图元拓扑
		m_commandList->IASetVertexBuffers(0, 1, &ritem->Geo->VertexBufferView());
		m_commandList->IASetIndexBuffer(&ritem->Geo->IndexBufferView());
		m_commandList->IASetPrimitiveTopology(ritem->PrimitiveType);

		// 设置 ObjectCB 的根描述符表（槽位 0）
        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ritem->ObjCBIndex*objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ritem->Mat->MatCBIndex*matCBByteSize;
        m_commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);
        m_commandList->SetGraphicsRootConstantBufferView(1, matCBAddress);

		//绘制顶点（通过索引缓冲区绘制）
		m_commandList->DrawIndexedInstanced(ritem->IndexCount, //每个实例要绘制的索引数
			1,	//实例化个数
			ritem->StartIndexLocation,	//起始索引位置
			ritem->BaseVertexLocation,	//子物体起始索引在全局索引中的位置
			0);	//实例化的高级技术，暂时设置为0
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
    auto passCB = mCurrFrameResource->PassCB->Resource();
    m_commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //渲染几何体
    DrawRenderItems(m_commandList.Get(),mOpaqueRitems);
    std::cout << "finish" << std::endl;

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
            1, (UINT)mAllRitems.size(),(UINT)mMaterials.size()));
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

//用键盘控制光源的位置
void Renderer::OnKeyboardInput()
{
    const float dt = 0.016f;
	//左右键改变平行光的Theta角，上下键改变平行光的Phi角
	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
		sunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
		sunTheta += 1.0f * dt;
	if (GetAsyncKeyState(VK_UP) & 0x8000)
		sunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		sunPhi += 1.0f * dt;

	//将Phi约束在[0, PI/2]之间
	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);
}