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
    void updatePhysics(float dt);
    void resolvePlayerCollision(const Vector3& previous, Vector3& position, Vector3& velocity, bool& grounded);
    bool blocksEnemy(const Vector3& position, float radius);
    void draw();
}
