/****************************************************************************
MIT License

Copyright (c) 2022 Guillaume Boissé

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
****************************************************************************/
#ifndef GFX_INCLUDE_GFX_WINDOW_H
#define GFX_INCLUDE_GFX_WINDOW_H

#include "gfx.h"

//!
//! Window management.
//!

class GfxWindow { friend class GfxWindowInternal; uint64_t handle; HWND hwnd; public:
                  inline bool operator ==(GfxWindow const &other) const { return handle == other.handle; }
                  inline bool operator !=(GfxWindow const &other) const { return handle != other.handle; }
                  inline GfxWindow() { memset(this, 0, sizeof(*this)); }
                  inline operator bool() const { return !!handle; }
                  inline HWND getHWND() const { return hwnd; }
                  inline operator HWND() const { return hwnd; } };

enum GfxCreateWindowFlag
{
    kGfxCreateWindowFlag_MaximizeWindow = 1 << 0,
    kGfxCreateWindowFlag_HideWindow     = 1 << 1
};
typedef uint32_t GfxCreateWindowFlags;

GfxWindow gfxCreateWindow(uint32_t window_width, uint32_t window_height, char const *window_title = nullptr, GfxCreateWindowFlags flags = 0);
GfxResult gfxDestroyWindow(GfxWindow window);

GfxResult gfxWindowPumpEvents(GfxWindow window);

bool gfxWindowIsKeyDown(GfxWindow window, uint32_t key_code);   // https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
bool gfxWindowIsKeyPressed(GfxWindow window, uint32_t key_code);
bool gfxWindowIsKeyReleased(GfxWindow window, uint32_t key_code);
bool gfxWindowIsCloseRequested(GfxWindow window);
bool gfxWindowIsMinimized(GfxWindow window);
bool gfxWindowIsMaximized(GfxWindow window);
int gfxWindowXMousePosition(GfxWindow window);
int gfxWindowYMousePosition(GfxWindow window);
int gfxWindowXMouseOffset(GfxWindow window);
int gfxWindowYMouseOffset(GfxWindow window);

#endif //! GFX_INCLUDE_GFX_WINDOW_H

//!
//! Implementation details.
//!

#ifdef GFX_IMPLEMENTATION_DEFINE

#pragma once

#include "gfx_imgui.h"  // for (optional) ImGui integration
#include "backends/imgui_impl_win32.h"

class GfxWindowInternal
{
    GFX_NON_COPYABLE(GfxWindowInternal);

    HWND window_ = {};
    bool is_minimized_ = false;
    bool is_maximized_ = false;
    bool is_close_requested_ = false;
    bool is_key_down_[VK_OEM_CLEAR] = {};
    bool is_previous_key_down_[VK_OEM_CLEAR] = {};
    int mouse_position_x_ = 0;
    int mouse_position_y_ = 0;
    int last_mouse_position_x_ = 0;
    int last_mouse_position_y_ = 0;
    float x_offset;
    float y_offset;
 

public:
    GfxWindowInternal(GfxWindow &window) { window.handle = reinterpret_cast<uint64_t>(this); }
    ~GfxWindowInternal() { terminate(); }

    GfxResult initialize(GfxWindow &window, uint32_t window_width, uint32_t window_height, char const *window_title, GfxCreateWindowFlags flags)
    {
        window_title = (!window_title ? "gfx" : window_title);

        WNDCLASSEX
        window_class               = {};
        window_class.cbSize        = sizeof(window_class);
        window_class.style         = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc   = WindowProc;
        window_class.hInstance     = GetModuleHandle(nullptr);
        window_class.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        window_class.lpszClassName = window_title;

        RegisterClassEx(&window_class);

        RECT window_rect = { 0, 0, (LONG)window_width, (LONG)window_height };

        DWORD const window_style = WS_OVERLAPPEDWINDOW;

        AdjustWindowRect(&window_rect, window_style, FALSE);

        window_ = CreateWindowEx(0,
                                 window_title,
                                 window_title,
                                 window_style,
                                 CW_USEDEFAULT,
                                 CW_USEDEFAULT,
                                 window_rect.right - window_rect.left,
                                 window_rect.bottom - window_rect.top,
                                 nullptr,
                                 nullptr,
                                 GetModuleHandle(nullptr),
                                 nullptr);

        SetWindowLongPtrA(window_, GWLP_USERDATA, (LONG_PTR)this);

        ShowWindow(window_, (flags & kGfxCreateWindowFlag_MaximizeWindow) != 0 ? SW_SHOWMAXIMIZED :
                            (flags & kGfxCreateWindowFlag_HideWindow    ) != 0 ? SW_HIDE          :
                                                                                 SW_SHOWDEFAULT);

        window.hwnd = window_;

        return kGfxResult_NoError;
    }

    GfxResult terminate()
    {
        if(window_)
            DestroyWindow(window_);
        if(ImGui_ImplWin32_GetBackendData() != nullptr)
            ImGui_ImplWin32_Shutdown();

        return kGfxResult_NoError;
    }

    GfxResult pumpEvents()
    {
        MSG msg = {};
        memcpy(is_previous_key_down_, is_key_down_, sizeof(is_key_down_));
        while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return kGfxResult_NoError;
    }

    inline bool getIsKeyDown(uint32_t key_code) const
    {
        return (key_code < ARRAYSIZE(is_key_down_) ? is_key_down_[key_code] : false);
    }

    inline bool getIsKeyPressed(uint32_t key_code) const
    {
        return (key_code < ARRAYSIZE(is_key_down_) ? is_key_down_[key_code] && !is_previous_key_down_[key_code] : false);
    }

    inline bool getIsKeyReleased(uint32_t key_code) const
    {
        return (key_code < ARRAYSIZE(is_key_down_) ? !is_key_down_[key_code] && is_previous_key_down_[key_code] : false);
    }

    inline bool getIsCloseRequested() const
    {
        return is_close_requested_;
    }

    inline bool getIsMinimized() const
    {
        return is_minimized_;
    }

    inline bool getIsMaximized() const
    {
        return is_maximized_;
    }

    inline int getXMousePosition() const
    {
        return mouse_position_x_;
    }
    
    inline int getYMousePosition() const
    {
        return mouse_position_y_;
    }

    inline float getXMouseOffset()
    {
        float temp_offset = x_offset;
        x_offset = 0.0f;
        return temp_offset;
    }

    inline float getYMouseOffset()
    {
        float temp_offset = y_offset;
        y_offset = 0.0f;
        return temp_offset;
    }

    static inline GfxWindowInternal *GetGfxWindow(GfxWindow window) { return reinterpret_cast<GfxWindowInternal *>(window.handle); }

private:
    inline void updateKeyBinding(uint32_t message, uint32_t key_code)
    {
        bool is_down;
        switch(message)
        {
        case WM_KEYUP: case WM_SYSKEYUP:
            is_down = false;
            break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            is_down = true;
            break;
        default:
            return;
        }
        is_key_down_[key_code] = is_down;
    }

    inline void resetKeyBindings()
    {
        for(uint32_t i = 0; i < ARRAYSIZE(is_key_down_); ++i)
            is_key_down_[i] = false;
    }

    inline void updateMousePosition(int x, int y) 
    {
        static bool first_update = true;

        if (first_update) {
            this->last_mouse_position_x_ = x;
            this->last_mouse_position_y_ = y;
            first_update = false;
        }

        x_offset = x - this->last_mouse_position_x_;
        y_offset = this->last_mouse_position_y_ - y;

        this->last_mouse_position_x_ = x;
        this->last_mouse_position_y_ = y;

        this->mouse_position_x_ = x;
        this->mouse_position_y_ = y;

    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        GfxWindowInternal *gfx_window = (GfxWindowInternal *)GetWindowLongPtrA(window, GWLP_USERDATA);
        if(gfx_window != nullptr)
        {
            if(ImGui_ImplWin32_GetBackendData() == nullptr && gfxImGuiIsInitialized())
                ImGui_ImplWin32_Init(gfx_window->window_);
            switch(message)
            {
            case WM_SIZE:
                {
                    gfx_window->is_minimized_ = IsIconic(gfx_window->window_);
                    gfx_window->is_maximized_ = IsZoomed(gfx_window->window_);
                }
                break;
            case WM_DESTROY:
                {
                    gfx_window->is_close_requested_ = true;
                }
                return 0;
            case WM_KILLFOCUS:
                {
                    gfx_window->resetKeyBindings();
                }
                break;
            case WM_MOUSEMOVE:
                {
                uint32_t x_position = GET_X_LPARAM(l_param);
                uint32_t y_position = GET_Y_LPARAM(l_param);
                gfx_window->updateMousePosition(x_position, y_position);
                }
            case WM_CHAR:
            case WM_SETCURSOR:
            case WM_DEVICECHANGE:
            case WM_KEYUP: case WM_SYSKEYUP:
            case WM_KEYDOWN: case WM_SYSKEYDOWN:
            case WM_LBUTTONUP: case WM_RBUTTONUP:
            case WM_MBUTTONUP: case WM_XBUTTONUP:
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
                if(w_param < ARRAYSIZE(is_key_down_))
                    gfx_window->updateKeyBinding(message, (uint32_t)w_param);
                if(ImGui_ImplWin32_GetBackendData() != nullptr)
                    ImGui_ImplWin32_WndProcHandler(gfx_window->window_, message, w_param, l_param);
                break;
            default:
                break;
            }
        }
        return DefWindowProc(window, message, w_param, l_param);
    }
};

GfxWindow gfxCreateWindow(uint32_t window_width, uint32_t window_height, char const *window_title, GfxCreateWindowFlags flags)
{
    GfxResult result;
    GfxWindow window = {};
    GfxWindowInternal *gfx_window = new GfxWindowInternal(window);
    if(!gfx_window) return window;  // out of memory
    result = gfx_window->initialize(window, window_width, window_height, window_title, flags);
    if(result != kGfxResult_NoError)
    {
        window = {};
        delete gfx_window;
        GFX_PRINT_ERROR(result, "Failed to create new window");
    }
    return window;
}

GfxResult gfxDestroyWindow(GfxWindow window)
{
    delete GfxWindowInternal::GetGfxWindow(window);
    return kGfxResult_NoError;
}

GfxResult gfxWindowPumpEvents(GfxWindow window)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return kGfxResult_InvalidParameter;
    return gfx_window->pumpEvents();
}

bool gfxWindowIsKeyDown(GfxWindow window, uint32_t key_code)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsKeyDown(key_code);
}

bool gfxWindowIsKeyPressed(GfxWindow window, uint32_t key_code)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsKeyPressed(key_code);
}

bool gfxWindowIsKeyReleased(GfxWindow window, uint32_t key_code)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsKeyReleased(key_code);
}

bool gfxWindowIsCloseRequested(GfxWindow window)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsCloseRequested();
}

bool gfxWindowIsMinimized(GfxWindow window)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsMinimized();
}

bool gfxWindowIsMaximized(GfxWindow window)
{
    GfxWindowInternal *gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if(!gfx_window) return false;   // invalid window handle
    return gfx_window->getIsMaximized();
}

int gfxWindowXMousePosition(GfxWindow window)
{
    GfxWindowInternal* gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if (!gfx_window) return false;   // invalid window handle
    return gfx_window->getXMousePosition();
}

int gfxWindowYMousePosition(GfxWindow window)
{
    GfxWindowInternal* gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if (!gfx_window) return false;   // invalid window handle
    return gfx_window->getYMousePosition();
}

int gfxWindowXMouseOffset(GfxWindow window)
{
    GfxWindowInternal* gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if (!gfx_window) return false;   // invalid window handle
    return gfx_window->getXMouseOffset();
}

int gfxWindowYMouseOffset(GfxWindow window)
{
    GfxWindowInternal* gfx_window = GfxWindowInternal::GetGfxWindow(window);
    if (!gfx_window) return false;   // invalid window handle
    return gfx_window->getYMouseOffset();
}


#endif //! GFX_IMPLEMENTATION_DEFINE
