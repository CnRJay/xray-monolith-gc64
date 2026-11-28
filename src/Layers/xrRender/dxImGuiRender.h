#pragma once

#include "../Include/xrRender/ImGuiRender.h"
#include <imgui.h>

class dxImGuiRender : public IImGuiRender
{
    void SetState(ImDrawData* data);

public:
    dxImGuiRender() = default;
    ~dxImGuiRender() override { ClearCaptured(); }

    void Copy(IImGuiRender& _in) override;

    void Frame() override;
    void Render(ImDrawData* data) override;
    void CaptureDrawData(ImDrawData* data) override;
    void RenderCaptured() override;
    void ClearCaptured() override;

    void OnDeviceCreate(ImGuiContext* context) override;
    void OnDeviceDestroy() override;
    void OnDeviceResetBegin() override;
    void OnDeviceResetEnd() override;

private:
    ImDrawData m_captured_data;
    xr_vector<ImDrawList*> m_captured_lists;
};