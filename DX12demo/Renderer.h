#pragma once

#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>    // For ComPtr
#include <DirectXMath.h>
#include <vector>

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

    XMFLOAT3 ambientColor;
    float pad1;

    XMFLOAT3 directionalLightDir;
    float pad2;

    XMFLOAT3 directionalLightColor;
    float pad3;

    XMFLOAT3 pointLightPosition;
    float pointLightRange;

    XMFLOAT3 pointLightColor;
    float pad4;
};

struct GameObject
{
    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 scale;
    float moveSpeed = 0.5f;
};

struct Camera
{
    float yaw = 0.0f;   // rotation around Y
    float pitch = 0.0f; // rotation around X
    float mouseSensitivity = 0.002f;
};

struct Player
{
    XMFLOAT3 position = { 0.0f, 5.0f, 0.0f };
    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };

    float moveAcceleration = 50.0f;
    float maxSpeed = 8.0f;
    float jumpStrength = 8.0f;
    float gravity = -20.0f;

    float radius = 0.5f;
    float halfHeight = 0.25f;
    float eyeOffset = 0.0f;

    bool grounded = false;

    // Hookshot mechanic
    bool hookActive;
    XMFLOAT3 hookPoint;
    float ropeLength;
    float hookshotForce = 30.0f;
};

class Renderer
{
public:

    Renderer(HWND hwnd, UINT width, UINT height);
    ~Renderer();

    void Render();

private:
    void DrawCrosshair(XMMATRIX& view, XMMATRIX& proj, XMVECTOR camPos, XMVECTOR forward);
    void DrawHookMarker(XMMATRIX& view, XMMATRIX& proj);
    void ResolvePlayerCollision(XMVECTOR& position, XMVECTOR& velocity, const GameObject& box);
    bool RayAABB(
        const XMVECTOR& origin,
        const XMVECTOR& dir,
        const XMVECTOR& boxMin,
        const XMVECTOR& boxMax,
        float& t);
    void fireHookshot();
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
	UINT objectCount;
    UINT cbStride = 0;

    // Texture
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> textureUpload;

	// Game object
    std::vector<GameObject> sceneObjects;

	// Camera
	Camera camera;

    // Time
    LARGE_INTEGER frequency;
    LARGE_INTEGER lastTime;

    // Player
    Player player;
};
