// =========================================================
// ファイルの情報[ToonShader.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

class AnimationModel;

struct ToonSettings
{
    int shadingMode = 0;
    int lightBands = 2;
    float lightThreshold = 0.45f;
    float edgeSoftness = 0.015f;
    float shadowBrightness = 0.45f;
    float shadowTint[3] = { 1.0f, 1.0f, 1.0f };
    float exposure = 1.0f;
    bool useBaseTextures = true;
    bool outlineEnabled = true;
    float outlineWidth = 0.018f;
    float outlineColor[3] = { 0.04f, 0.035f, 0.06f };
    float rimStrength = 0.0f;
    float rimPower = 4.0f;
    float rimColor[3] = { 0.65f, 0.8f, 1.0f };
    float lightYaw = -2.677945f;
    float lightPitch = 0.729728f;
    float modelYaw = 0.0f;
};

class ToonShader
{
private:
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_vertexLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_settingsBuffer;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_outlineRasterizer;
    ToonSettings m_settings;

    void uploadSettings(float outlineWidth) const;

public:
    void init();
    void uninit();
    void bindSurface() const;
    void drawOutline(AnimationModel& model) const;
    ToonSettings& settings() { return m_settings; }
};
