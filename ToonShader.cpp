// =========================================================
// ファイルの情報[ToonShader.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "ToonShader.h"
#include "animationModel.h"

namespace
{
    // toonCommon.hlsli と同じ順番で 16 バイト境界にそろえる。
    struct ToonConstants
    {
        float lightThreshold;
        float edgeSoftness;
        float shadowBrightness;
        float outlineWidth;
        float exposure;
        float rimStrength;
        float rimPower;
        int shadingMode;
        int lightBands;
        int useBaseTextures;
        float padding[2];
        XMFLOAT4 outlineColor;
        XMFLOAT4 shadowTint;
        XMFLOAT4 rimColor;
        XMFLOAT4 lightDirection;
    };

    static_assert(sizeof(ToonConstants) == 112, "HLSL constant buffer size mismatch.");
    constexpr UINT settingsSlot = 6;
}

// =========================================================
// トゥーン描画と輪郭パスのリソースを作成
// =========================================================
void ToonShader::init()
{
    Renderer::CreateVertexShader(m_vertexShader.GetAddressOf(), m_vertexLayout.GetAddressOf(),
        "shader\\toonVS.cso");
    Renderer::CreatePixelShader(m_pixelShader.GetAddressOf(), "shader\\toonPS.cso");

    auto* device = Renderer::GetDevice();
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(ToonConstants);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&bufferDesc, nullptr, m_settingsBuffer.GetAddressOf());
    assert(m_settingsBuffer);

    // 輪郭パスは押し出したメッシュの裏面だけを描画する。
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_FRONT;
    rasterizerDesc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rasterizerDesc, m_outlineRasterizer.GetAddressOf());
    assert(m_outlineRasterizer);
}

void ToonShader::uninit()
{
    m_outlineRasterizer.Reset();
    m_settingsBuffer.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();
    m_vertexLayout.Reset();
}

// =========================================================
// プレイヤー本体のシェーダーを設定
// =========================================================
void ToonShader::bindSurface() const
{
    auto* context = Renderer::GetDeviceContext();
    context->IASetInputLayout(m_vertexLayout.Get());
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    uploadSettings(0.0f);
}

// =========================================================
// 本体と同じポーズで輪郭を描画し、ラスタライザを戻す
// =========================================================
void ToonShader::drawOutline(AnimationModel& model) const
{
    if (!m_settings.outlineEnabled || m_settings.outlineWidth <= 0.0f || !m_outlineRasterizer)
        return;

    auto* context = Renderer::GetDeviceContext();
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
    context->RSGetState(previousRasterizer.GetAddressOf());
    context->RSSetState(m_outlineRasterizer.Get());
    uploadSettings(m_settings.outlineWidth);
    model.drawOutline();
    context->RSSetState(previousRasterizer.Get());
}

// =========================================================
// 現在の調整値を VS・PS 共通の定数バッファに送る
// =========================================================
void ToonShader::uploadSettings(float outlineWidth) const
{
    const float horizontal = cosf(m_settings.lightPitch);
    const ToonConstants constants{
        m_settings.lightThreshold, m_settings.edgeSoftness, m_settings.shadowBrightness, outlineWidth,
        m_settings.exposure, m_settings.rimStrength, m_settings.rimPower, m_settings.shadingMode,
        m_settings.lightBands, m_settings.useBaseTextures, {},
        { m_settings.outlineColor[0], m_settings.outlineColor[1], m_settings.outlineColor[2], 1.0f },
        { m_settings.shadowTint[0], m_settings.shadowTint[1], m_settings.shadowTint[2], 1.0f },
        { m_settings.rimColor[0], m_settings.rimColor[1], m_settings.rimColor[2], 1.0f },
        { sinf(m_settings.lightYaw) * horizontal, sinf(m_settings.lightPitch),
            cosf(m_settings.lightYaw) * horizontal, 0.0f }
    };
    auto* context = Renderer::GetDeviceContext();
    auto* buffer = m_settingsBuffer.Get();
    context->UpdateSubresource(buffer, 0, nullptr, &constants, 0, 0);
    context->VSSetConstantBuffers(settingsSlot, 1, &buffer);
    context->PSSetConstantBuffers(settingsSlot, 1, &buffer);
}
