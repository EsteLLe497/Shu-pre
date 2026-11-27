#pragma once

#include <DirectXMath.h>

class MeshField;
class Player;
class Vector3;

namespace SceneEditor
{
    void useRuralMap();
    void init(Player& player, MeshField& terrain);
    void shutdown();
    bool isAvailable();
    bool isOpen();
    bool isPlaying();
    void toggle();
    void close();
    void processPlayMode();
    void drawPlayMode();
    void drawVegetation(const Vector3& eye);
    void drawWater(const Vector3& eye, DirectX::FXMMATRIX viewProjection, float dt);
    bool cameraMatrices(Vector3& eye, DirectX::XMMATRIX& view, DirectX::XMMATRIX& projection);
    void updatePhysics(float dt);
    void resolvePlayerCollision(const Vector3& previous, Vector3& position, Vector3& velocity, bool& grounded);
    bool blocksEnemy(const Vector3& position, float radius);
    void draw();
}
