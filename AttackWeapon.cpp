#include "main.h"
#include "renderer.h"
#include "AttackWeapon.h"
#include <DirectXCollision.h>

namespace
{
    constexpr unsigned int trailSegments = 18;
    constexpr unsigned int maxParticles = 128;

    struct EndConstants
    {
        XMFLOAT4 color;
        XMFLOAT4 params;
        XMFLOAT4 bladeAxis;
    };

    float random01(unsigned int seed)
    {
        seed ^= seed >> 16;
        seed *= 0x7feb352du;
        seed ^= seed >> 15;
        seed *= 0x846ca68bu;
        seed ^= seed >> 16;
        return static_cast<float>(seed & 0xffff) / 65535.0f;
    }

    XMVECTOR loadPoint(const std::array<float, 3>& point)
    {
        return XMVectorSet(point[0], point[1], point[2], 1.0f);
    }
}

bool AttackWeapon::load(const std::string& path)
{
    if (path.empty())
        return false;
    if (m_model && path == m_path)
        return true;

    auto model = std::make_unique<AnimationModel>(&m_anchor);
    if (!model->tryLoad(path.c_str(), true))
        return false;

    if (m_model)
        m_model->Uninit();
    m_model = std::move(model);
    m_path = path;
    return true;
}

void AttackWeapon::clear()
{
    if (m_model)
        m_model->Uninit();
    m_model.reset();
    m_path.clear();
    m_endConstants.Reset();
    m_endVS.Reset();
    m_endPS.Reset();
    m_endLayout.Reset();
    m_trailVertices.Reset();
    m_trailDepth.Reset();
    m_trailRasterizer.Reset();
}

XMMATRIX AttackWeapon::poseMatrix(const WeaponTrack& track, float time)
{
    const auto pose = track.sample(time);
    return XMMatrixScaling(track.scale, track.scale, track.scale)
        * XMMatrixTranslation(-track.pivot[0], -track.pivot[1], -track.pivot[2])
        * XMMatrixRotationRollPitchYaw(XMConvertToRadians(pose.angles[0]),
            XMConvertToRadians(pose.angles[1]), XMConvertToRadians(pose.angles[2]))
        * XMMatrixTranslation(pose.position[0], pose.position[1], pose.position[2]);
}

Vector3 AttackWeapon::effectPosition(const WeaponTrack& track, float time, FXMMATRIX anchor)
{
    const auto tip = XMVector3TransformCoord(loadPoint(track.trailTip), poseMatrix(track, time) * anchor);
    return { XMVectorGetX(tip), XMVectorGetY(tip), XMVectorGetZ(tip) };
}

bool AttackWeapon::hitTest(const WeaponTrack& track, float time, FXMVECTOR origin, FXMVECTOR direction) const
{
    if (!m_model || track.model != m_path || !track.visible(time))
        return false;

    const auto inverse = XMMatrixInverse(nullptr, poseMatrix(track, time));
    const auto rayOrigin = XMVector3TransformCoord(origin, inverse);
    const auto rayDirection = XMVector3Normalize(XMVector3TransformNormal(direction, inverse));
    const auto extents = m_model->staticHalfExtents();
    const BoundingBox bounds({ 0, 0, 0 },
        { std::max(extents.x, 0.06f), std::max(extents.y, 0.06f), std::max(extents.z, 0.06f) });
    float distance = 0.0f;
    return bounds.Intersects(rayOrigin, rayDirection, distance);
}

void AttackWeapon::draw(const WeaponTrack& track, float time, FXMMATRIX anchor, const XMMATRIX* endAnchor)
{
    if (!m_model || track.model != m_path || !track.enabled)
        return;

    if (track.visible(time))
        m_model->drawAt(poseMatrix(track, time) * anchor);
    if (track.trail && time > track.appear && time < track.disappear + track.trailLength)
        drawTrail(track, time, anchor);
    if (track.dissolve && time > track.disappear && time < track.visualEnd())
        drawDissolve(track, time - track.disappear, poseMatrix(track, track.disappear) * (endAnchor ? *endAnchor : anchor));
}

bool AttackWeapon::prepareOverlay()
{
    auto* device = Renderer::GetDevice();
    if (!m_trailVertices)
    {
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(VERTEX_3D) * maxParticles * 6;
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, m_trailVertices.GetAddressOf())))
            return false;
    }
    if (!m_trailDepth)
    {
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(device->CreateDepthStencilState(&depth, m_trailDepth.GetAddressOf())))
            return false;
    }
    if (!m_trailRasterizer)
    {
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizer, m_trailRasterizer.GetAddressOf())))
            return false;
    }

    return true;
}

void AttackWeapon::drawTrail(const WeaponTrack& track, float time, FXMMATRIX anchor)
{
    if (!prepareOverlay())
        return;

    const float start = std::max(track.appear, time - track.trailLength);
    const float end = std::min(time, track.disappear);
    auto* context = Renderer::GetDeviceContext();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_trailVertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    auto* vertices = static_cast<VERTEX_3D*>(mapped.pData);
    for (unsigned int i = 0; i <= trailSegments; ++i)
    {
        const float sampleTime = start + (end - start) * static_cast<float>(i) / trailSegments;
        const auto transform = poseMatrix(track, sampleTime) * anchor;
        const float alpha = 0.65f * std::max(0.0f, 1.0f - (time - sampleTime) / track.trailLength);
        vertices[2 * i] = { {}, { 0, 1, 0 }, { track.trailColor[0], track.trailColor[1], track.trailColor[2], alpha }, {} };
        vertices[2 * i + 1] = vertices[2 * i];
        XMStoreFloat3(&vertices[2 * i].Position, XMVector3TransformCoord(loadPoint(track.trailBase), transform));
        XMStoreFloat3(&vertices[2 * i + 1].Position, XMVector3TransformCoord(loadPoint(track.trailTip), transform));
    }
    context->Unmap(m_trailVertices.Get(), 0);

    Renderer::SetWorldMatrix(XMMatrixIdentity());
    MATERIAL material{};
    material.Diffuse = { 1, 1, 1, 1 };
    material.AlphaBlend = TRUE;
    material.AlphaCutoff = 1.0f / 255.0f;
    Renderer::SetMaterial(material);
    drawOverlay(2 * (trailSegments + 1), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
}

void AttackWeapon::drawOverlay(unsigned int count, D3D11_PRIMITIVE_TOPOLOGY topology)
{
    auto* context = Renderer::GetDeviceContext();
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11BlendState> blend;
    UINT stencil = 0, mask = 0;
    float blendFactor[4]{};
    context->RSGetState(rasterizer.GetAddressOf());
    context->OMGetDepthStencilState(depth.GetAddressOf(), &stencil);
    context->OMGetBlendState(blend.GetAddressOf(), blendFactor, &mask);
    context->RSSetState(m_trailRasterizer.Get());
    context->OMSetDepthStencilState(m_trailDepth.Get(), 0);
    Renderer::SetAddBlendEnable(true);
    constexpr UINT stride = sizeof(VERTEX_3D), offset = 0;
    context->IASetVertexBuffers(0, 1, m_trailVertices.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(topology);
    context->Draw(count, 0);
    context->OMSetBlendState(blend.Get(), blendFactor, mask);
    context->OMSetDepthStencilState(depth.Get(), stencil);
    context->RSSetState(rasterizer.Get());
}

void AttackWeapon::drawDissolve(const WeaponTrack& track, float age, FXMMATRIX world)
{
    auto* device = Renderer::GetDevice();
    auto* context = Renderer::GetDeviceContext();
    if (!m_endVS || !m_endPS || !m_endLayout)
    {
        Renderer::CreateVertexShader(m_endVS.ReleaseAndGetAddressOf(), m_endLayout.ReleaseAndGetAddressOf(),
            "shader/weaponEndVS.cso");
        Renderer::CreatePixelShader(m_endPS.ReleaseAndGetAddressOf(), "shader/weaponEndPS.cso");
        if (!m_endVS || !m_endPS || !m_endLayout)
            return;
    }
    if (!m_endConstants)
    {
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(EndConstants);
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, m_endConstants.GetAddressOf())))
            return;
    }

    const auto extent = m_model->staticHalfExtents();
    const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const XMFLOAT4 bladeAxis(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f, 0.0f);
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11VertexShader> oldVS;
    ComPtr<ID3D11PixelShader> oldPS;
    ComPtr<ID3D11InputLayout> oldLayout;
    ComPtr<ID3D11Buffer> oldVSConstants, oldPSConstants;
    context->VSGetShader(oldVS.GetAddressOf(), nullptr, nullptr);
    context->PSGetShader(oldPS.GetAddressOf(), nullptr, nullptr);
    context->IAGetInputLayout(oldLayout.GetAddressOf());
    context->VSGetConstantBuffers(7, 1, oldVSConstants.GetAddressOf());
    context->PSGetConstantBuffers(7, 1, oldPSConstants.GetAddressOf());
    context->VSSetShader(m_endVS.Get(), nullptr, 0);
    context->PSSetShader(m_endPS.Get(), nullptr, 0);
    context->IASetInputLayout(m_endLayout.Get());
    context->VSSetConstantBuffers(7, 1, m_endConstants.GetAddressOf());
    context->PSSetConstantBuffers(7, 1, m_endConstants.GetAddressOf());

    if (age < track.dissolveDuration)
    {
        const float t = age / track.dissolveDuration;
        const EndConstants constants{
            { track.dissolveColor[0], track.dissolveColor[1], track.dissolveColor[2], 1 },
            { t, track.dissolveGlow, 0, 0 }, bladeAxis
        };
        context->UpdateSubresource(m_endConstants.Get(), 0, nullptr, &constants, 0, 0);
        m_model->drawAt(world);
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (track.dissolveParticles && prepareOverlay()
        && SUCCEEDED(context->Map(m_trailVertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        auto* vertices = static_cast<VERTEX_3D*>(mapped.pData);
        unsigned int count = 0;
        constexpr XMFLOAT2 corners[] = { {-1,-1}, {-1,1}, {1,1}, {-1,-1}, {1,1}, {1,-1} };
        const float bounds[] = { extent.x, extent.y, extent.z };
        for (int i = 0; i < track.particleCount; ++i)
        {
            const unsigned int seed = 11 + 13 * i;
            float point[3];
            for (int j = 0; j < 3; ++j)
                point[j] = bounds[j] * (2.0f * random01(seed + j) - 1.0f);
            const float birth = track.dissolveDuration * (1.48f * fabsf(point[axis]) + 0.26f * random01(seed + 3));
            const float life = 0.17f + 0.21f * random01(seed + 4);
            const float elapsed = age - birth;
            if (elapsed < 0.0f || elapsed >= life)
                continue;

            const auto position = XMVector3TransformCoord(XMVectorSet(point[0], point[1], point[2], 1), world);
            const auto velocity = XMVectorSet(random01(seed + 5) - 0.5f, 0.18f + 0.42f * random01(seed + 6),
                random01(seed + 7) - 0.5f, 0);
            XMFLOAT3 center;
            XMStoreFloat3(&center, position + velocity * (elapsed * (1.8f - elapsed)));
            const float fade = 1.0f - elapsed / life;
            const float radius = (0.006f + 0.016f * powf(random01(seed + 8), 3.0f)) * (0.5f + 0.5f * fade);
            const float opacity = fade * fade * std::min(1.0f, elapsed * 100.0f);
            for (const auto& corner : corners)
                vertices[count++] = { center, { radius, 0, 0 }, { 1, 1, 1, opacity }, corner };
        }
        context->Unmap(m_trailVertices.Get(), 0);
        if (count)
        {
            const EndConstants constants{
                { track.particleColor[0], track.particleColor[1], track.particleColor[2], 1 },
                { 0, 0, 1, 0 }, bladeAxis
            };
            context->UpdateSubresource(m_endConstants.Get(), 0, nullptr, &constants, 0, 0);
            Renderer::SetWorldMatrix(XMMatrixIdentity());
            drawOverlay(count, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }
    }

    context->VSSetShader(oldVS.Get(), nullptr, 0);
    context->PSSetShader(oldPS.Get(), nullptr, 0);
    context->IASetInputLayout(oldLayout.Get());
    context->VSSetConstantBuffers(7, 1, oldVSConstants.GetAddressOf());
    context->PSSetConstantBuffers(7, 1, oldPSConstants.GetAddressOf());
}
