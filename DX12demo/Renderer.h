#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>    // For ComPtr
#include <DirectXMath.h>

using namespace DirectX;

using Microsoft::WRL::ComPtr;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
	XMFLOAT2 uv;
};

struct MVPConstants
{
    XMFLOAT4X4 world;
    XMFLOAT4X4 view;
    XMFLOAT4X4 projection;

    XMFLOAT3 lightDir;
    float pad1;

    XMFLOAT3 lightColor;
    float pad2;
};

class Renderer
{
public:

    Renderer(HWND hwnd, UINT width, UINT height);
    ~Renderer();

    void Render();

private:
    void InitD3D(HWND hwnd);

private:
    static const UINT FrameCount = 2;

    UINT width;
    UINT height;

    // Core DX12 objects
    ComPtr<IDXGIFactory4> dxgiFactory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<IDXGISwapChain3> swapChain;

    // Rendering
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12Resource> renderTargets[FrameCount];
    D3D12_RESOURCE_STATES renderTargetStates[FrameCount];
    UINT rtvDescriptorSize = 0;

    // Commands
    ComPtr<ID3D12CommandAllocator> commandAllocators[FrameCount];
    ComPtr<ID3D12GraphicsCommandList> commandList;

    // Sync
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValues[FrameCount];
    HANDLE fenceEvent;
    UINT64 nextFenceValue = 1;

	// Viewport and Scissor
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissorRect;

    // Root signature
	ComPtr<ID3D12RootSignature> rootSignature;

	// Pipeline state
	ComPtr<ID3D12PipelineState> pipelineState;

    // Vertex buffer
    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    // Depth buffer
    ComPtr<ID3D12Resource> depthBuffer;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    UINT dsvDescriptorSize = 0;

    // SRV heap
    ComPtr<ID3D12DescriptorHeap> srvHeap;

	// Index buffer
	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;

	// MVP - Model-View-Projection constants buffer
    ComPtr<ID3D12Resource> constantBuffer;
    MVPConstants* cbData = nullptr;

    // Texture
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUpload;

    float rotX = 0.0f;
    float rotY = 0.0f;
};
