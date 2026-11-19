#pragma once

#include <DirectXMath.h>

class MeshField;
class Player;
class Vector3;

namespace SceneEditor
{
    void init(Player& player, MeshField& terrain);
    void shutdown();
    bool isAvailable();
    bool isOpen();
    void toggle();
    bool cameraMatrices(Vector3& eye, DirectX::XMMATRIX& view, DirectX::XMMATRIX& projection);
    void draw();
}
