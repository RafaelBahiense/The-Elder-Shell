#include "ReloadableUI.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cwchar>

namespace {
    using Microsoft::WRL::ComPtr;
    preview::ReloadableUI ui;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> target;
    UINT resizeWidth = 0, resizeHeight = 0;

    bool CreateTarget() {
        ComPtr<ID3D11Texture2D> buffer;
        return SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
               SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target));
    }

    bool CreateDevice(HWND window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        constexpr std::array levels{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        for (const auto driver : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
            if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels.data(),
                                                        static_cast<UINT>(levels.size()), D3D11_SDK_VERSION, &desc,
                                                        &swapChain, &device, nullptr, &context)))
                return CreateTarget();
        }
        return false;
    }

    LRESULT WINAPI WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (ui.API() && ui.API()->message(window, message, wParam, lParam))
            return 1;
        switch (message) {
            case WM_SIZE:
                if (wParam != SIZE_MINIMIZED) {
                    resizeWidth = LOWORD(lParam);
                    resizeHeight = HIWORD(lParam);
                }
                return 0;
            case WM_SYSCOMMAND:
                if ((wParam & 0xFFF0) == SC_KEYMENU)
                    return 0;
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    if (std::wcsstr(arguments, L"--smoke-test")) {
        preview::ReloadableUI test;
        if (!test.Load(nullptr, nullptr, nullptr))
            return 1;
        if (const int result = test.API()->smoke())
            return result;
        if (test.Load(nullptr, nullptr, nullptr, L"missing-preview-module.dll") || !test.API())
            return 5;
        if (!test.Load(nullptr, nullptr, nullptr))
            return 6;
        return test.API()->smoke();
    }
    SetProcessDPIAware();
    const WNDCLASSEXW windowClass{
        sizeof(WNDCLASSEXW),  CS_CLASSDC, WindowProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr,
        L"ElderShellPreview", nullptr};
    if (!RegisterClassExW(&windowClass))
        return 1;
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Elder Shell UI Preview", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 900, 720, nullptr, nullptr, instance, nullptr);
    int result = 0;
    if (!window || !CreateDevice(window)) {
        result = 1;
    } else {
        if (!ui.Load(window, device.Get(), context.Get())) {
            result = 1;
        } else {
            ShowWindow(window, SW_SHOWDEFAULT);
            bool quit = false;
            while (!quit) {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                    quit |= message.message == WM_QUIT;
                }
                if (quit)
                    break;
                if (IsIconic(window)) {
                    WaitMessage();
                    continue;
                }
                if (resizeWidth && resizeHeight) {
                    target.Reset();
                    if (FAILED(swapChain->ResizeBuffers(0, resizeWidth, resizeHeight, DXGI_FORMAT_UNKNOWN, 0)) ||
                        !CreateTarget()) {
                        result = 1;
                        break;
                    }
                    resizeWidth = resizeHeight = 0;
                }
                ui.Poll(window, device.Get(), context.Get());
                if (!ui.API()) {
                    result = 1;
                    break;
                }
                constexpr float clear[] = {0.08f, 0.08f, 0.09f, 1};
                auto* renderTarget = target.Get();
                context->OMSetRenderTargets(1, &renderTarget, nullptr);
                context->ClearRenderTargetView(renderTarget, clear);
                ui.API()->frame();
                if (FAILED(swapChain->Present(1, 0))) {
                    result = 1;
                    break;
                }
            }
            ui.Close();
        }
    }
    target.Reset();
    swapChain.Reset();
    context.Reset();
    device.Reset();
    if (window && IsWindow(window))
        DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return result;
}
