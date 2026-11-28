#pragma once

#include "GameObject.h"
#include <wrl/client.h>

class SkyDome final : public GameObject
{
public:
    struct Clock
    {
        float hour = 9.5f;
        float dayMinutes = 12.0f;
        float cloudSeconds = 0.0f;
        bool runInEditor = false;
    };

    void Init() override;
    void Uninit() override;
    void Draw() override;
    void advance(float seconds, bool editing);
    void bindFrame() const;
    Clock& clock() { return m_clock; }

private:
    Clock m_clock;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_frame;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertex;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixel;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cloudDensity;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_cloudSampler;
};

