#include "main.h"
#include "renderer.h"
#include "Scenery.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <wrl/client.h>

namespace
{
    using Microsoft::WRL::ComPtr;
    std::unordered_map<std::string, std::weak_ptr<Scenery::Model>> models;
    std::optional<BoundingFrustum> frustum;
    std::vector<Scenery::Model*> batches;
    struct Deferred
    {
        Scenery::Model* model;
        XMFLOAT4X4 world;
    };
    std::vector<Deferred> deferred;
    constexpr UINT instancesPerUpload = 2048;
    float windTime = 0.0f;
    constexpr float meadowDetailDistance = 8.0f;

    struct Pipeline
    {
        ComPtr<ID3D11InputLayout> layout, instancedLayout;
        ComPtr<ID3D11VertexShader> vertex, instancedVertex;
        ComPtr<ID3D11PixelShader> pixel;
        ComPtr<ID3D11Buffer> instanceBuffer;
        ComPtr<ID3D11Buffer> wind;
    };
    Pipeline pipeline;

    void bind(bool instanced)
    {
        auto* context = Renderer::GetDeviceContext();
        context->IASetInputLayout(instanced ? pipeline.instancedLayout.Get() : pipeline.layout.Get());
        context->VSSetShader(instanced ? pipeline.instancedVertex.Get() : pipeline.vertex.Get(), nullptr, 0);
        context->PSSetShader(pipeline.pixel.Get(), nullptr, 0);
        context->VSSetConstantBuffers(12, 1, pipeline.wind.GetAddressOf());
    }

    bool initialize()
    {
        if (pipeline.vertex && pipeline.layout && pipeline.pixel) return true;
        Renderer::CreateVertexShader(pipeline.vertex.ReleaseAndGetAddressOf(),
            pipeline.layout.ReleaseAndGetAddressOf(), "shader\\litTextureVS.cso");
        Renderer::CreatePixelShader(pipeline.pixel.ReleaseAndGetAddressOf(), "shader\\litTexturePS.cso");
        if (!pipeline.vertex || !pipeline.layout || !pipeline.pixel) return false;

        D3D11_BUFFER_DESC windDesc{};
        windDesc.ByteWidth = sizeof(XMFLOAT4);
        windDesc.Usage = D3D11_USAGE_DEFAULT;
        windDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        const XMFLOAT4 wind{ windTime, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA initialWind{};
        initialWind.pSysMem = &wind;
        Renderer::GetDevice()->CreateBuffer(&windDesc, &initialWind, pipeline.wind.ReleaseAndGetAddressOf());

        Renderer::CreateVertexShader(pipeline.instancedVertex.ReleaseAndGetAddressOf(),
            pipeline.instancedLayout.ReleaseAndGetAddressOf(), "shader\\litInstancedVS.cso", true);
        if (pipeline.instancedVertex && pipeline.instancedLayout)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = instancesPerUpload * sizeof(XMFLOAT4X4);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Renderer::GetDevice()->CreateBuffer(&desc, nullptr, pipeline.instanceBuffer.ReleaseAndGetAddressOf());
        }
        return true;
    }
}

std::shared_ptr<Scenery::Model> Scenery::load(const std::string& path)
{
    auto& cached = models[path];
    if (auto shared = cached.lock()) return shared;
    auto model = std::make_shared<Model>();
    if (!initialize() || !model->mesh.tryLoad(path.c_str(), true)) return {};
    cached = model;
    if (path == "asset/maps/rural-town/wild-meadow-grass.glb")
    {
        model->farModel = load("asset/maps/rural-town/wild-meadow-grass-far.glb");
        if (model->farModel)
        {
            const auto detailed = model->mesh.staticHalfExtents();
            const auto reduced = model->farModel->mesh.staticHalfExtents();
            if (reduced.x > 0 && reduced.y > 0 && reduced.z > 0)
                model->farScale = { detailed.x / reduced.x, detailed.y / reduced.y, detailed.z / reduced.z };
            else
                model->farModel.reset();
        }
    }
    return model;
}

void Scenery::begin(FXMMATRIX view, CXMMATRIX projection, float dt)
{
    BoundingFrustum local(projection);
    local.Transform(frustum.emplace(), XMMatrixInverse(nullptr, view));
    windTime += std::max(0.0f, dt);
    if (pipeline.wind)
    {
        const XMFLOAT4 wind{ windTime, 0, 0, 0 };
        Renderer::GetDeviceContext()->UpdateSubresource(pipeline.wind.Get(), 0, nullptr, &wind, 0, 0);
    }
}

BoundingBox Scenery::bounds(const Model& model, FXMMATRIX world)
{
    const BoundingBox local({ 0, 0, 0 }, model.mesh.staticHalfExtents());
    BoundingBox transformed;
    local.Transform(transformed, world);
    const float padding = model.mesh.windReach() * XMVectorGetX(XMVector3Length(world.r[1]));
    transformed.Extents.x += padding;
    transformed.Extents.z += padding;
    return transformed;
}

bool Scenery::visible(const BoundingBox& box)
{
    return !frustum || frustum->Intersects(box);
}

bool Scenery::visible(const Model& model, FXMMATRIX world)
{
    return visible(bounds(model, world));
}

void Scenery::draw(Model& model, FXMMATRIX world, bool allowInstancing)
{
    Model* selected = &model;
    XMMATRIX transform = world;
    if (model.farModel && frustum)
    {
        const auto delta = world.r[3] - XMLoadFloat3(&frustum->Origin);
        if (XMVectorGetX(XMVector3LengthSq(delta)) > meadowDetailDistance * meadowDetailDistance)
        {
            selected = model.farModel.get();
            transform = XMMatrixScalingFromVector(XMLoadFloat3(&model.farScale)) * world;
        }
    }
    if (allowInstancing && pipeline.instanceBuffer)
    {
        if (selected->mesh.hasTransparentParts())
            XMStoreFloat4x4(&deferred.emplace_back(Deferred{ selected, {} }).world, transform);
        else
        {
            if (selected->instances.empty()) batches.push_back(selected);
            XMStoreFloat4x4(&selected->instances.emplace_back(), transform);
        }
        return;
    }
    bind(false);
    selected->mesh.drawAt(transform);
}

void Scenery::flush()
{
    if (batches.empty() && deferred.empty()) return;
    if (!batches.empty()) bind(true);
    auto* context = Renderer::GetDeviceContext();
    ID3D11Buffer* buffer = pipeline.instanceBuffer.Get();
    const UINT stride = sizeof(XMFLOAT4X4), offset = 0;
    context->IASetVertexBuffers(1, 1, &buffer, &stride, &offset);
    for (auto* model : batches)
    {
        for (size_t i = 0; i < model->instances.size(); i += instancesPerUpload)
        {
            const size_t count = std::min<size_t>(instancesPerUpload, model->instances.size() - i);
            D3D11_MAPPED_SUBRESOURCE upload{};
            if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &upload)))
            {
                std::memcpy(upload.pData, model->instances.data() + i, count * stride);
                context->Unmap(buffer, 0);
                model->mesh.drawInstances(static_cast<UINT>(count));
            }
            else
            {
                bind(false);
                for (size_t j = i; j < i + count; ++j)
                    model->mesh.drawAt(XMLoadFloat4x4(&model->instances[j]));
                bind(true);
            }
        }
        model->instances.clear();
    }
    ID3D11Buffer* none = nullptr;
    context->IASetVertexBuffers(1, 1, &none, &stride, &offset);
    batches.clear();
    if (!deferred.empty()) bind(false);
    for (const auto& item : deferred) item.model->mesh.drawAt(XMLoadFloat4x4(&item.world));
    deferred.clear();
}

void Scenery::reset()
{
    frustum.reset();
    models.clear();
    batches.clear();
    deferred.clear();
    windTime = 0.0f;
    pipeline = {};
}
