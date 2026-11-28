#include "main.h"
#include "renderer.h"
#include "SkyDome.h"

#include <algorithm>
#include <cmath>
#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT skySlot = 13;
    constexpr UINT cloudDensitySlot = 8;
    constexpr UINT cloudSamplerSlot = 4;

    struct SkyFrame
    {
        XMFLOAT4 sun;
        XMFLOAT4 moon;
        XMFLOAT4 cycle;
        XMFLOAT4 cloudWind;
    };

    float smooth(float low, float high, float value)
    {
        const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    XMFLOAT4 celestialDirection(float hour, float declination)
    {
        constexpr float latitude = 35.0f * XM_PI / 180.0f;
        const float rotation = (hour - 12.0f) * XM_PI / 12.0f;
        const float east = -std::cos(declination) * std::sin(rotation);
        const float up = std::sin(latitude) * std::sin(declination)
            + std::cos(latitude) * std::cos(declination) * std::cos(rotation);
        const float north = std::cos(latitude) * std::sin(declination)
            - std::sin(latitude) * std::cos(declination) * std::cos(rotation);
        return { east, up, north, 0.0f };
    }
}

void SkyDome::Init()
{
    // The manager draws it at the far plane after opaque scenery.
    m_Layer = -1;
    ComPtr<ID3DBlob> vertex, pixel;
    if (FAILED(D3DReadFileToBlob(L"shader/SkyDomeVS.cso", vertex.GetAddressOf()))
        || FAILED(D3DReadFileToBlob(L"shader/SkyDomePS.cso", pixel.GetAddressOf()))) return;

    auto* gpu = Renderer::GetDevice();
    if (FAILED(gpu->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, m_vertex.GetAddressOf()))
        || FAILED(gpu->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, m_pixel.GetAddressOf()))) return;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(SkyFrame);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gpu->CreateBuffer(&desc, nullptr, m_frame.GetAddressOf());

    DirectX::ScratchImage density;
    DirectX::TexMetadata metadata{};
    if (FAILED(DirectX::LoadFromDDSFile(L"asset/texture/SummerClouds.dds", DirectX::DDS_FLAGS_NONE,
        &metadata, density))) return;
    if (FAILED(DirectX::CreateShaderResourceView(gpu, density.GetImages(), density.GetImageCount(),
        metadata, m_cloudDensity.GetAddressOf()))) return;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    gpu->CreateSamplerState(&sampler, m_cloudSampler.GetAddressOf());
}

void SkyDome::Uninit()
{
    ID3D11Buffer* none = nullptr;
    ID3D11ShaderResourceView* noDensity = nullptr;
    ID3D11SamplerState* noSampler = nullptr;
    auto* context = Renderer::GetDeviceContext();
    context->PSSetConstantBuffers(skySlot, 1, &none);
    context->PSSetShaderResources(cloudDensitySlot, 1, &noDensity);
    context->PSSetSamplers(cloudSamplerSlot, 1, &noSampler);
    m_cloudDensity.Reset();
    m_cloudSampler.Reset();
    m_frame.Reset();
    m_vertex.Reset();
    m_pixel.Reset();
    GameObject::Uninit();
}

void SkyDome::advance(float seconds, bool editing)
{
    if (editing && !m_clock.runInEditor) return;
    seconds = std::clamp(seconds, 0.0f, 0.25f);
    m_clock.hour = std::fmod(m_clock.hour + seconds * 24.0f / (60.0f * m_clock.dayMinutes), 24.0f);
    m_clock.cloudSeconds += seconds;
}

void SkyDome::bindFrame() const
{
    if (!m_frame) return;
    SkyFrame frame{};
    frame.sun = celestialDirection(m_clock.hour, 22.5f * XM_PI / 180.0f);
    frame.moon = celestialDirection(m_clock.hour + 12.8f, 6.0f * XM_PI / 180.0f);
    const float sunHeight = frame.sun.y;
    const float daylight = smooth(-0.19f, 0.28f, sunHeight);
    const float twilight = smooth(-0.22f, -0.035f, sunHeight) * (1.0f - smooth(-0.02f, 0.24f, sunHeight));
    frame.sun.w = smooth(-0.022f, 0.31f, sunHeight);
    frame.moon.w = smooth(-0.045f, 0.25f, frame.moon.y) * (1.0f - daylight);
    frame.cycle = { daylight, twilight, m_clock.cloudSeconds, 1.0f };
    const float windAngle = m_clock.cloudSeconds * 0.0034f;
    frame.cloudWind = { std::cos(windAngle), std::sin(windAngle),
        m_cloudDensity && m_cloudSampler ? 1.0f : 0.0f, 0.0f };

    LIGHT light{};
    light.Enable = true;
    const auto& source = sunHeight > -0.025f ? frame.sun : frame.moon;
    light.Direction = { -source.x, -source.y, -source.z, 0.0f };
    light.Ambient = { 0.14f + daylight * 0.38f + twilight * 0.07f,
        0.18f + daylight * 0.38f + twilight * 0.027f, 0.26f + daylight * 0.33f, 1.0f };
    const float warmth = 1.0f - smooth(0.03f, 0.38f, sunHeight);
    const float moonlight = sunHeight > -0.025f ? 0.0f : frame.moon.w;
    light.Diffuse = { 0.79f * frame.sun.w + 0.09f * moonlight,
        (0.75f - 0.28f * warmth) * frame.sun.w + 0.12f * moonlight,
        (0.68f - 0.40f * warmth) * frame.sun.w + 0.20f * moonlight, 1.0f };
    Renderer::SetLight(light);

    auto* context = Renderer::GetDeviceContext();
    context->UpdateSubresource(m_frame.Get(), 0, nullptr, &frame, 0, 0);
    context->PSSetConstantBuffers(skySlot, 1, m_frame.GetAddressOf());
    context->PSSetShaderResources(cloudDensitySlot, 1, m_cloudDensity.GetAddressOf());
    context->PSSetSamplers(cloudSamplerSlot, 1, m_cloudSampler.GetAddressOf());
}

void SkyDome::Draw()
{
    if (!m_frame) return;
    auto* context = Renderer::GetDeviceContext();
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(m_vertex.Get(), nullptr, 0);
    context->PSSetShader(m_pixel.Get(), nullptr, 0);
    context->Draw(3, 0);
}
