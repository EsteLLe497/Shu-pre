#pragma once

#include <array>
#include <bitset>
#include <climits>
#include <iosfwd>
#include <vector>
#include <wrl/client.h>

class MeshField;

class Wave
{
public:
    static constexpr int PondSize = 200;
    static constexpr float PondOrigin = -100.0f;
    static constexpr size_t MaxRivers = 64;
    static constexpr size_t MaxBasins = 64;
    static constexpr size_t MaxRiverPoints = 32;

    struct River
    {
        struct Section
        {
            XMFLOAT2 center;
            XMFLOAT2 tangent;
            float distance;
            XMFLOAT2 across(float offset) const;
        };

        std::vector<XMFLOAT2> points;
        float width = 5.0f;
        float height = 0.25f;
        float speed = 1.5f;

        bool valid() const;
        std::vector<Section> sections() const;
    };

    struct Layout
    {
        std::bitset<PondSize * PondSize> pond;
        std::vector<XMFLOAT2> basins;
        std::vector<River> rivers;
        float pondHeight = 0.25f;
        float opacity = 0.65f;
        float reflection = 0.85f;
        bool alphaBlend = true;

        bool edited() const;
        bool paintPond(float x, float z, float radius, bool paint);
        bool editBasin(const MeshField& terrain, float x, float z, bool fill);
        bool read(std::istream& input);
        void write(std::ostream& output) const;
    };

    void invalidate() { m_dirty = true; }
    void draw(const Layout& settings, const MeshField& terrain, const Vector3& eye, FXMMATRIX viewProjection, float dt);
    void reset();

private:
    struct Surface
    {
        XMFLOAT3 center;
        float speed;
        float width;
        float length;
        UINT firstIndex;
        UINT indexCount;
        bool pond;
        std::vector<River::Section> sections;
        XMFLOAT4 bounds{};
    };

    bool initialize();
    bool rebuild(const Layout& settings, const MeshField& terrain);
    const Surface* waterAtEye(const Layout& settings, const MeshField& terrain, const Vector3& eye) const;
    bool captureSceneColor(ID3D11RenderTargetView& target);
    void updateRiverMask(const Surface& surface);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertices;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_indices;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_mask;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_maskView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_terrainHeight;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_terrainHeightView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_basinMask;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_basinMaskView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_riverMask;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_riverMaskView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_detailView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_skyView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_sceneColor;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_sceneColorView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_readableDepthView;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_frameBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthReadOnly;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_skySampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_detailSampler;
    std::vector<Surface> m_surfaces;
    std::vector<unsigned char> m_basinTriangles;
    const MeshField* m_terrain = nullptr;
    unsigned int m_terrainRevision = 0;
    UINT m_maskedRiver = UINT_MAX;
    float m_time = 0.0f;
    bool m_dirty = true;
};
