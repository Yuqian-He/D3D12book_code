#include <windows.h>
#include <iostream>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include "Renderer.h"
#include <stdexcept>
#include <vector>

using namespace Microsoft::WRL;

// 窗口过程函数
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// 入口点函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    // 创建控制台窗口
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stdout);  // 重定向标准输出到控制台
        freopen("CONOUT$", "w", stderr);  // 重定向标准错误输出到控制台
        std::cout << "Console window is now open!" << std::endl;  // 输出测试信息
    }

    // 注册窗口类
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"D3D12Renderer";

    RegisterClass(&wc);

    // 创建窗口
    HWND hwnd = CreateWindowExW(  // 使用宽字符版本
        0, L"D3D12Renderer", L"Direct3D 12 Renderer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nShowCmd);

    // 创建渲染器实例并初始化
    Renderer renderer;
    try {
        std::cout << "Before initializing renderer" << std::endl;
        renderer.Initialize(hwnd);  // 初始化渲染器
        std::cout << "Renderer initialized successfully!" << std::endl;
    } catch (const std::runtime_error& e) {
        std::cerr << "Initialization Failed: " << e.what() << std::endl;
        MessageBoxA(hwnd, e.what(), "Initialization Failed", MB_OK | MB_ICONERROR);
        return -1;
    }

    // 主消息循环
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 每一帧渲染
        renderer.Render();
    }

    return 0;
}
