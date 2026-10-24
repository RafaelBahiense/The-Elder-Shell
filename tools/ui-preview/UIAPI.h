#pragma once

#include "Session.h"
#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace preview {
    struct UIAPI {
        unsigned int version;
        std::size_t sessionSize, menuStateSize;
        bool (*attach)(HWND, ID3D11Device*, ID3D11DeviceContext*, Session*);
        void (*detach)();
        void (*frame)();
        LRESULT (*message)(HWND, UINT, WPARAM, LPARAM);
        int (*smoke)();
    };

    using QueryUI = const UIAPI* (*)();
}
