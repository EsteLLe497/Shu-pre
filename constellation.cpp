// =========================================================
// 星座の配置・クリック判定・結線 [constellation.cpp]
// =========================================================
#include "main.h"
#include "constellation.h"
#include "input.h"

namespace
{
    using namespace constellation;

    // アンタレスから尾へ伸びる形。表示用に間隔を調整している。
    const XMFLOAT2 scorpioStars[] = {
        {-1.07f, 0.69f}, {-0.94f, 0.34f}, {-1.09f, 0.05f}, {-0.47f, 0.17f},
        {-0.18f, -0.21f}, {0.04f, -0.60f}, {0.47f, -0.77f}, {0.91f, -0.63f},
        {1.13f, -0.32f}, {1.03f, 0.02f}, {0.73f, 0.13f}
    };
    constexpr Link scorpioLinks[] = {
        {0, 1}, {2, 1}, {1, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {7, 8}, {8, 9}, {9, 10}
    };
    const Shape scorpioShape{
        scorpioStars, _countof(scorpioStars), scorpioLinks, _countof(scorpioLinks), {0.48f, 0.70f, 1.0f}
    };

    const Shape* activeShape = nullptr;
    std::array<bool, maxLinks> connected{};
    int selected = -1;
    unsigned int connectedCount = 0;

    // 現在の始点から結べる未接続の辺を探す。始点選択前は全ての未接続辺が対象。
    int availableLink(int star, int from = -1)
    {
        for (unsigned int i = 0; i < activeShape->linkCount; ++i)
        {
            if (connected[i])
                continue;

            const auto link = activeShape->links[i];
            const int a = static_cast<int>(link.from);
            const int b = static_cast<int>(link.to);
            if ((star == a && (from < 0 || from == b)) || (star == b && (from < 0 || from == a)))
                return static_cast<int>(i);
        }
        return -1;
    }

    void selectStar(int star)
    {
        if (selected == star)
        {
            selected = -1;
            return;
        }

        const int link = availableLink(star, selected);
        if (link < 0)
            return;

        if (selected >= 0)
        {
            connected[link] = true;
            ++connectedCount;
        }

        // 行き止まりまで結んだら、新しい枝の始点を選べるようにする。
        selected = availableLink(star) >= 0 ? star : -1;
    }
}

const constellation::Shape& constellation::scorpio()
{
    return scorpioShape;
}

void constellation::open(const Shape& shape)
{
    reset();
    if (!shape.stars || !shape.links || shape.starCount > maxStars ||
        shape.linkCount == 0 || shape.linkCount > maxLinks)
        return;

    for (unsigned int i = 0; i < shape.linkCount; ++i)
        if (shape.links[i].from >= shape.starCount || shape.links[i].to >= shape.starCount)
            return;

    activeShape = &shape;
}

void constellation::reset()
{
    activeShape = nullptr;
    connected.fill(false);
    connectedCount = 0;
    selected = -1;
}

void constellation::project(FXMMATRIX viewProjection, XMFLOAT3 center, float radius, float yaw,
    float visibility, bool canPick, DrawData& data, XMFLOAT3* aimedStar)
{
    if (!activeShape || visibility <= 0.0f)
        return;

    const auto& shape = *activeShape;
    const auto base = XMLoadFloat3(&center);
    const auto forward = XMVectorSet(sinf(yaw), 0, cosf(yaw), 0);
    const auto right = XMVectorSet(cosf(yaw), 0, -sinf(yaw), 0);
    constexpr XMFLOAT2 reticle{ SCREEN_WIDTH * 0.5f, SCREEN_HEIGHT * 0.5f };
    const auto includePoint = [&](float x, float y) {
        data.bounds.x = std::min(data.bounds.x, x - 32.0f);
        data.bounds.y = std::min(data.bounds.y, y - 32.0f);
        data.bounds.z = std::max(data.bounds.z, x + 32.0f);
        data.bounds.w = std::max(data.bounds.w, y + 32.0f);
    };
    data.bounds = { 1.0e6f, 1.0e6f, -1.0e6f, -1.0e6f };
    float nearest = 19.0f * 19.0f;
    int hovered = -1;

    for (unsigned int i = 0; i < shape.starCount; ++i)
    {
        const auto local = shape.stars[i];
        // 星座データをドーム中心からの方向に変え、球面上へ置く。
        const auto direction = XMVector3Normalize(forward * 5.0f + right * (1.05f + local.x)
            + XMVectorSet(0, 1.95f + local.y, 0, 0));
        const auto world = base + direction * radius;
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector3Transform(world, viewProjection));
        if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w)
            continue;

        auto& star = data.stars[i];
        star = { (clip.x / clip.w + 1.0f) * 0.5f * SCREEN_WIDTH,
            (1.0f - clip.y / clip.w) * 0.5f * SCREEN_HEIGHT, 1.0f, 0.0f };
        includePoint(star.x, star.y);
        const float dx = reticle.x - star.x;
        const float dy = reticle.y - star.y;
        const float distance = dx * dx + dy * dy;
        if (canPick && distance < nearest)
        {
            nearest = distance;
            hovered = static_cast<int>(i);
            if (aimedStar)
                XMStoreFloat3(aimedStar, world);
        }
    }

    if (!aimedStar && hovered >= 0 && connectedCount < shape.linkCount && Input::GetKeyTrigger(VK_LBUTTON))
        selectStar(hovered);

    const bool complete = connectedCount == shape.linkCount;
    const bool validTarget = hovered >= 0 && (complete || hovered == selected || availableLink(hovered, selected) >= 0);
    const bool invalidTarget = hovered >= 0 && !validTarget;
    data.color = { shape.color.x, shape.color.y, shape.color.z, visibility };
    data.state = { static_cast<float>(shape.starCount), 0.0f, complete ? 1.0f : 0.0f,
        canPick ? (validTarget ? 2.0f : (invalidTarget ? -1.0f : 1.0f)) : 0.0f };
    for (unsigned int i = 0; i < shape.starCount; ++i)
    {
        const int index = static_cast<int>(i);
        const bool available = availableLink(index, selected) >= 0;
        data.stars[i].w = complete || index == selected || (available && index == hovered)
            ? 1.0f : (available ? 0.0f : -1.0f);
    }

    // 始点から照準へ仮の線を引き、候補に重なれば星の中心まで伸ばす。
    if (canPick && !complete && selected >= 0 && data.stars[selected].z > 0.0f)
    {
        const auto& start = data.stars[selected];
        const bool snap = validTarget && hovered != selected;
        const float x = snap ? data.stars[hovered].x : reticle.x;
        const float y = snap ? data.stars[hovered].y : reticle.y;
        data.previewLine = { start.x, start.y, x, y };
        data.previewState.x = invalidTarget ? -1.0f : (snap ? 2.0f : 1.0f);
        includePoint(x, y);
    }

    // 完成済みの線と、選んだ星から伸ばせる線を区別する。
    unsigned int drawn = 0;
    for (unsigned int i = 0; i < shape.linkCount; ++i)
    {
        const auto link = shape.links[i];
        const bool hint = selected == static_cast<int>(link.from) || selected == static_cast<int>(link.to);
        if ((!connected[i] && !hint) || data.stars[link.from].z == 0.0f || data.stars[link.to].z == 0.0f)
            continue;
        data.links[drawn++] = { static_cast<float>(link.from), static_cast<float>(link.to),
            connected[i] ? 1.0f : 0.0f, 0.0f };
    }
    data.state.y = static_cast<float>(drawn);
}
