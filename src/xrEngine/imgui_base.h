#pragma once

#include "IInputReceiver.h"
#include "../Include/xrRender/ImGuiRender.h"

#define IMGUI_DISABLE_OBSOLETE_KEYIO
struct ImGuiContext;
struct ImFont;
struct ImFontConfig;

namespace xr_imgui
{
    struct ide_backend;

    class ide :
        public pureRender,
        public pureFrame,
        public pureAppActivate,
        public pureAppDeactivate,
        public pureAppStart,
        public pureAppEnd,
        public pureScreenResolutionChanged,
        public IInputReceiver
    {
    public:
        ide();
        ~ide();

        bool is_shown() const { return m_shown; }
        void Show(bool bShow = true);
        bool is_input() const { return m_input; }
        void EnableInput(bool bInput = true);

    public:
        void OnDeviceCreate();
        void OnDeviceDestroy();
        void OnDeviceResetBegin() const;
        void OnDeviceResetEnd() const;

    public:
        // Interface implementations
        void OnFrame() final;
        void OnRender() final;
        void CaptureFrame();
        void RenderCaptured();
        void ClearCaptured();

        void OnAppActivate() final;
        void OnAppDeactivate() final;

        void OnAppStart() final;
        void OnAppEnd() final;

        virtual void OnScreenResolutionChanged();

        virtual void IR_Capture();
        virtual void IR_Release();

        void IR_OnMousePress(int key) final;
        void IR_OnMouseRelease(int key) final;
        void IR_OnMouseWheel(int direction) final;
        void IR_OnMouseMove(int x, int y) final;

        void IR_OnKeyboardPress(int key) final;
        void IR_OnKeyboardRelease(int key) final;

        // ImGui handles hold state on its own
        void IR_OnMouseHold(int key) final {};
        void IR_OnKeyboardHold(int key) final {};

        void InputChar(WPARAM param);
        void UpdateInputLang();

        ImFont* GetFont(LPCSTR name);
        ImFontConfig LoadImGuiFontConfig(string_path path, LPCSTR name);
        void LoadImGuiFont(string_path path, LPCSTR name);

    private:
        void InitBackend();
        void ShutdownBackend();

    private:
        void ShowMain();

    private:
        CTimer m_timer;
        IImGuiRender* m_render;
        ImGuiContext* m_context;
        ide_backend* m_backend_data;
        bool m_shown;
        bool m_input;
        bool firstframe;
        UINT32 keyboard_code_page;
        xr_vector<IReader*> ImGuiFontsPtr;
        xr_map<shared_str, ImFont*> ImFonts;
    };
} // namespace xr_imgui