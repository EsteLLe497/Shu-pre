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

    struct Pipeline
    {
        ComPtr<ID3D11InputLayout> layout, instancedLayout;
        ComPtr<ID3D11VertexShader> vertex, instancedVertex;
        ComPtr<ID3D11PixelShader> pixel;
        ComPtr<ID3D11Buffer> instanceBuffer;
    };
    Pipeline pipeline;

    void bind(bool instanced)
    {
        auto* context = Renderer::GetDeviceContext();
        context->IASetInputLayout(instanced ? pipeline.instancedLayout.Get() : pipeline.layout.Get());
        context->VSSetShader(instanced ? pipeline.instancedVertex.Get() : pipeline.vertex.Get(), nullptr, 0);
        context->PSSetShader(pipeline.pixel.Get(), nullptr, 0);
    }

    bool initialize()
    {
        if (pipeline.vertex && pipeline.layout && pipeline.pixel) return true;
        Renderer::CreateVertexShader(pipeline.vertex.ReleaseAndGetAddressOf(),
            pipeline.layout.ReleaseAndGetAddressOf(), "shader\\litTextureVS.cso");
        Renderer::CreatePixelShader(pipeline.pixel.ReleaseAndGetAddressOf(), "shader\\litTexturePS.cso");
        if (!pipeline.vertex || !pipeline.layout || !pipeline.pixel) return false;

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
    return model;
}

void Scenery::begin(FXMMATRIX view, CXMMATRIX projection)
{
    BoundingFrustum local(projection);
    local.Transform(frustum.emplace(), XMMatrixInverse(nullptr, view));
}

bool Scenery::visible(const BoundingBox& box)
{
    return !frustum || frustum->Intersects(box);
}

bool Scenery::visible(const Model& model, FXMMATRIX world)
{
    BoundingBox bounds({ 0, 0, 0 }, model.mesh.staticHalfExtents());
    BoundingBox transformed;
    bounds.Transform(transformed, world);
    return visible(transformed);
}

void Scenery::draw(Model& model, FXMMATRIX world, bool allowInstancing)
{
    if (allowInstancing && pipeline.instanceBuffer)
    {
        if (model.mesh.hasTransparentParts())
            XMStoreFloat4x4(&deferred.emplace_back(Deferred{ &model, {} }).world, world);
        else
        {
            if (model.instances.empty()) batches.push_back(&model);
            XMStoreFloat4x4(&model.instances.emplace_back(), world);
        }
        return;
    }
    bind(false);
    model.mesh.drawAt(world);
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
    pipeline = {};
}
