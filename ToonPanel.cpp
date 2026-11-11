// =========================================================
// ファイルの情報[ToonPanel.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "input.h"
#include "ToonShader.h"
#include "ToonPanel.h"
#include "Camera.h"
#include "ToonPreset.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    bool panelOpen = false;
    bool previewWorkspace = false;
    const char* presetStatus = nullptr;
    constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

    // パネルは度数表示、描画と保存ではラジアンを使う。
    void editAngle(const char* label, float& angle, float minimum, float maximum)
    {
        float degrees = XMConvertToDegrees(angle);
        if (ImGui::SliderFloat(label, &degrees, XMConvertToDegrees(minimum),
            XMConvertToDegrees(maximum), "%.2f deg", sliderFlags))
        {
            angle = fmaxf(minimum, fminf(maximum, XMConvertToRadians(degrees)));
        }
    }

    void editShading(ToonSettings& settings)
    {
        if (!ImGui::CollapsingHeader("Shading", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Combo("Mode", &settings.shadingMode, "Toon\0Smooth\0Unlit\0");
        if (settings.shadingMode == 0)
        {
            ImGui::SliderInt("Bands", &settings.lightBands, 2, 8, "%d", sliderFlags);
            ImGui::SliderFloat("Threshold", &settings.lightThreshold, 0.0f, 0.98f, "%.3f", sliderFlags);
            ImGui::SliderFloat("Softness", &settings.edgeSoftness, 0.0f, 0.20f, "%.4f", sliderFlags);
        }

        if (settings.shadingMode != 2)
        {
            ImGui::SliderFloat("Shadow level", &settings.shadowBrightness, 0.0f, 1.0f, "%.3f", sliderFlags);
            ImGui::ColorEdit3("Shadow tint", settings.shadowTint, ImGuiColorEditFlags_Float);
        }
        ImGui::Checkbox("Base textures", &settings.useBaseTextures);
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.0f, 3.0f, "%.3f", sliderFlags);
    }

    void editOutline(ToonSettings& settings)
    {
        if (!ImGui::CollapsingHeader("Outline & rim", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        ImGui::Checkbox("Outline", &settings.outlineEnabled);
        ImGui::SliderFloat("Outline width", &settings.outlineWidth, 0.0f, 0.08f, "%.4f", sliderFlags);
        ImGui::ColorEdit3("Outline color", settings.outlineColor, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("Rim strength", &settings.rimStrength, 0.0f, 1.0f, "%.3f", sliderFlags);
        ImGui::SliderFloat("Rim power", &settings.rimPower, 1.0f, 12.0f, "%.2f", sliderFlags);
        ImGui::ColorEdit3("Rim color", settings.rimColor, ImGuiColorEditFlags_Float);
    }

    void editLighting(ToonSettings& settings)
    {
        editAngle("Light yaw", settings.lightYaw, -XM_PI, XM_PI);
        editAngle("Light pitch", settings.lightPitch, -XM_PIDIV2, XM_PIDIV2);
        editAngle("Model yaw", settings.modelYaw, -XM_PI, XM_PI);
    }

    void editCamera(ToonSettings& settings, CameraOrbit& orbit)
    {
        if (!ImGui::CollapsingHeader("Light & camera", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        editLighting(settings);
        editAngle("Orbit yaw", orbit.yaw, -XM_PI, XM_PI);
        editAngle("Orbit pitch", orbit.pitch, CameraOrbit::minPitch, CameraOrbit::maxPitch);
        ImGui::SliderFloat("Zoom", &orbit.distance, CameraOrbit::minDistance, CameraOrbit::maxDistance,
            "%.3f", sliderFlags | ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Look height", &orbit.lookHeight, CameraOrbit::minLookHeight,
            CameraOrbit::maxLookHeight, "%.3f", sliderFlags);

        if (ImGui::Button("Full body"))
        {
            orbit.distance = 4.2f;
            orbit.lookHeight = 0.95f;
            orbit.pitch = 0.20f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close-up"))
        {
            orbit.distance = 1.45f;
            orbit.lookHeight = 1.55f;
            orbit.pitch = 0.0f;
        }
    }

    void editPreset(ToonSettings& settings, CameraOrbit& orbit)
    {
        ImGui::Separator();
        if (ImGui::Button("Save settings"))
            presetStatus = ToonPreset::save(settings, orbit) ? "Settings saved." : "Could not save settings.";
        ImGui::SameLine();
        if (ImGui::Button("Load settings"))
            presetStatus = ToonPreset::load(settings, orbit) ? "Settings loaded." : "No valid saved settings.";
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            settings = ToonSettings{};
            orbit = CameraOrbit{};
            presetStatus = "Defaults restored.";
        }

        ImGui::TextDisabled("Ctrl + click a slider to enter a value.");
        if (presetStatus)
            ImGui::TextUnformatted(presetStatus);
    }
}

// =========================================================
// Win32・DirectX 11 の ImGui バックエンドを初期化
// =========================================================
void ToonPanel::init(HWND window, bool previewMode)
{
    previewWorkspace = previewMode;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(window))
    {
        ImGui::DestroyContext();
        return;
    }

    if (!ImGui_ImplDX11_Init(Renderer::GetDevice(), Renderer::GetDeviceContext()))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

void ToonPanel::uninit()
{
    if (!ImGui::GetCurrentContext())
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    panelOpen = false;
}

bool ToonPanel::isOpen()
{
    return panelOpen || previewWorkspace;
}

// =========================================================
// F1 の押下でパネルを開閉し、マウスをカメラと切り替える
// =========================================================
LRESULT ToonPanel::handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!ImGui::GetCurrentContext())
        return 0;

    if (previewWorkspace && message == WM_SYSCOMMAND && (wParam & 0xfff0) == SC_KEYMENU)
        return 1;

    if (!previewWorkspace && message == WM_KEYDOWN && wParam == VK_F1)
    {
        constexpr LPARAM previousKeyState = 1L << 30;
        if ((lParam & previousKeyState) == 0)
        {
            panelOpen = !panelOpen;
            Input::SetMouseCapture(!panelOpen && GetFocus() == window);
            if (panelOpen)
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            else
            {
                if (GetCapture() == window)
                    ReleaseCapture();

                ImGuiIO& io = ImGui::GetIO();
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();
                if (Input::IsMouseCaptured())
                    SetCursor(nullptr);
            }
        }
        return 1;
    }

    if (isOpen())
        return ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);

    return 0;
}

// =========================================================
// 描画前に調整値を更新し、そのフレームのモデルに反映
// =========================================================
void ToonPanel::beginFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // スワップチェーンの解像度は固定。ウィンドウの拡縮に合わせて補正する。
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
        io.DisplayFramebufferScale = ImVec2(SCREEN_WIDTH / io.DisplaySize.x, SCREEN_HEIGHT / io.DisplaySize.y);

    ImGui::NewFrame();
}

void ToonPanel::beginFrame(ToonSettings* settings, CameraOrbit* orbit)
{
    beginFrame();
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(365.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Player / Toon Lab"))
    {
        if (settings && orbit)
        {
            ImGui::PushItemWidth(-112.0f);
            editShading(*settings);
            editOutline(*settings);
            editCamera(*settings, *orbit);
            ImGui::PopItemWidth();
            editPreset(*settings, *orbit);
        }
        else
        {
            ImGui::TextUnformatted("Player and camera are unavailable.");
        }
    }
    ImGui::End();
}

// =========================================================
// 3D 描画が完了した後、Present の前にパネルを描画
// =========================================================
void ToonPanel::editAppearance(ToonSettings& settings)
{
    editShading(settings);
    editOutline(settings);
    if (ImGui::CollapsingHeader("Light & model", ImGuiTreeNodeFlags_DefaultOpen))
        editLighting(settings);
}

void ToonPanel::draw()
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
