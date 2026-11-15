// =========================================================
// 星座ドームの展開・カメラ・描画 [planetarium.cpp]
// =========================================================
#include "main.h"
#include "renderer.h"
#include "planetarium.h"
#include "input.h"
#include "manager.h"
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    struct DomeConstants
    {
        XMFLOAT4X4 inverseViewProjection;
        XMFLOAT4 centerRadius;
        XMFLOAT4 tint;
        XMFLOAT4 settings;
    };

    constexpr float worldSpeed = 0.2f;
    constexpr float domeRadius = 12.0f;
    const Vector3 domeTint{ 0.22f, 0.52f, 0.86f };

    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11BlendState> blendState;
    Vector3 center{};
    float progress = 0.0f;
    float worldTicks = 0.0f;
    bool held = false;

    float opening()
    {
        return progress * progress * (3.0f - 2.0f * progress);
    }
}

// =========================================================
// フルスクリーン描画用のリソースを読み込む
// =========================================================
void planetarium::init()
{
    auto* device = Renderer::GetDevice();
    ComPtr<ID3DBlob> vertexCode, pixelCode;
    if (FAILED(D3DReadFileToBlob(L"shader/planetariumVS.cso", vertexCode.GetAddressOf())) ||
        FAILED(D3DReadFileToBlob(L"shader/planetariumPS.cso", pixelCode.GetAddressOf())))
        return;

    if (FAILED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
        nullptr, vertexShader.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
        nullptr, pixelShader.GetAddressOf())))
        return;

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(DomeConstants);
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&buffer, nullptr, constants.GetAddressOf())))
        return;

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&raster, rasterizer.GetAddressOf())))
        return;

    D3D11_BLEND_DESC blend{};
    auto& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_ZERO;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blend, blendState.GetAddressOf());
}

void planetarium::uninit()
{
    reset();
    blendState.Reset();
    rasterizer.Reset();
    constants.Reset();
    pixelShader.Reset();
    vertexShader.Reset();
}

void planetarium::reset()
{
    held = false;
    progress = 0.0f;
    worldTicks = 0.0f;
}

// =========================================================
// プレイヤーの入力と、実時間で進む展開演出
// =========================================================
float planetarium::updatePlayer()
{
    held = blendState && Renderer::sceneDepth() &&
        Input::isGameplayActive() && Input::GetKeyPress('F');

    const float dt = Manager::animationDeltaSeconds();
    progress = std::max(0.0f, std::min(1.0f, progress + dt * (held ? 2.1f : -3.8f)));
    return held ? worldSpeed : 1.0f;
}

bool planetarium::updateWorld()
{
    // 既存の固定刻みを保ち、ワールド側の更新頻度だけを落とす。
    if (!held)
    {
        worldTicks = 0.0f;
        return true;
    }

    worldTicks += worldSpeed;
    if (worldTicks < 1.0f)
        return false;

    worldTicks -= 1.0f;
    return true;
}

void planetarium::frameCamera(const Vector3& playerPosition, const Vector3& orbitRotation, float orbitDistance,
    Vector3& eye, Vector3& target)
{
    if (progress <= 0.0f)
        return;

    center = playerPosition;

    // 通常カメラと角度を共有し、肩越しの距離と構図だけを変える。
    const float yaw = orbitRotation.y;
    const Vector3 forward(sinf(yaw), 0.0f, cosf(yaw));
    const Vector3 right(cosf(yaw), 0.0f, -sinf(yaw));
    const float pitch = orbitRotation.x - 0.17f;
    const float distance = std::max(0.85f, std::min(6.0f, orbitDistance * 0.28f));
    const auto lookDirection = forward * cosf(pitch) - Vector3(0.0f, sinf(pitch), 0.0f);
    auto shoulder = center + Vector3(0.0f, 1.30f, 0.0f)
        + right * (distance * 0.58f) - lookDirection * distance;
    shoulder.y = std::max(shoulder.y, center.y + 0.40f);
    const auto aim = shoulder + lookDirection * (distance + 2.5f);
    const float weight = opening();

    eye += (shoulder - eye) * weight;
    target += (aim - target) * weight;
}

// =========================================================
// シーン深度からドームの内側だけを残す
// =========================================================
void planetarium::draw(FXMMATRIX view, CXMMATRIX projection)
{
    if (progress <= 0.0f || !blendState || !Renderer::sceneDepth())
        return;

    const float weight = opening();
    DomeConstants data{};
    XMStoreFloat4x4(&data.inverseViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, view * projection)));
    data.centerRadius = { center.x, center.y, center.z, 0.25f + domeRadius * weight };
    data.tint = { domeTint.x, domeTint.y, domeTint.z, weight };
    data.settings = { 1.0f / SCREEN_WIDTH, 1.0f / SCREEN_HEIGHT, 0.0f, 0.0f };

    auto* context = Renderer::GetDeviceContext();
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11RasterizerState> oldRasterizer;
    ComPtr<ID3D11BlendState> oldBlend;
    float blendFactor[4]{};
    UINT sampleMask = 0;
    context->OMGetRenderTargets(1, target.GetAddressOf(), depth.GetAddressOf());
    context->RSGetState(oldRasterizer.GetAddressOf());
    context->OMGetBlendState(oldBlend.GetAddressOf(), blendFactor, &sampleMask);

    // 同じ深度を読み書きしないよう、一時的に出力先から外す。
    context->OMSetRenderTargets(1, target.GetAddressOf(), nullptr);
    context->RSSetState(rasterizer.Get());
    context->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
    context->UpdateSubresource(constants.Get(), 0, nullptr, &data, 0, 0);
    context->PSSetConstantBuffers(8, 1, constants.GetAddressOf());
    auto* sceneDepth = Renderer::sceneDepth();
    context->PSSetShaderResources(8, 1, &sceneDepth);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->Draw(3, 0);

    sceneDepth = nullptr;
    context->PSSetShaderResources(8, 1, &sceneDepth);
    context->OMSetRenderTargets(1, target.GetAddressOf(), depth.Get());
    context->RSSetState(oldRasterizer.Get());
    context->OMSetBlendState(oldBlend.Get(), blendFactor, sampleMask);
}
