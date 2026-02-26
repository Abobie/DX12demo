#include "Renderer.h"
#include <utility> // std::swap
#include <stdexcept> // error handling
#include <d3dcompiler.h>
#include "d3dx12.h"
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

Renderer::Renderer(HWND hwnd, UINT w, UINT h)
    : width(w), height(h)
{
    InitD3D(hwnd);

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastTime);
}

Renderer::~Renderer()
{
    // Make sure GPU is done with all work
    const UINT64 fenceToWaitFor = nextFenceValue++;
    commandQueue->Signal(fence.Get(), fenceToWaitFor);

    if (fence->GetCompletedValue() < fenceToWaitFor)
    {
        fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    CloseHandle(fenceEvent);
}

void LoadTextureFromFile(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    const wchar_t* filename,
    ID3D12Resource** texture,
    ID3D12Resource** uploadHeap)
{
    // Initialize WIC
    ComPtr<IWICImagingFactory> factory;

    CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );

    // Load image
    ComPtr<IWICBitmapDecoder> decoder;
    factory->CreateDecoderFromFilename(
        filename,
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, &frame);

    // Convert to RGBA
    ComPtr<IWICFormatConverter> converter;
    factory->CreateFormatConverter(&converter);

    converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom
    );

    UINT width, height;
    converter->GetSize(&width, &height);

    std::vector<BYTE> imageData(width * height * 4);

    converter->CopyPixels(
        nullptr,
        width * 4,
        (UINT)imageData.size(),
        imageData.data()
    );

    // Create GPU texture
    auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        width,
        height);

	auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(texture)
    );

    // Upload heap
    UINT64 uploadSize;
    device->GetCopyableFootprints(
        &texDesc,
        0, 1, 0,
        nullptr,
        nullptr,
        nullptr,
        &uploadSize
    );

	auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC uploadDesc =
        CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadHeap)
    );

    // Copy data
    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData = imageData.data();
    sub.RowPitch = width * 4;
    sub.SlicePitch = sub.RowPitch * height;

    UpdateSubresources(
        cmd,
        *texture,
        *uploadHeap,
        0, 0, 1,
        &sub
    );

    // Transition
    auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(
            *texture,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );

    cmd->ResourceBarrier(1, &barrier);
}

// AABB collision resolution between player and object on a specific axis (0 = X, 1 = Y, 2 = Z)
void Renderer::ResolvePlayerCollision(XMVECTOR& position, XMVECTOR& velocity, GameObject& obj, UINT axis)
{
    XMFLOAT3 p;
    XMFLOAT3 v;
    XMStoreFloat3(&p, position);
    XMStoreFloat3(&v, velocity);

    float playerHalfX = player.width * 0.5f;
    float playerHalfY = player.height * 0.5f;
    float playerHalfZ = player.depth * 0.5f;

    float objHalfX = obj.scale.x * 0.5f;
    float objHalfY = obj.scale.y * 0.5f;
    float objHalfZ = obj.scale.z * 0.5f;

    float dx = obj.position.x - p.x;
    float dy = obj.position.y - p.y;
    float dz = obj.position.z - p.z;

    float overlapX = (playerHalfX + objHalfX) - fabsf(dx);
    float overlapY = (playerHalfY + objHalfY) - fabsf(dy);
    float overlapZ = (playerHalfZ + objHalfZ) - fabsf(dz);

	// If there's an overlap on all axes, resolve it along the specified axis.
    if (overlapX > 0 && overlapY > 0 && overlapZ > 0)
    {
        if (axis == 0) // X
        {
            float dir = (dx > 0) ? 1.0f : -1.0f;
            p.x -= dir * overlapX;
            v.x = 0.0f;
        }
        else if (axis == 1) // Y
        {
            float dir = (dy > 0) ? 1.0f : -1.0f;
            p.y -= dir * overlapY;
            v.y = 0.0f;

            // If colliding from above -> grounded
            if (dir < 0)
                player.grounded = true;
        }
        else if (axis == 2) // Z
        {
            float dir = (dz > 0) ? 1.0f : -1.0f;
            p.z -= dir * overlapZ;
            v.z = 0.0f;
        }
    }
	position = XMLoadFloat3(&p);
	velocity = XMLoadFloat3(&v);
}

void Renderer::InitD3D(HWND hwnd)
{
    // DX12 initialization. First, create DXGI Factory
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create DXGI Factory");
    }

    // Create device
    hr = D3D12CreateDevice(
        nullptr,                    // default adapter (first hardware GPU)
        D3D_FEATURE_LEVEL_11_0,     // minimum feature level
        IID_PPV_ARGS(&device)
    );
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create D3D12 device");
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create command queue");
    }

    // Set up and create the swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(),   // GPU queue that presents
        hwnd,                 // window handle
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create swap chain");
    }

    swapChain1.As(&swapChain);

    // Viewport stuff
    viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    scissorRect = { 0, 0, (LONG)width, (LONG)height };

    // Prevent full screen
    dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    // Create RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create RTV heap");
    }

    // Query the type of each descriptor
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    );

    // Create DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    device->CreateDescriptorHeap(
        &dsvHeapDesc,
        IID_PPV_ARGS(&dsvHeap)
    );

    dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV
    );

    // Create SRV heap (for textures)
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    device->CreateDescriptorHeap(
        &srvDesc,
        IID_PPV_ARGS(&srvHeap)
    );

	// Create depth buffer
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES depthHeapProps = {};
    depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    device->CreateCommittedResource(
        &depthHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClear,
        IID_PPV_ARGS(&depthBuffer)
    );

	// Create depth stencil view (DSV)
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    device->CreateDepthStencilView(
        depthBuffer.Get(),
        &dsvDesc,
        dsvHeap->GetCPUDescriptorHandleForHeapStart()
    );

    // Load shaders
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;

    D3DReadFileToBlob(L"VertexShader.cso", &vsBlob);
    D3DReadFileToBlob(L"PixelShader.cso", &psBlob);

    if (!vsBlob || !psBlob)
        throw std::runtime_error("Failed to load shaders");

    // Descriptor range for SRV (t0)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0; // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Root parameters
    D3D12_ROOT_PARAMETER params[2] = {};

    // b0 constant buffer
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // t0 texture
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Add sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0; // s0
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRS, error;
    D3D12SerializeRootSignature(
        &rsDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRS,
        &error
    );

    if (error)
    {
        OutputDebugStringA((char*)error->GetBufferPointer());
    }

    device->CreateRootSignature(
        0,
        serializedRS->GetBufferPointer(),
        serializedRS->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature)
    );

	// Define input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        {
            "POSITION", 0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
            "NORMAL", 0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        },
        {
			"TEXCOORD", 0,
			DXGI_FORMAT_R32G32_FLOAT,
			0, 24,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0
        }
    };

	// Create pipeline state object (PSO)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    auto rast = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rast.FrontCounterClockwise = TRUE;
    psoDesc.RasterizerState = rast;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));


    // Get the back buffers and create RTVs
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FrameCount; ++i)
    {
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        if (FAILED(hr))
        {
            throw std::runtime_error("Failed to get swap chain buffer");
        }

        device->CreateRenderTargetView(
            renderTargets[i].Get(),
            nullptr,
            rtvHandle
        );

        rtvHandle.ptr += rtvDescriptorSize;

		// Render target states
		renderTargetStates[i] = D3D12_RESOURCE_STATE_PRESENT;

        // Command allocator
        device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&commandAllocators[i])
        );
    }

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    for (UINT i = 0; i < FrameCount; ++i)
        fenceValues[i] = 0;

    Vertex cubeVertices[] =
    {
        // +Z (front)
        {{-0.5f, -0.5f,  0.5f}, {0,0,1}, {0,1}},
        {{-0.5f,  0.5f,  0.5f}, {0,0,1}, {0,0}},
        {{ 0.5f,  0.5f,  0.5f}, {0,0,1}, {1,0}},
        {{ 0.5f, -0.5f,  0.5f}, {0,0,1}, {1,1}},

        // -Z (back)
        {{ 0.5f, -0.5f, -0.5f}, {0,0,-1}, {0,1}},
        {{ 0.5f,  0.5f, -0.5f}, {0,0,-1}, {0,0}},
        {{-0.5f,  0.5f, -0.5f}, {0,0,-1}, {1,0}},
        {{-0.5f, -0.5f, -0.5f}, {0,0,-1}, {1,1}},

        // +X (right)
        {{ 0.5f, -0.5f,  0.5f}, {1,0,0}, {0,1}},
        {{ 0.5f,  0.5f,  0.5f}, {1,0,0}, {0,0}},
        {{ 0.5f,  0.5f, -0.5f}, {1,0,0}, {1,0}},
        {{ 0.5f, -0.5f, -0.5f}, {1,0,0}, {1,1}},

        // -X (left)
        {{-0.5f, -0.5f, -0.5f}, {-1,0,0}, {0,1}},
        {{-0.5f,  0.5f, -0.5f}, {-1,0,0}, {0,0}},
        {{-0.5f,  0.5f,  0.5f}, {-1,0,0}, {1,0}},
        {{-0.5f, -0.5f,  0.5f}, {-1,0,0}, {1,1}},

        // +Y (top)
        {{-0.5f,  0.5f,  0.5f}, {0,1,0}, {0,1}},
        {{-0.5f,  0.5f, -0.5f}, {0,1,0}, {0,0}},
        {{ 0.5f,  0.5f, -0.5f}, {0,1,0}, {1,0}},
        {{ 0.5f,  0.5f,  0.5f}, {0,1,0}, {1,1}},

        // -Y (bottom)
        {{-0.5f, -0.5f, -0.5f}, {0,-1,0}, {0,1}},
        {{-0.5f, -0.5f,  0.5f}, {0,-1,0}, {0,0}},
        {{ 0.5f, -0.5f,  0.5f}, {0,-1,0}, {1,0}},
        {{ 0.5f, -0.5f, -0.5f}, {0,-1,0}, {1,1}},
    };

    const UINT vertexBufferSize = sizeof(cubeVertices);

    // Create upload heap
    auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = vertexBufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );

    // Copy data to the upload heap
    void* mappedData;
    vertexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, cubeVertices, vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = vertexBufferSize;
    vertexBufferView.StrideInBytes = sizeof(Vertex);

    // Cube indices
    uint16_t cubeIndices[] =
    {
         0,  1,  2,  0,  2,  3,   // front
         4,  5,  6,  4,  6,  7,   // back
         8,  9, 10,  8, 10, 11,   // right
        12, 13, 14, 12, 14, 15,   // left
        16, 17, 18, 16, 18, 19,   // top
        20, 21, 22, 20, 22, 23    // bottom
    };

    const UINT indexBufferSize = sizeof(cubeIndices);

    // Create index buffer (upload heap)
    D3D12_HEAP_PROPERTIES indexHeapProps = {};
    indexHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC indexResDesc = {};
    indexResDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    indexResDesc.Width = indexBufferSize;
    indexResDesc.Height = 1;
    indexResDesc.DepthOrArraySize = 1;
    indexResDesc.MipLevels = 1;
    indexResDesc.SampleDesc.Count = 1;
    indexResDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(
        &indexHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &indexResDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)
    );

    // Copy index data
    void* indexData;
    indexBuffer->Map(0, nullptr, &indexData);
    memcpy(indexData, cubeIndices, indexBufferSize);
    indexBuffer->Unmap(0, nullptr);

    // Initialize index buffer view
    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = indexBufferSize;
    indexBufferView.Format = DXGI_FORMAT_R16_UINT;

    // Create constant buffer (256-byte aligned)
    objectCount = 128;
    cbStride = (sizeof(MVPConstants) + 255) & ~255;
    const UINT cbSize = cbStride * objectCount;

    D3D12_HEAP_PROPERTIES cbHeapProps = {};
    cbHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = cbSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(
        &cbHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constantBuffer)
    );

    // Map once, keep mapped
    constantBuffer->Map(0, nullptr, (void**)&cbData);

	// Create command list
    device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&commandList)
    );

    commandList->Close();

    commandList->Reset(commandAllocators[0].Get(), nullptr);

    // Load texture
    LoadTextureFromFile(
        device.Get(),
        commandList.Get(),
        L"lava.jpg",
        texture.GetAddressOf(),
        textureUpload.GetAddressOf()
    );

    commandList->Close();

    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // Wait for upload
    const UINT64 fenceVal = nextFenceValue++;
    commandQueue->Signal(fence.Get(), fenceVal);

    if (fence->GetCompletedValue() < fenceVal)
    {
        fence->SetEventOnCompletion(fenceVal, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }

	// Create shader resource view (SRV) for the texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(
        texture.Get(),
        &srv,
        srvHeap->GetCPUDescriptorHandleForHeapStart()
    );

    // Build test world
    // Floor
    sceneObjects.push_back({
        { 0.0f, -1.0f, 0.0f },  // position
        { 0.0f, 0.0f, 0.0f },   // rotation
        { 20.0f, 1.0f, 20.0f }  // scale
        });

    // Ceiling
    sceneObjects.push_back({
        { 0.0f, 7.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 20.0f, 1.0f, 20.0f }
        });

    // Back wall
    sceneObjects.push_back({
        { 0.0f, 3.5f, 10.0f },
        { 0.0f, 0.0f, 0.0f },
        { 20.0f, 8.0f, 1.0f }
        });

    // Front wall
    sceneObjects.push_back({
        { 0.0f, 3.5f, -10.0f },
        { 0.0f, 0.0f, 0.0f },
        { 20.0f, 8.0f, 1.0f }
        });

    // Left wall
    sceneObjects.push_back({
        { -10.0f, 3.5f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 8.0f, 20.0f }
        });

    // Right wall
    sceneObjects.push_back({
        { 10.0f, 3.5f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 8.0f, 20.0f }
        });

    // Random cubes
    sceneObjects.push_back({ { 3,0.5f,3 },{0,0,0},{1,2,1} });
    sceneObjects.push_back({ { -4,1.0f,2 },{0,0,0},{1,3,1} });
    sceneObjects.push_back({ { 0,0,-4 },{0,0,0},{2,1,2} });
    sceneObjects[sceneObjects.size() - 1].moveSpeed = -0.5f;
}

void Renderer::Render()
{
    // Time
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);

    float deltaTime =
        float(currentTime.QuadPart - lastTime.QuadPart) /
        float(frequency.QuadPart);

    lastTime = currentTime;

	// Swap chain frame index
    UINT frameIndex = swapChain->GetCurrentBackBufferIndex();

	// Fence wait
    if (fence->GetCompletedValue() < fenceValues[frameIndex])
    {
        fence->SetEventOnCompletion(fenceValues[frameIndex], fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
	}

    commandAllocators[frameIndex]->Reset();
    commandList->Reset(commandAllocators[frameIndex].Get(), pipelineState.Get());

    // Mouse input for rotating camera
    POINT mousePos;
    GetCursorPos(&mousePos);
    ScreenToClient(GetActiveWindow(), &mousePos);

    static POINT lastMousePos = mousePos;

    float deltaX = float(mousePos.x - lastMousePos.x);
    float deltaY = float(mousePos.y - lastMousePos.y);

    lastMousePos = mousePos;

    camera.yaw += deltaX * camera.mouseSensitivity;
    camera.pitch -= deltaY * camera.mouseSensitivity;

    const float limit = XM_PIDIV2 - 0.01f;

    if (camera.pitch > limit) camera.pitch = limit;
    if (camera.pitch < -limit) camera.pitch = -limit;

    // Recenter cursor in window
    RECT rect;
    GetClientRect(GetActiveWindow(), &rect);

    POINT center;
    center.x = rect.right / 2;
    center.y = rect.bottom / 2;

    ClientToScreen(GetActiveWindow(), &center);
    SetCursorPos(center.x, center.y);

    lastMousePos = { rect.right / 2, rect.bottom / 2 };

    XMVECTOR forward =
        XMVectorSet(
            cosf(camera.pitch) * sinf(camera.yaw),
            sinf(camera.pitch),
            cosf(camera.pitch) * cosf(camera.yaw),
            0.0f
        );

    XMVECTOR right =
        XMVector3Normalize(
            XMVector3Cross(
                XMVectorSet(0, 1, 0, 0),
                forward
            )
        );

    XMVECTOR up = XMVector3Cross(forward, right);

    // Player movement
    XMVECTOR velocity = XMLoadFloat3(&player.velocity);
    XMVECTOR position = XMLoadFloat3(&player.position);

    XMVECTOR forwardXZ = XMVectorSet(
        XMVectorGetX(forward),
        0.0f,
        XMVectorGetZ(forward),
        0.0f
    );

    forwardXZ = XMVector3Normalize(forwardXZ);

    XMVECTOR rightXZ = XMVectorSet(
        XMVectorGetX(right),
        0.0f,
        XMVectorGetZ(right),
        0.0f
    );

    rightXZ = XMVector3Normalize(rightXZ);

    XMVECTOR acceleration = XMVectorZero();

    // WASD movement only when grounded
    if (player.grounded)
    {
        if (GetAsyncKeyState('W') & 0x8000)
            acceleration += forwardXZ;

        if (GetAsyncKeyState('S') & 0x8000)
            acceleration -= forwardXZ;

        if (GetAsyncKeyState('D') & 0x8000)
            acceleration += rightXZ;

        if (GetAsyncKeyState('A') & 0x8000)
            acceleration -= rightXZ;
    }

    if (XMVector3LengthSq(acceleration).m128_f32[0] > 0.0f)
    {
        acceleration = XMVector3Normalize(acceleration);
        velocity += acceleration * player.moveAcceleration * deltaTime;
    }

    // Apply gravity
    velocity += XMVectorSet(0.0f, player.gravity * deltaTime, 0.0f, 0.0f);

    // Clamp horizontal velocity
    XMVECTOR horizontalVel = XMVectorSet(
        XMVectorGetX(velocity),
        0.0f,
        XMVectorGetZ(velocity),
        0.0f
    );

    float speed = XMVectorGetX(XMVector3Length(horizontalVel));

    if (speed > player.maxSpeed)
    {
        horizontalVel = XMVector3Normalize(horizontalVel) * player.maxSpeed;
        velocity = XMVectorSet(
            XMVectorGetX(horizontalVel),
            XMVectorGetY(velocity),
            XMVectorGetZ(horizontalVel),
            0.0f
        );
    }

	// Apply velocity to position

    // X axis
	position += XMVectorMultiply(velocity, XMVectorSet(deltaTime, 0.0f, 0.0f, 0.0f));
    for (auto& obj : sceneObjects)
    {
        ResolvePlayerCollision(position, velocity, obj, 0);
    }

    // Z axis
    position += XMVectorMultiply(velocity, XMVectorSet(0.0f, 0.0f, deltaTime, 0.0f));
    for (auto& obj : sceneObjects)
    {
        ResolvePlayerCollision(position, velocity, obj, 2);
    }

    // Y axis
    player.grounded = false; // reset before Y resolution
    position += XMVectorMultiply(velocity, XMVectorSet(0.0f, deltaTime, 0.0f, 0.0f));
    for (auto& obj : sceneObjects)
    {
        ResolvePlayerCollision(position, velocity, obj, 1);
    }

    // Jumping
    if (player.grounded && (GetAsyncKeyState(VK_SPACE) & 0x8000))
    {
        velocity += XMVectorSet(0.0f, player.jumpStrength, 0.0f, 0.0f);
        player.grounded = false;
    }

    // Ground friction
    if (player.grounded)
    {
        float friction = 8.0f;

        float frictionFactor = max(0.0f, 1.0f - friction * deltaTime);
        horizontalVel *= frictionFactor;

        velocity = XMVectorSet(
            XMVectorGetX(horizontalVel),
            XMVectorGetY(velocity),
            XMVectorGetZ(horizontalVel),
            0.0f
        );
    }

    XMStoreFloat3(&player.position, position);
    XMStoreFloat3(&player.velocity, velocity);

    // Camera to follow player
    XMVECTOR camPos = XMLoadFloat3(&player.position);
    camPos += XMVectorSet(0.0f, player.eyeOffset, 0.0f, 0.0f);

    // Camera
    XMVECTOR camTarget = camPos + forward;

    XMMATRIX view = XMMatrixLookAtLH(camPos, camTarget, up);

    // Compute cube movement
    for (UINT i = 0; i < sceneObjects.size(); ++i)
    {
        // Skip floor and walls
        // Assume first few objects are room pieces
        if (i < 6) continue;

        XMVECTOR objPos = XMLoadFloat3(&sceneObjects[i].position);

        // Direction from cube to camera
        XMVECTOR toCamera = camPos - objPos;

        // Remove Y component (stay on ground)
        toCamera = XMVectorSetY(toCamera, 0.0f);

        float length = XMVectorGetX(XMVector3Length(toCamera));

        if (length > 0.01f) // avoid jitter when very close
        {
            XMVECTOR direction = XMVector3Normalize(toCamera);

            objPos += direction * sceneObjects[i].moveSpeed * deltaTime;

            XMStoreFloat3(&sceneObjects[i].position, objPos);
        }
    }

    // Cube-cube collision resolution (simple AABB)
    for (int i = 6; i < sceneObjects.size(); ++i)
    {
        for (int j = i + 1; j < sceneObjects.size(); ++j)
        {
            XMFLOAT3& posA = sceneObjects[i].position;
            XMFLOAT3& posB = sceneObjects[j].position;

            float halfA = sceneObjects[i].scale.x * 0.5f;
            float halfB = sceneObjects[j].scale.x * 0.5f;

            float dx = posB.x - posA.x;
            float dz = posB.z - posA.z;

            float overlapX = (halfA + halfB) - fabsf(dx);
            float overlapZ = (halfA + halfB) - fabsf(dz);

            if (overlapX > 0 && overlapZ > 0)
            {
                // Resolve along smallest axis
                if (overlapX < overlapZ)
                {
                    float push = overlapX * 0.5f;
                    float dir = (dx > 0) ? 1.0f : -1.0f;

                    posA.x -= dir * push;
                    posB.x += dir * push;
                }
                else
                {
                    float push = overlapZ * 0.5f;
                    float dir = (dz > 0) ? 1.0f : -1.0f;

                    posA.z -= dir * push;
                    posB.z += dir * push;
                }
            }
        }
    }

    // Cube-wall collision resolution (AABB)
    for (int i = 6; i < sceneObjects.size(); ++i)
    {
        for (int j = 2; j < 6; ++j)
        {
            XMFLOAT3& posCube = sceneObjects[i].position;
            XMFLOAT3& posWall = sceneObjects[j].position;

            float halfCubeX = sceneObjects[i].scale.x * 0.5f;
            float halfWallX = sceneObjects[j].scale.x * 0.5f;
            float halfCubeZ = sceneObjects[i].scale.z * 0.5f;
            float halfWallZ = sceneObjects[j].scale.z * 0.5f;

            float dx = posWall.x - posCube.x;
            float dz = posWall.z - posCube.z;

            float overlapX = (halfCubeX + halfWallX) - fabsf(dx);
            float overlapZ = (halfCubeZ + halfWallZ) - fabsf(dz);

            if (overlapX > 0 && overlapZ > 0)
            {
                // Resolve along smallest axis
                if (overlapX < overlapZ)
                {
                    float dir = (dx > 0) ? 1.0f : -1.0f;
                    posCube.x -= dir * overlapX;
                }
                else
                {
                    float dir = (dz > 0) ? 1.0f : -1.0f;
                    posCube.z -= dir * overlapZ;
                }
            }
        }
    }

    // Projection
    float aspect = (float)width / height;

    XMMATRIX proj =
        XMMatrixPerspectiveFovLH(
            XM_PIDIV4,
            aspect,
            0.1f,
            100.0f);

	// Set light color and direction
    cbData->ambientColor = { 0.15f, 0.15f, 0.18f };

    cbData->directionalLightDir = { -0.5f, -1.0f, -0.3f };
    cbData->directionalLightColor = { 0.0f, 0.0f, 0.0f };

    cbData->pointLightPosition = player.position;
    cbData->pointLightRange = 50.0f;
    cbData->pointLightColor = { 0.4f, 0.4f, 1.0f };

    // Transition: PRESENT -> RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTargets[frameIndex].Get();
    barrier.Transition.StateBefore = renderTargetStates[frameIndex];
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);

    renderTargetStates[frameIndex] = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// Clear render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frameIndex * rtvDescriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        dsvHeap->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    float clearColor[] = { 0.9f, 0.4f, 0.6f, 1.0f };
    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->SetPipelineState(pipelineState.Get());
    commandList->SetGraphicsRootSignature(rootSignature.Get());
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView);
    ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());

	// Draw all objects in the scene
    for (size_t i = 0; i < sceneObjects.size(); i++)
    {
        const auto& obj = sceneObjects[i];

        XMMATRIX scale =
            XMMatrixScaling(obj.scale.x, obj.scale.y, obj.scale.z);

        XMMATRIX rot =
            XMMatrixRotationRollPitchYaw(
                obj.rotation.x,
                obj.rotation.y,
                obj.rotation.z);

        XMMATRIX trans =
            XMMatrixTranslation(
                obj.position.x,
                obj.position.y,
                obj.position.z);

        XMMATRIX world = scale * rot * trans;

        MVPConstants* objCB =
            (MVPConstants*)(
                (uint8_t*)cbData + i * cbStride
                );

        XMStoreFloat4x4(
            &objCB->world,
            XMMatrixTranspose(world)
        );

        XMStoreFloat4x4(
            &objCB->view,
            XMMatrixTranspose(view)
        );

        XMStoreFloat4x4(
            &objCB->projection,
            XMMatrixTranspose(proj)
        );

        objCB->ambientColor = cbData->ambientColor;
        objCB->directionalLightDir = cbData->directionalLightDir;
        objCB->directionalLightColor = cbData->directionalLightColor;
		objCB->pointLightPosition = cbData->pointLightPosition;
		objCB->pointLightRange = cbData->pointLightRange;
		objCB->pointLightColor = cbData->pointLightColor;

        D3D12_GPU_VIRTUAL_ADDRESS cbAddress =
            constantBuffer->GetGPUVirtualAddress()
            + i * cbStride;

        commandList->SetGraphicsRootConstantBufferView(
            0,
            cbAddress
        );

        commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }

    // Transition: RENDER_TARGET -> PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &barrier);

    // Update tracked state
    renderTargetStates[frameIndex] = D3D12_RESOURCE_STATE_PRESENT;

    commandList->Close();

    // Execute
    ID3D12CommandList* lists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, lists);

    // Present
    swapChain->Present(1, 0);

	// Signal and increment the fence value
    const UINT64 signalValue = nextFenceValue++;
    commandQueue->Signal(fence.Get(), signalValue);
    fenceValues[frameIndex] = signalValue;
}
