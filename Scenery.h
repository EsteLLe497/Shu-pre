#pragma once

#include <DirectXCollision.h>
#include <memory>
#include <vector>

#include "animationModel.h"

namespace Scenery
{
    struct Model
    {
        AnimationModel mesh{ nullptr };
        std::shared_ptr<Model> farModel;
        XMFLOAT3 farScale{ 1, 1, 1 };
        std::vector<XMFLOAT4X4> instances;
        ~Model() { mesh.Uninit(); }
    };

    std::shared_ptr<Model> load(const std::string& path);
    void begin(FXMMATRIX view, CXMMATRIX projection, float dt);
    BoundingBox bounds(const Model& model, FXMMATRIX world);
    bool visible(const BoundingBox& box);
    bool visible(const Model& model, FXMMATRIX world);
    void draw(Model& model, FXMMATRIX world, bool allowInstancing);
    void flush();
    void reset();
}
