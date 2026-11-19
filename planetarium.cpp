// =========================================================
// 星座ドームの展開・カメラ・描画 [planetarium.cpp]
// =========================================================
#include "main.h"
#include "renderer.h"
#include "planetarium.h"
#include "constellation.h"
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

    struct SkyLightConstants
    {
        XMFLOAT4 centerRadius;
        XMFLOAT4 skylight;
    };

    constexpr float worldSpeed = 0.2f;
    constexpr float domeRadius = 12.0f;
    const Vector3 domeTint{ 0.22f, 0.52f, 0.86f };

    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Buffer> skyLightConstants;
    ComPtr<ID3D11Buffer> constellationConstants;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11BlendState> blendState;
    ComPtr<ID3D11ShaderResourceView> starPanorama;
    ComPtr<ID3D11SamplerState> starSampler;
    Vector3 center{};
    float progress = 0.0f;
    float worldTicks = 0.0f;
    bool held = false;
    bool setConstellationYaw = false;
    float constellationYaw = 0.0f;
    Vector3 aimCorrection{ 0.0f, 0.0f, 0.0f };

    // 原画は残し、GPUには縮小した全天画像だけを載せる。
    void loadStarPanorama(ID3D11Device* device)
    {
        constexpr size_t maxWidth = 4096;
        ScratchImage image, resized;
        if (FAILED(LoadFromWICFile(L"asset/texture/planetarium2.jpg", WIC_FLAGS_IGNORE_SRGB, nullptr, image)))
            return;

        const auto* sky = &image;
        const auto& metadata = image.GetMetadata();
        if (metadata.width > maxWidth)
        {
            const auto height = std::max(size_t(1), metadata.height * maxWidth / metadata.width);
            if (FAILED(Resize(*image.GetImage(0, 0, 0), maxWidth, height, TEX_FILTER_FANT, resized)))
                return;
            sky = &resized;
        }

        if (FAILED(CreateShaderResourceView(device, sky->GetImages(), sky->GetImageCount(),
            sky->GetMetadata(), starPanorama.GetAddressOf())))
            return;

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sampler, starSampler.GetAddressOf());
    }

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

    buffer.ByteWidth = sizeof(SkyLightConstants);
    device->CreateBuffer(&buffer, nullptr, skyLightConstants.GetAddressOf());

    buffer.ByteWidth = sizeof(constellation::DrawData);
    device->CreateBuffer(&buffer, nullptr, constellationConstants.GetAddressOf());

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
    if (SUCCEEDED(device->CreateBlendState(&blend, blendState.GetAddressOf())))
        loadStarPanorama(device);
}

void planetarium::uninit()
{
    reset();
    starSampler.Reset();
    starPanorama.Reset();
    blendState.Reset();
    rasterizer.Reset();
    constellationConstants.Reset();
    skyLightConstants.Reset();
    constants.Reset();
    pixelShader.Reset();
    vertexShader.Reset();
}

void planetarium::reset()
{
    held = false;
    setConstellationYaw = false;
    constellation::reset();
    aimCorrection.zero();
    progress = 0.0f;
    worldTicks = 0.0f;
}

// =========================================================
// プレイヤーの入力と、実時間で進む展開演出
// =========================================================
float planetarium::updatePlayer()
{
    const bool requested = blendState && Renderer::sceneDepth() &&
        Input::isGameplayActive() && Input::GetKeyPress(VK_RBUTTON);
    if (requested && !held)
    {
        constellation::open(constellation::scorpio());
        setConstellationYaw = true;
    }
    held = requested;

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
    if (setConstellationYaw)
    {
        constellationYaw = orbitRotation.y;
        setConstellationYaw = false;
    }

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
// 素の照準が星に近い間だけ補助し、マウスで離せば解除する。
void planetarium::assistAim(const Vector3& eye, Vector3& target, CXMMATRIX projection)
{
    if (!held || opening() < 0.71f)
    {
        aimCorrection.zero();
        return;
    }

    const auto origin = XMVectorSet(eye.x, eye.y, eye.z, 1.0f);
    const auto aim = XMVectorSet(target.x, target.y, target.z, 1.0f);
    const auto view = XMMatrixLookAtLH(origin, aim, XMVectorSet(0, 1, 0, 0));
    constellation::DrawData stars{};
    XMFLOAT3 aimedStar{};
    constellation::project(view * projection, {center.x, center.y, center.z},
        0.25f + domeRadius * opening(), constellationYaw, 1.0f, true, stars, &aimedStar);

    Vector3 correction(0.0f, 0.0f, 0.0f);
    if (stars.state.w > 1.0f)
    {
        const auto direction = XMVector3Normalize(XMLoadFloat3(&aimedStar) - origin);
        const auto offset = origin + direction * XMVector3Length(aim - origin) - aim;
        correction = { XMVectorGetX(offset), XMVectorGetY(offset), XMVectorGetZ(offset) };
    }

    const float blend = 1.0f - expf(-22.0f * Manager::animationDeltaSeconds());
    aimCorrection += (correction - aimCorrection) * blend;
    target += aimCorrection;
}

void planetarium::bindLighting(const Vector3& playerPosition)
{
    constexpr UINT slot = 9;
    auto* context = Renderer::GetDeviceContext();
    auto* buffer = opening() > 0.0f ? skyLightConstants.Get() : nullptr;
    if (buffer)
    {
        const float weight = opening();
        const SkyLightConstants data{
            { playerPosition.x, playerPosition.y, playerPosition.z, 0.25f + domeRadius * weight },
            { 0.18f, 0.34f, 0.64f, weight }
        };
        context->UpdateSubresource(buffer, 0, nullptr, &data, 0, 0);
    }
    context->PSSetConstantBuffers(slot, 1, &buffer);
}

void planetarium::draw(FXMMATRIX view, CXMMATRIX projection)
{
    if (progress <= 0.0f || !blendState || !Renderer::sceneDepth())
        return;

    const float weight = opening();
    DomeConstants data{};
    XMStoreFloat4x4(&data.inverseViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, view * projection)));
    data.centerRadius = { center.x, center.y, center.z, 0.25f + domeRadius * weight };
    data.tint = { domeTint.x, domeTint.y, domeTint.z, weight };
    data.settings = { 1.0f / SCREEN_WIDTH, 1.0f / SCREEN_HEIGHT, starPanorama && starSampler ? 1.0f : 0.0f, 0.0f };

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
    if (constellationConstants)
    {
        constellation::DrawData stars{};
        const float visibility = std::max(0.0f, std::min(1.0f, (weight - 0.35f) / 0.45f));
        constellation::project(view * projection, {center.x, center.y, center.z}, data.centerRadius.w, constellationYaw,
            visibility, held && visibility >= 0.8f, stars);
        context->UpdateSubresource(constellationConstants.Get(), 0, nullptr, &stars, 0, 0);
    }
    context->PSSetConstantBuffers(10, 1, constellationConstants.GetAddressOf());
    ID3D11ShaderResourceView* textures[] = { Renderer::sceneDepth(), starPanorama.Get() };
    context->PSSetShaderResources(8, 2, textures);
    context->PSSetSamplers(2, 1, starSampler.GetAddressOf());
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->Draw(3, 0);

    ID3D11ShaderResourceView* emptyTextures[2]{};
    context->PSSetShaderResources(8, 2, emptyTextures);
    ID3D11SamplerState* emptySampler = nullptr;
    context->PSSetSamplers(2, 1, &emptySampler);
    context->OMSetRenderTargets(1, target.GetAddressOf(), depth.Get());
    context->RSSetState(oldRasterizer.Get());
    context->OMSetBlendState(oldBlend.Get(), blendFactor, sampleMask);
}
