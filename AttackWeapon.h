#pragma once

#include <memory>
#include <wrl/client.h>
#include "ActionSequence.h"
#include "GameObject.h"
#include "animationModel.h"

class AttackWeapon
{
public:
    bool load(const std::string& path);
    void clear();
    void draw(const WeaponTrack& track, float time, FXMMATRIX anchor, const XMMATRIX* endAnchor = nullptr);
    bool hitTest(const WeaponTrack& track, float time, FXMVECTOR origin, FXMVECTOR direction) const;
    bool ready() const { return m_model != nullptr; }
    unsigned int missingTextures() const { return m_model ? m_model->missingTextures() : 0; }

    static Vector3 effectPosition(const WeaponTrack& track, float time, FXMMATRIX anchor);

private:
    static XMMATRIX poseMatrix(const WeaponTrack& track, float time);
    void drawDissolve(const WeaponTrack& track, float age, FXMMATRIX world);
    bool prepareOverlay();
    void drawOverlay(unsigned int count, D3D11_PRIMITIVE_TOPOLOGY topology);
    void drawTrail(const WeaponTrack& track, float time, FXMMATRIX anchor);

    std::string m_path;
    GameObject m_anchor;
    std::unique_ptr<AnimationModel> m_model;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_trailVertices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_endConstants;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_endVS;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_endPS;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_endLayout;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_trailDepth;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_trailRasterizer;
};
