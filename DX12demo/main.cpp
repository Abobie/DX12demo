#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>   // For ComPtr
#include <utility> // For std::swap
#include "Renderer.h"

using Microsoft::WRL::ComPtr;

// WindowProc = something interacts with the window, like a click or cursor moves
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    LPSTR,
    int nCmdShow)
{
    // Some parameters
    const UINT FrameCount = 2;
    const UINT WindowWidth = 1280;
    const UINT WindowHeight = 720;
    UINT fenceValue = 0;
    HANDLE fenceEvent = nullptr;

    const wchar_t CLASS_NAME[] = L"DX12DemoWindowClass";

    // Set up a basic window
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"DX12 Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WindowWidth, WindowHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    ShowWindow(hwnd, nCmdShow);

	Renderer renderer(hwnd, WindowWidth, WindowHeight);

    // Infinite loop for getting input to the window
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            renderer.Render();
        }
    }

    return 0;
}
