#include "main.h"
#include "renderer.h"
#include "Wave.h"
#include "MeshField.h"

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>
#include <random>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr float eyeGroundTolerance = 0.005f;
    // The three Gerstner waves in WaveVS can move a vertex by up to 0.101 units in XZ.
    constexpr float waveReach = 0.11f;
    constexpr int riverMaskSize = 384;
    constexpr int gridSize = MeshField::GridSize;
    constexpr int terrainCells = gridSize - 1;

    struct WaterFrame
    {
        XMFLOAT4X4 inverseViewProjection;
        XMFLOAT4X4 viewProjection;
        XMFLOAT4 settings;
        XMFLOAT4 terrainGrid;
        XMFLOAT4 volume;
        XMFLOAT4 channel;
    };

    bool within(float value, float min, float max)
    {
        return value >= min && value <= max;
    }

    bool validPoint(const XMFLOAT2& point)
    {
        const float end = Wave::PondOrigin + Wave::PondSize;
        return within(point.x, Wave::PondOrigin, end) && within(point.y, Wave::PondOrigin, end);
    }

    std::array<int, 3> triangleVertices(int x, int z, int triangle)
    {
        const int first = z * gridSize + x;
        return { first + (triangle ? gridSize + 1 : 0), first + 1, first + gridSize };
    }

    bool terrainCell(const MeshField& terrain, float x, float z, int& col, int& row, int& triangle)
    {
        const auto grid = terrain.worldGrid();
        const float u = (x - grid.x) / grid.z, v = (z - grid.y) / grid.w;
        if (!within(u, 0, terrainCells) || !within(v, 0, terrainCells)) return false;
        col = std::min(terrainCells - 1, static_cast<int>(u));
        row = std::min(terrainCells - 1, static_cast<int>(v));
        triangle = (u - col) + (v - row) > 1.0f;
        return true;
    }

    MeshField::Heights worldHeights(const MeshField& terrain)
    {
        auto heights = terrain.heights();
        for (auto& height : heights)
            height = terrain.GetPosition().y + height * terrain.GetScale().y;
        return heights;
    }

    int submergedVertex(const MeshField& terrain, const MeshField::Heights& heights,
        const XMFLOAT2& seed, float level)
    {
        float ground;
        int x, z, triangle;
        if (!terrain.tryGetHeight(seed.x, seed.y, ground) || ground >= level
            || !terrainCell(terrain, seed.x, seed.y, x, z, triangle)) return -1;
        const auto vertices = triangleVertices(x, z, triangle);
        for (int vertex : vertices)
            if (heights[vertex] < level) return vertex;
        return -1;
    }

    using BasinVertices = std::bitset<gridSize * gridSize>;

    bool traceBasin(const MeshField::Heights& heights, int start, float level, BasinVertices& wet)
    {
        std::vector<int> queue{ start };
        wet.set(start);
        bool enclosed = true;
        // These six edges follow the same triangle split as MeshField::Draw.
        constexpr int dx[]{ -1, 1, 0, 0, -1, 1 };
        constexpr int dz[]{ 0, 0, -1, 1, 1, -1 };
        for (size_t i = 0; i < queue.size(); ++i)
        {
            const int x = queue[i] % gridSize, z = queue[i] / gridSize;
            if (x == 0 || z == 0 || x == terrainCells || z == terrainCells)
                enclosed = false;
            for (int edge = 0; edge < 6; ++edge)
            {
                const int col = x + dx[edge], row = z + dz[edge];
                if (col < 0 || row < 0 || col >= gridSize || row >= gridSize) continue;
                const int next = row * gridSize + col;
                if (wet.test(next) || heights[next] >= level) continue;
                wet.set(next);
                queue.push_back(next);
            }
        }
        return enclosed;
    }

    VERTEX_3D waterVertex(float x, float y, float z, float u, float v)
    {
        // Normal.x holds the minimum terrain clearance around this vertex for the water VS.
        return { { x, y, z }, { 0.70f, 1, 0 }, { 1, 1, 1, 1 }, { u, v } };
    }

    float terrainTop(const MeshField::Heights& heights, float x0, float z0, float x1, float z1)
    {
        float top = -INFINITY;
        for (int row = static_cast<int>(z0); row < terrainCells && row <= z1; ++row)
            for (int col = static_cast<int>(x0); col < terrainCells && col <= x1; ++col)
            {
                const float left = std::max(0.0f, x0 - col), right = std::min(1.0f, x1 - col);
                const float front = std::max(0.0f, z0 - row), back = std::min(1.0f, z1 - row);
                const int first = row * gridSize + col;
                const float b = heights[first + 1], c = heights[first + gridSize];
                const auto include = [&](float u, float v)
                {
                    const float a = heights[first], d = heights[first + gridSize + 1];
                    const float y = u + v <= 1.0f ? a + u * (b - a) + v * (c - a)
                        : d + (1.0f - u) * (c - d) + (1.0f - v) * (b - d);
                    top = std::max(top, y);
                };
                // Each terrain cell has two planar triangles. Extrema inside the rectangle occur at
                // its corners or along intersections with the diagonal shared by the triangles.
                for (float u : { left, right })
                {
                    include(u, front);
                    include(u, back);
                    if (within(1.0f - u, front, back)) include(u, 1.0f - u);
                }
                for (float v : { front, back })
                    if (within(1.0f - v, left, right)) include(1.0f - v, v);
            }
        return top;
    }

    void addSurfaceMesh(std::vector<UINT>& indices, std::vector<VERTEX_3D>& vertices,
        UINT first, int columns, int rows, const MeshField::Heights& heights, const XMFLOAT4& grid)
    {
        const float padX = waveReach / std::abs(grid.z), padZ = waveReach / std::abs(grid.w);
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < columns; ++col)
            {
                const UINT a = first + row * (columns + 1) + col;
                const UINT b = a + columns + 1;
                indices.insert(indices.end(), { a, a + 1, b, b, a + 1, b + 1 });

                float left = INFINITY, right = -INFINITY, front = INFINITY, back = -INFINITY;
                for (UINT corner : { a, a + 1, b, b + 1 })
                {
                    const auto& point = vertices[corner].Position;
                    const float x = (point.x - grid.x) / grid.z, z = (point.z - grid.y) / grid.w;
                    left = std::min(left, x);
                    right = std::max(right, x);
                    front = std::min(front, z);
                    back = std::max(back, z);
                }
                float clearance = 0.0f;
                if (left >= padX && right + padX <= terrainCells && front >= padZ && back + padZ <= terrainCells)
                    clearance = vertices[a].Position.y
                        - terrainTop(heights, left - padX, front - padZ, right + padX, back + padZ);
                // All four vertices of any quad touching a bank must stay flat so an interpolated
                // wave cannot dip into the shoreline between vertices.
                for (UINT corner : { a, a + 1, b, b + 1 })
                    vertices[corner].Normal.x = std::min(vertices[corner].Normal.x, clearance);
            }
    }

    float signedArea(const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& p)
    {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    }

    bool inTriangle(const XMFLOAT2& p, const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
    {
        if (std::abs(signedArea(a, b, c)) < 0.000001f) return false;
        const float ab = signedArea(a, b, p), bc = signedArea(b, c, p), ca = signedArea(c, a, p);
        return (ab >= 0 && bc >= 0 && ca >= 0) || (ab <= 0 && bc <= 0 && ca <= 0);
    }

    ComPtr<ID3D11ShaderResourceView> createWaterDetail(ID3D11Device& device)
    {
        constexpr int size = 64, levels = 7, layers = 2;
        std::array<std::vector<BYTE>, layers * levels> pixels;
        std::array<D3D11_SUBRESOURCE_DATA, layers * levels> data{};
        std::mt19937 random(2167);
        const auto pack = [](float value)
        {
            return static_cast<BYTE>(std::max(0.0f, std::min(value, 1.0f)) * 255.0f + 0.5f);
        };
        for (int layer = 0; layer < layers; ++layer)
        {
            // Integer modes create tileable gradients of a periodic height field.
            std::array<XMFLOAT4, 12> waves{};
            for (auto& wave : waves)
            {
                const float x = 1.0f + random() % 7, z = static_cast<int>(random() % 13) - 6.0f;
                wave = { x, z, static_cast<float>(random() % 4096) * (XM_2PI / 4096.0f),
                    0.24f / std::hypot(x, z) };
            }
            auto& base = pixels[layer * levels];
            base.resize(size * size * 4);
            for (int z = 0; z < size; ++z)
                for (int x = 0; x < size; ++x)
                {
                    float dx = 0.0f, dz = 0.0f, height = 0.0f;
                    for (const auto& wave : waves)
                    {
                        const float phase = (x * wave.x + z * wave.y) * (XM_2PI / size) + wave.z;
                        const float slope = wave.w * std::cos(phase);
                        dx += wave.x * slope;
                        dz += wave.y * slope;
                        height += std::sin(phase);
                    }
                    const int offset = (z * size + x) * 4;
                    base[offset] = pack(0.5f + 0.34f * dx);
                    base[offset + 1] = pack(0.5f + 0.34f * dz);
                    base[offset + 2] = pack(0.50f + 0.10f * height);
                    base[offset + 3] = 255;
                }
            int side = size;
            for (int level = 0; level < levels; ++level, side /= 2)
            {
                const int subresource = layer * levels + level;
                auto& mip = pixels[subresource];
                if (level > 0)
                {
                    const auto& previous = pixels[subresource - 1];
                    const int rowStride = side * 8;
                    mip.resize(side * side * 4);
                    for (int row = 0; row < side; ++row)
                        for (int col = 0; col < side; ++col)
                            for (int c = 0; c < 4; ++c)
                            {
                                const int start = row * rowStride * 2 + col * 8 + c;
                                mip[(row * side + col) * 4 + c] = static_cast<BYTE>((previous[start]
                                    + previous[start + 4] + previous[start + rowStride]
                                    + previous[start + rowStride + 4] + 2) / 4);
                            }
                }
                data[subresource] = { mip.data(), static_cast<UINT>(side * 4), 0 };
            }
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = size;
        desc.MipLevels = levels;
        desc.ArraySize = layers;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        if (SUCCEEDED(device.CreateTexture2D(&desc, data.data(), &texture)))
            device.CreateShaderResourceView(texture.Get(), nullptr, &view);
        return view;
    }
}

bool Wave::River::valid() const
{
    if (points.size() < 2 || points.size() > MaxRiverPoints
        || !within(width, 0.5f, 40.0f) || !within(height, -30.0f, 30.0f)
        || !within(speed, 0.0f, 12.0f)) return false;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (!validPoint(points[i])) return false;
        if (i > 0 && std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y) < 0.5f)
            return false;
    }
    return true;
}

XMFLOAT2 Wave::River::Section::across(float offset) const
{
    return { center.x - tangent.y * offset, center.y + tangent.x * offset };
}

std::vector<Wave::River::Section> Wave::River::sections() const
{
    if (!valid()) return {};
    std::vector<Section> result;
    float chordLength = 0.0f;
    for (size_t i = 1; i < points.size(); ++i)
        chordLength += std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
    const float targetStep = std::max(0.85f, chordLength / 1024.0f);
    const auto load = [](const XMFLOAT2& point) { return XMLoadFloat2(&point); };
    const auto spacing = [](FXMVECTOR a, FXMVECTOR b)
    {
        return std::sqrt(XMVectorGetX(XMVector2Length(a - b)));
    };
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        const auto b = load(points[i]), c = load(points[i + 1]);
        const auto a = i > 0 ? load(points[i - 1]) : b + (b - c);
        const auto d = i + 2 < points.size() ? load(points[i + 2]) : c + (c - b);
        const float ab = spacing(a, b), bc = spacing(b, c), cd = spacing(c, d);
        const auto start = ((b - a) / ab - (c - a) / (ab + bc) + (c - b) / bc) * bc;
        const auto end = ((c - b) / bc - (d - b) / (bc + cd) + (d - c) / cd) * bc;
        const int steps = std::max(3, static_cast<int>(std::ceil(bc * bc / targetStep)));
        for (int step = i == 0 ? 0 : 1; step <= steps; ++step)
        {
            const float t = static_cast<float>(step) / steps, t2 = t * t;
            Section section{};
            XMStoreFloat2(&section.center, XMVectorHermite(b, start, c, end, t));
            const auto derivative = (b - c) * (6.0f * t2 - 6.0f * t)
                + start * (3.0f * t2 - 4.0f * t + 1.0f) + end * (3.0f * t2 - 2.0f * t);
            const auto direction = XMVectorGetX(XMVector2LengthSq(derivative)) > 0.00001f ? derivative : c - b;
            XMStoreFloat2(&section.tangent, XMVector2Normalize(direction));
            if (!result.empty())
            {
                const auto& previous = result.back();
                section.distance = previous.distance + std::hypot(section.center.x - previous.center.x,
                    section.center.y - previous.center.y);
            }
            result.push_back(section);
        }
    }
    return result;
}

bool Wave::Layout::edited() const
{
    const Layout defaults;
    return pond.any() || !basins.empty() || !rivers.empty() || pondHeight != defaults.pondHeight
        || opacity != defaults.opacity || reflection != defaults.reflection || alphaBlend != defaults.alphaBlend;
}

bool Wave::Layout::paintPond(float x, float z, float radius, bool paint)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(x - PondOrigin - radius)));
    const int x1 = std::min(PondSize - 1, static_cast<int>(std::floor(x - PondOrigin + radius)));
    const int z0 = std::max(0, static_cast<int>(std::floor(z - PondOrigin - radius)));
    const int z1 = std::min(PondSize - 1, static_cast<int>(std::floor(z - PondOrigin + radius)));
    bool changed = false;
    for (int row = z0; row <= z1; ++row)
        for (int col = x0; col <= x1; ++col)
        {
            const float dx = PondOrigin + col + 0.5f - x;
            const float dz = PondOrigin + row + 0.5f - z;
            const size_t cell = row * PondSize + col;
            if (dx * dx + dz * dz > radius * radius || pond.test(cell) == paint) continue;
            pond.set(cell, paint);
            changed = true;
        }
    return changed;
}

bool Wave::Layout::editBasin(const MeshField& terrain, float x, float z, bool fill)
{
    const XMFLOAT2 seed{ x, z };
    if (fill && (!validPoint(seed) || basins.size() >= MaxBasins)) return false;
    const auto heights = worldHeights(terrain);
    const float level = pondHeight;
    const int start = submergedVertex(terrain, heights, seed, level);
    if (start < 0) return false;
    BasinVertices wet;
    const bool enclosed = traceBasin(heights, start, level, wet);
    const auto sameBasin = [&](const XMFLOAT2& existing)
    {
        const int vertex = submergedVertex(terrain, heights, existing, level);
        return vertex >= 0 && wet.test(vertex);
    };
    if (fill)
    {
        if (!enclosed || std::any_of(basins.begin(), basins.end(), sameBasin)) return false;
        basins.push_back(seed);
        return true;
    }
    const auto end = std::remove_if(basins.begin(), basins.end(), sameBasin);
    if (end == basins.end()) return false;
    basins.erase(end, basins.end());
    return true;
}

bool Wave::Layout::read(std::istream& input)
{
    Layout candidate;
    std::string signature;
    size_t count = 0;
    int version = 0, blend = 0;
    if (!(input >> signature >> version >> candidate.pondHeight >> candidate.opacity >> blend)
        || signature != "Water" || version < 1 || version > 4
        || !within(candidate.pondHeight, -30.0f, 30.0f)
        || !within(candidate.opacity, 0.0f, 1.0f) || (blend != 0 && blend != 1)) return false;
    if (version >= 2 && (!(input >> candidate.reflection) || !within(candidate.reflection, 0.0f, 1.0f)))
        return false;
    if (!(input >> count)
        || count > MaxRivers) return false;
    candidate.alphaBlend = blend != 0;
    candidate.rivers.resize(count);
    for (auto& river : candidate.rivers)
    {
        size_t points = 2;
        if (version >= 4 && (!(input >> points) || points < 2 || points > MaxRiverPoints))
            return false;
        river.points.resize(points);
        for (auto& point : river.points)
            if (!(input >> point.x >> point.y)) return false;
        if (!(input >> river.width >> river.height >> river.speed) || !river.valid())
            return false;
    }
    if (!(input >> count) || count > candidate.pond.size()) return false;
    for (size_t i = 0; i < count; ++i)
    {
        size_t cell = 0;
        if (!(input >> cell) || cell >= candidate.pond.size() || candidate.pond.test(cell)) return false;
        candidate.pond.set(cell);
    }
    if (version >= 3)
    {
        if (!(input >> count) || count > MaxBasins) return false;
        candidate.basins.resize(count);
        for (auto& seed : candidate.basins)
            if (!(input >> seed.x >> seed.y) || !validPoint(seed)) return false;
    }
    *this = std::move(candidate);
    return true;
}

void Wave::Layout::write(std::ostream& output) const
{
    const bool curved = std::any_of(rivers.begin(), rivers.end(),
        [](const River& river) { return river.points.size() > 2; });
    output << "Water " << (curved ? 4 : basins.empty() ? 2 : 3) << '\n'
        << pondHeight << ' ' << opacity << ' ' << alphaBlend << ' ' << reflection << '\n';
    output << rivers.size() << '\n';
    for (const auto& river : rivers)
    {
        if (curved) output << river.points.size() << ' ';
        for (const auto& point : river.points) output << point.x << ' ' << point.y << ' ';
        output << river.width << ' ' << river.height << ' ' << river.speed << '\n';
    }
    output << pond.count() << '\n';
    for (size_t i = 0; i < pond.size(); ++i)
        if (pond.test(i)) output << i << '\n';
    if (curved || !basins.empty())
    {
        output << basins.size() << '\n';
        for (const auto& seed : basins) output << seed.x << ' ' << seed.y << '\n';
    }
}

bool Wave::initialize()
{
    if (m_detailSampler) return true;
    auto* device = Renderer::GetDevice();
    if (!m_inputLayout)
    {
        Renderer::CreateVertexShader(m_vertexShader.ReleaseAndGetAddressOf(),
            m_inputLayout.ReleaseAndGetAddressOf(), "shader\\WaveVS.cso");
        Renderer::CreatePixelShader(m_pixelShader.ReleaseAndGetAddressOf(), "shader\\WavePS.cso");
    }
    if (!m_inputLayout || !m_vertexShader || !m_pixelShader) return false;

    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = PondSize;
    texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
    texture.Format = DXGI_FORMAT_R8_UNORM;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&texture, nullptr, m_mask.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(m_mask.Get(), nullptr, m_maskView.ReleaseAndGetAddressOf())))
        return false;

    texture.Width = texture.Height = gridSize;
    texture.Format = DXGI_FORMAT_R32_FLOAT;
    if (FAILED(device->CreateTexture2D(&texture, nullptr, m_terrainHeight.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(m_terrainHeight.Get(), nullptr, m_terrainHeightView.ReleaseAndGetAddressOf())))
        return false;
    texture.Width = texture.Height = terrainCells;
    texture.Format = DXGI_FORMAT_R8G8_UNORM;
    if (FAILED(device->CreateTexture2D(&texture, nullptr, m_basinMask.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(m_basinMask.Get(), nullptr, m_basinMaskView.ReleaseAndGetAddressOf())))
        return false;
    texture.Width = texture.Height = riverMaskSize;
    texture.Format = DXGI_FORMAT_R8_UNORM;
    if (FAILED(device->CreateTexture2D(&texture, nullptr, m_riverMask.ReleaseAndGetAddressOf()))
        || FAILED(device->CreateShaderResourceView(m_riverMask.Get(), nullptr, m_riverMaskView.ReleaseAndGetAddressOf())))
        return false;
    m_detailView = createWaterDetail(*device);
    if (!m_detailView) return false;

    D3D11_BUFFER_DESC frame{};
    frame.ByteWidth = sizeof(WaterFrame);
    frame.Usage = D3D11_USAGE_DEFAULT;
    frame.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&frame, nullptr, m_frameBuffer.ReleaseAndGetAddressOf()))) return false;

    if (auto* sceneDepth = Renderer::sceneDepth())
    {
        ComPtr<ID3D11Resource> resource;
        sceneDepth->GetResource(&resource);
        D3D11_DEPTH_STENCIL_VIEW_DESC description{};
        D3D11_SHADER_RESOURCE_VIEW_DESC source{};
        sceneDepth->GetDesc(&source);
        description.Format = source.Format == DXGI_FORMAT_R32_FLOAT ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D16_UNORM;
        description.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        description.Flags = D3D11_DSV_READ_ONLY_DEPTH;
        device->CreateDepthStencilView(resource.Get(), &description, m_readableDepthView.ReleaseAndGetAddressOf());
    }
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(device->CreateDepthStencilState(&depth, m_depthReadOnly.ReleaseAndGetAddressOf()))) return false;

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterizer, m_rasterizer.ReleaseAndGetAddressOf()))) return false;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampler, m_sampler.ReleaseAndGetAddressOf()))) return false;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    return SUCCEEDED(device->CreateSamplerState(&sampler, m_detailSampler.ReleaseAndGetAddressOf()));
}

bool Wave::rebuild(const Layout& settings, const MeshField& terrain)
{
    std::vector<VERTEX_3D> vertices;
    std::vector<UINT> indices;
    std::vector<Surface> surfaces;
    std::array<BYTE, PondSize * PondSize> mask{};
    std::vector<BYTE> basinTriangles(terrainCells * terrainCells * 2);
    auto* context = Renderer::GetDeviceContext();
    const auto heights = worldHeights(terrain);
    const auto grid = terrain.worldGrid();
    context->UpdateSubresource(m_terrainHeight.Get(), 0, nullptr, heights.data(), sizeof(float) * gridSize, 0);
    float left = INFINITY, front = INFINITY, right = -INFINITY, back = -INFINITY;
    const auto extend = [&](float x, float z)
    {
        left = std::min(left, x);
        right = std::max(right, x);
        front = std::min(front, z);
        back = std::max(back, z);
    };
    for (int z = 0; z < PondSize; ++z)
        for (int x = 0; x < PondSize; ++x)
        {
            const int index = z * PondSize + x;
            if (!settings.pond.test(index)) continue;
            mask[index] = 255;
            extend(PondOrigin + x - 0.5f, PondOrigin + z - 0.5f);
            extend(PondOrigin + x + 1.5f, PondOrigin + z + 1.5f);
        }
    context->UpdateSubresource(m_mask.Get(), 0, nullptr, mask.data(), PondSize, 0);

    if (!settings.basins.empty())
    {
        BasinVertices processed, filled;
        const float level = settings.pondHeight;
        for (const auto& seed : settings.basins)
        {
            const int start = submergedVertex(terrain, heights, seed, level);
            if (start < 0 || processed.test(start)) continue;
            BasinVertices wet;
            if (traceBasin(heights, start, level, wet)) filled |= wet;
            processed |= wet;
        }
        for (int z = 0; z < terrainCells; ++z)
            for (int x = 0; x < terrainCells; ++x)
            {
                for (int triangle = 0; triangle < 2; ++triangle)
                {
                    const auto corners = triangleVertices(x, z, triangle);
                    if (std::none_of(corners.begin(), corners.end(), [&](int vertex) { return filled.test(vertex); }))
                        continue;
                    basinTriangles[(z * terrainCells + x) * 2 + triangle] = 255;
                    extend(grid.x + x * grid.z, grid.y + z * grid.w);
                    extend(grid.x + (x + 1) * grid.z, grid.y + (z + 1) * grid.w);
                }
            }
    }
    context->UpdateSubresource(m_basinMask.Get(), 0, nullptr, basinTriangles.data(), terrainCells * 2, 0);

    if (left < right && front < back)
    {
        const int columns = static_cast<int>(std::ceil((right - left) / 0.9f));
        const int rows = static_cast<int>(std::ceil((back - front) / 0.9f));
        for (int row = 0; row <= rows; ++row)
            for (int col = 0; col <= columns; ++col)
                vertices.push_back(waterVertex(left + (right - left) * col / columns, settings.pondHeight,
                    front + (back - front) * row / rows, 0, 0));
        addSurfaceMesh(indices, vertices, 0, columns, rows, heights, grid);
        surfaces.push_back({ { (left + right) * 0.5f, settings.pondHeight,
            (front + back) * 0.5f }, 0, 0, 0, 0, static_cast<UINT>(indices.size()), true, {} });
    }
    for (const auto& river : settings.rivers)
    {
        auto sections = river.sections();
        if (sections.empty()) continue;
        const int columns = std::max(2, std::min(24, static_cast<int>(std::ceil(river.width / 0.9f))));
        const UINT firstVertex = static_cast<UINT>(vertices.size()), firstIndex = static_cast<UINT>(indices.size());
        XMFLOAT4 bounds{ INFINITY, INFINITY, -INFINITY, -INFINITY };
        for (const auto& section : sections)
        {
            for (int col = 0; col <= columns; ++col)
            {
                const float across = (static_cast<float>(col) / columns - 0.5f) * river.width;
                const auto point = section.across(across);
                auto vertex = waterVertex(point.x, river.height, point.y, across, section.distance);
                vertex.Diffuse = { section.tangent.x, 0, section.tangent.y, 1 };
                vertices.push_back(vertex);
                bounds.x = std::min(bounds.x, point.x);
                bounds.y = std::min(bounds.y, point.y);
                bounds.z = std::max(bounds.z, point.x);
                bounds.w = std::max(bounds.w, point.y);
            }
        }
        addSurfaceMesh(indices, vertices, firstVertex, columns, static_cast<int>(sections.size() - 1),
            heights, grid);
        const float length = sections.back().distance;
        surfaces.push_back({ { (bounds.x + bounds.z) * 0.5f, river.height, (bounds.y + bounds.w) * 0.5f },
            river.speed, river.width, length, firstIndex, static_cast<UINT>(indices.size()) - firstIndex,
            false, std::move(sections), bounds });
    }

    ComPtr<ID3D11Buffer> buffer, indexBuffer;
    if (!vertices.empty())
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(VERTEX_3D));
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = vertices.data();
        if (FAILED(Renderer::GetDevice()->CreateBuffer(&desc, &data, &buffer))) return false;
        desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(UINT));
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        data.pSysMem = indices.data();
        if (FAILED(Renderer::GetDevice()->CreateBuffer(&desc, &data, &indexBuffer))) return false;
    }
    m_vertices = std::move(buffer);
    m_indices = std::move(indexBuffer);
    m_surfaces = std::move(surfaces);
    m_basinTriangles = std::move(basinTriangles);
    m_terrain = &terrain;
    m_terrainRevision = terrain.revision();
    m_maskedRiver = UINT_MAX;
    m_dirty = false;
    return true;
}

const Wave::Surface* Wave::waterAtEye(const Layout& settings, const MeshField& terrain, const Vector3& eye) const
{
    float ground;
    if (!terrain.tryGetHeight(eye.x, eye.z, ground) || eye.y < ground - eyeGroundTolerance) return nullptr;
    const auto painted = [&]()
    {
        const float u = eye.x - PondOrigin - 0.5f, v = eye.z - PondOrigin - 0.5f;
        if (!within(u, -0.5f, PondSize - 0.5f) || !within(v, -0.5f, PondSize - 0.5f)) return false;
        const int x = static_cast<int>(std::floor(u)), z = static_cast<int>(std::floor(v));
        const auto sample = [&](int col, int row)
        {
            const int x = std::max(0, std::min(col, PondSize - 1));
            const int z = std::max(0, std::min(row, PondSize - 1));
            return settings.pond.test(z * PondSize + x) ? 1.0f : 0.0f;
        };
        const float top = (1 + x - u) * sample(x, z) + (u - x) * sample(x + 1, z);
        const float bottom = (1 + x - u) * sample(x, z + 1) + (u - x) * sample(x + 1, z + 1);
        return (1 + z - v) * top + (v - z) * bottom > (settings.alphaBlend ? 0.32f : 0.50f);
    };
    const Surface* result = nullptr;
    for (const auto& surface : m_surfaces)
    {
        if (surface.center.y <= std::max(eye.y, ground)
            || (result && result->center.y >= surface.center.y)) continue;
        if (surface.pond)
        {
            int x, z, triangle;
            const bool filled = terrainCell(terrain, eye.x, eye.z, x, z, triangle)
                && m_basinTriangles[(z * terrainCells + x) * 2 + triangle];
            if (!filled && !painted()) continue;
        }
        else
        {
            const auto& bounds = surface.bounds;
            if (!within(eye.x, bounds.x, bounds.z) || !within(eye.z, bounds.y, bounds.w)) continue;
            const float half = surface.width * 0.5f;
            const XMFLOAT2 point{ eye.x, eye.z };
            bool inside = false;
            for (size_t i = 1; i < surface.sections.size(); ++i)
            {
                const auto& a = surface.sections[i - 1];
                const auto& b = surface.sections[i];
                const auto left = a.across(-half), right = a.across(half);
                const auto nextLeft = b.across(-half), nextRight = b.across(half);
                inside = inTriangle(point, left, right, nextLeft) || inTriangle(point, right, nextRight, nextLeft);
                if (inside) break;
            }
            if (!inside) continue;
        }
        result = &surface;
    }
    return result;
}

void Wave::updateRiverMask(const Surface& surface)
{
    if (m_maskedRiver == surface.firstIndex) return;
    const auto bounds = surface.bounds;
    std::vector<BYTE> pixels(riverMaskSize * riverMaskSize);
    const auto toPixel = [&](const XMFLOAT2& point)
    {
        return XMFLOAT2{ (point.x - bounds.x) / (bounds.z - bounds.x) * riverMaskSize - 0.5f,
            (point.y - bounds.y) / (bounds.w - bounds.y) * riverMaskSize - 0.5f };
    };
    const auto fillTriangle = [&](const XMFLOAT2& a, const XMFLOAT2& b, const XMFLOAT2& c)
    {
        const XMFLOAT2 points[]{ toPixel(a), toPixel(b), toPixel(c) };
        const int top = std::max(0, static_cast<int>(std::ceil(std::min({ points[0].y, points[1].y, points[2].y }))));
        const int bottom = std::min(riverMaskSize - 1,
            static_cast<int>(std::floor(std::max({ points[0].y, points[1].y, points[2].y }))));
        for (int row = top; row <= bottom; ++row)
        {
            float left = INFINITY, right = -INFINITY;
            for (int edge = 0; edge < 3; ++edge)
            {
                const auto p = points[edge], q = points[(edge + 1) % 3];
                if (p.y == q.y || !within(static_cast<float>(row), std::min(p.y, q.y), std::max(p.y, q.y))) continue;
                const float x = p.x + (q.x - p.x) * (row - p.y) / (q.y - p.y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
            if (left > right) continue;
            const int first = std::max(0, static_cast<int>(std::ceil(left)));
            const int last = std::min(riverMaskSize - 1, static_cast<int>(std::floor(right)));
            if (first <= last)
                std::fill(pixels.begin() + row * riverMaskSize + first,
                    pixels.begin() + row * riverMaskSize + last + 1, 255);
        }
    };
    const float half = surface.width * 0.5f;
    for (size_t i = 1; i < surface.sections.size(); ++i)
    {
        const auto& a = surface.sections[i - 1];
        const auto& b = surface.sections[i];
        const auto left = a.across(-half), right = a.across(half);
        const auto nextLeft = b.across(-half), nextRight = b.across(half);
        fillTriangle(left, right, nextLeft);
        fillTriangle(right, nextRight, nextLeft);
    }
    Renderer::GetDeviceContext()->UpdateSubresource(m_riverMask.Get(), 0, nullptr, pixels.data(), riverMaskSize, 0);
    m_maskedRiver = surface.firstIndex;
}

bool Wave::captureSceneColor(ID3D11RenderTargetView& target)
{
    ComPtr<ID3D11Resource> source;
    target.GetResource(&source);
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(source.As(&texture))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC cached{};
    if (m_sceneColor) m_sceneColor->GetDesc(&cached);
    if (!m_sceneColorView || cached.Format != desc.Format || cached.Width != desc.Width
        || cached.Height != desc.Height || cached.SampleDesc.Count != desc.SampleDesc.Count
        || cached.SampleDesc.Quality != desc.SampleDesc.Quality)
    {
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = desc.MiscFlags = 0;
        auto* device = Renderer::GetDevice();
        ComPtr<ID3D11Texture2D> replacement;
        ComPtr<ID3D11ShaderResourceView> replacementView;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &replacement))
            || FAILED(device->CreateShaderResourceView(replacement.Get(), nullptr, &replacementView)))
            return false;
        m_sceneColor = replacement;
        m_sceneColorView = replacementView;
    }
    Renderer::GetDeviceContext()->CopyResource(m_sceneColor.Get(), source.Get());
    return true;
}

void Wave::draw(const Layout& settings, const MeshField& terrain, const Vector3& eye, FXMMATRIX viewProjection, float dt,
    ID3D11DepthStencilView* readOnlyDepth, ID3D11ShaderResourceView* depthImage)
{
    if ((!settings.pond.any() && settings.basins.empty() && settings.rivers.empty())
        || (settings.alphaBlend && settings.opacity <= 0.0f))
        return;
    if (!initialize()) return;
    if ((m_dirty || m_terrain != &terrain || m_terrainRevision != terrain.revision()) && !rebuild(settings, terrain))
        return;
    if (!m_vertices) return;
    m_time += dt;

    auto* context = Renderer::GetDeviceContext();
    ComPtr<ID3D11DepthStencilState> previousDepth;
    ComPtr<ID3D11BlendState> previousBlend;
    ComPtr<ID3D11RasterizerState> previousRasterizer;
    ComPtr<ID3D11SamplerState> previousSampler;
    ComPtr<ID3D11SamplerState> previousDetailSampler;
    ComPtr<ID3D11ShaderResourceView> previousMask;
    ComPtr<ID3D11ShaderResourceView> previousSceneDepth;
    ComPtr<ID3D11ShaderResourceView> previousSceneColor;
    ComPtr<ID3D11ShaderResourceView> previousTerrain;
    ComPtr<ID3D11ShaderResourceView> previousBasins;
    ComPtr<ID3D11ShaderResourceView> previousDetail;
    ComPtr<ID3D11Buffer> previousFrame;
    ComPtr<ID3D11Buffer> previousIndices;
    ComPtr<ID3D11RenderTargetView> currentTarget;
    ComPtr<ID3D11DepthStencilView> previousDepthView;
    UINT reference = 0, sampleMask = 0;
    UINT previousIndexOffset = 0;
    DXGI_FORMAT previousIndexFormat = DXGI_FORMAT_UNKNOWN;
    float blendFactor[4]{};
    context->OMGetDepthStencilState(&previousDepth, &reference);
    context->OMGetBlendState(&previousBlend, blendFactor, &sampleMask);
    context->RSGetState(&previousRasterizer);
    context->PSGetSamplers(1, 1, &previousSampler);
    context->PSGetSamplers(3, 1, &previousDetailSampler);
    context->PSGetShaderResources(1, 1, &previousMask);
    context->PSGetShaderResources(2, 1, &previousSceneDepth);
    context->PSGetShaderResources(4, 1, &previousSceneColor);
    context->PSGetShaderResources(5, 1, &previousTerrain);
    context->PSGetShaderResources(6, 1, &previousBasins);
    context->PSGetShaderResources(7, 1, &previousDetail);
    context->PSGetConstantBuffers(7, 1, &previousFrame);
    context->IAGetIndexBuffer(&previousIndices, &previousIndexFormat, &previousIndexOffset);

    context->OMSetDepthStencilState(m_depthReadOnly.Get(), 0);
    Renderer::SetAddBlendEnable(false);
    context->RSSetState(m_rasterizer.Get());
    context->PSSetSamplers(1, 1, m_sampler.GetAddressOf());
    context->PSSetSamplers(3, 1, m_detailSampler.GetAddressOf());
    context->PSSetShaderResources(1, 1, m_maskView.GetAddressOf());
    context->OMGetRenderTargets(1, &currentTarget, &previousDepthView);
    const bool hasSceneColor = (settings.alphaBlend || settings.reflection > 0.0f)
        && currentTarget && captureSceneColor(*currentTarget.Get());
    // 撮影時はウィンドウ用ではなく、撮影先と同じ解像度の深度を参照する。
    if (!readOnlyDepth)
    {
        readOnlyDepth = m_readableDepthView.Get();
        depthImage = Renderer::sceneDepth();
    }
    if (readOnlyDepth)
    {
        context->OMSetRenderTargets(1, currentTarget.GetAddressOf(), readOnlyDepth);
    }
    ID3D11ShaderResourceView* const depth = readOnlyDepth ? depthImage : nullptr;
    context->PSSetShaderResources(2, 1, &depth);
    ID3D11ShaderResourceView* const sceneColor = hasSceneColor ? m_sceneColorView.Get() : nullptr;
    context->PSSetShaderResources(4, 1, &sceneColor);
    context->PSSetShaderResources(5, 1, m_terrainHeightView.GetAddressOf());
    context->PSSetShaderResources(6, 1, m_basinMaskView.GetAddressOf());
    context->PSSetShaderResources(7, 1, m_detailView.GetAddressOf());
    WaterFrame frame{};
    XMStoreFloat4x4(&frame.inverseViewProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, viewProjection)));
    XMStoreFloat4x4(&frame.viewProjection, XMMatrixTranspose(viewProjection));
    frame.settings = { settings.reflection, depth ? 1.0f : 0.0f, hasSceneColor ? 1.0f : 0.0f, 0.0f };
    frame.terrainGrid = terrain.worldGrid();
    frame.volume = { 0, 0, PondOrigin, PondSize };
    context->UpdateSubresource(m_frameBuffer.Get(), 0, nullptr, &frame, 0, 0);
    context->PSSetConstantBuffers(7, 1, m_frameBuffer.GetAddressOf());
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT stride = sizeof(VERTEX_3D), offset = 0;
    context->IASetVertexBuffers(0, 1, m_vertices.GetAddressOf(), &stride, &offset);
    context->IASetIndexBuffer(m_indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    Renderer::SetWorldMatrix(XMMatrixIdentity());

    const auto distance = [&eye](const Surface& surface)
    {
        const float x = surface.center.x - eye.x, y = surface.center.y - eye.y, z = surface.center.z - eye.z;
        return x * x + y * y + z * z;
    };
    std::stable_sort(m_surfaces.begin(), m_surfaces.end(), [&](const Surface& a, const Surface& b)
    {
        return distance(a) > distance(b);
    });
    for (const auto& surface : m_surfaces)
    {
        MATERIAL material{};
        material.Diffuse.w = settings.opacity;
        material.Specular.x = surface.center.y;
        material.Emission = { m_time, surface.speed, surface.width, surface.length };
        material.TextureEnable = surface.pond;
        material.AlphaBlend = settings.alphaBlend;
        Renderer::SetMaterial(material);
        context->DrawIndexed(surface.indexCount, surface.firstIndex, 0);
    }

    const auto* volume = depth && currentTarget ? waterAtEye(settings, terrain, eye) : nullptr;
    if (volume)
    {
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(4, 1, &empty);
        if (captureSceneColor(*currentTarget.Get()))
        {
            Renderer::SetDepthEnable(false);
            context->PSSetShaderResources(4, 1, m_sceneColorView.GetAddressOf());
            frame.volume.x = volume->center.y;
            frame.volume.y = volume->pond ? 1.0f : 0.0f;
            if (!volume->pond)
            {
                updateRiverMask(*volume);
                const auto bounds = volume->bounds;
                frame.channel = { bounds.x, bounds.y, 1.0f / (bounds.z - bounds.x), 1.0f / (bounds.w - bounds.y) };
                context->PSSetShaderResources(1, 1, m_riverMaskView.GetAddressOf());
            }
            context->UpdateSubresource(m_frameBuffer.Get(), 0, nullptr, &frame, 0, 0);
            MATERIAL material{};
            material.Shininess = 1.0f;
            material.Diffuse.w = settings.alphaBlend ? settings.opacity : 1.0f;
            material.Emission.x = m_time;
            Renderer::SetMaterial(material);
            context->Draw(3, 0);
        }
    }

    context->PSSetShaderResources(1, 1, previousMask.GetAddressOf());
    context->PSSetShaderResources(2, 1, previousSceneDepth.GetAddressOf());
    context->PSSetShaderResources(4, 1, previousSceneColor.GetAddressOf());
    context->PSSetShaderResources(5, 1, previousTerrain.GetAddressOf());
    context->PSSetShaderResources(6, 1, previousBasins.GetAddressOf());
    context->PSSetShaderResources(7, 1, previousDetail.GetAddressOf());
    context->PSSetConstantBuffers(7, 1, previousFrame.GetAddressOf());
    context->IASetIndexBuffer(previousIndices.Get(), previousIndexFormat, previousIndexOffset);
    if (readOnlyDepth)
        context->OMSetRenderTargets(1, currentTarget.GetAddressOf(), previousDepthView.Get());
    context->PSSetSamplers(1, 1, previousSampler.GetAddressOf());
    context->PSSetSamplers(3, 1, previousDetailSampler.GetAddressOf());
    context->RSSetState(previousRasterizer.Get());
    context->OMSetDepthStencilState(previousDepth.Get(), reference);
    context->OMSetBlendState(previousBlend.Get(), blendFactor, sampleMask);
}

void Wave::reset()
{
    *this = Wave{};
}
