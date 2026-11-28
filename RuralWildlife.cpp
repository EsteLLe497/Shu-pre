#include "main.h"
#include "renderer.h"
#include "RuralWildlife.h"
#include "MeshField.h"
#include "Scenery.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    constexpr XMFLOAT2 fishHabitat[] = {
        { -72, -52 }, { -30, -44 }, { 6, -50 }, { 36, -54 }, { 46, -54 }, { 55, -51 }, { 75, -46 }
    };
    struct FrogPerch
    {
        float x, z;
        bool paddy = true;
    };
    constexpr FrogPerch frogPerches[] = {
        { -75, 44 }, { -64, 34 }, { -39, 35 }, { -25, 44 }, { -14, 36 }, { -15, 15 },
        { -26, 25 }, { -37, 14 }, { -51, 24 }, { -63, 14 }, { -86, -1 }, { -61, -7 },
        { -40, -10 }, { -38, -30 }, { -64, -32 }, { -85, -32 }, { 47, -46, false }, { 53, -47, false }
    };
    constexpr XMFLOAT2 dragonflyHabitat[] = {
        { -74, 43 }, { -62, 35 }, { -39, 35 }, { -18, 38 }, { -17, 16 }, { -40, 14 },
        { -64, 15 }, { -85, 2 }, { -40, -8 }, { -24, -26 }, { -63, -28 }, { 35, -46 }, { 55, -46 }
    };
    constexpr XMFLOAT2 butterflyHabitat[] = {
        { -7, 46 }, { 12, 22 }, { 31, 13 }, { 43, 11 }, { 62, 14 }, { 52, -37 }, { -12, 76 }
    };

    bool nearby(const Vector3& eye, float x, float z, float range)
    {
        const float dx = eye.x - x, dz = eye.z - z;
        return dx * dx + dz * dz < range * range;
    }

    void render(Scenery::Model& mesh, FXMMATRIX transform)
    {
        if (Scenery::visible(mesh, transform)) Scenery::draw(mesh, transform, true);
    }

    // Import normalizes each part about its own center. Move the center back
    // relative to the tail or wing hinge before rotating it.
    XMMATRIX wingTransform(const Scenery::Model& wing, float side, float angle)
    {
        const float center = wing.mesh.staticHalfExtents().x;
        return XMMatrixRotationZ(side < 0.0f ? XM_PI : 0.0f)
            * XMMatrixTranslation(side * center, 0.0f, 0.0f)
            * XMMatrixRotationZ(side * angle);
    }
}

void RuralWildlife::load()
{
    m_fish = Scenery::load("asset/maps/rural-town/fauna-fish.glb");
    m_tail = Scenery::load("asset/maps/rural-town/fauna-fish-tail.glb");
    m_frog = Scenery::load("asset/maps/rural-town/fauna-frog.glb");
    m_dragonfly = Scenery::load("asset/maps/rural-town/fauna-dragonfly.glb");
    m_dragonflyWing = Scenery::load("asset/maps/rural-town/fauna-dragonfly-wing.glb");
    m_butterfly = Scenery::load("asset/maps/rural-town/fauna-butterfly.glb");
    m_butterflyWing = Scenery::load("asset/maps/rural-town/fauna-butterfly-wing.glb");
}

void RuralWildlife::rebuildSchools(const Wave::Layout& water)
{
    m_riverDirty = false;
    m_rivers.clear();
    m_schools.clear();
    for (const auto& river : water.rivers)
        m_rivers.push_back(river.sections());

    for (std::size_t i = 0; i < std::size(fishHabitat); ++i)
    {
        float closest = 16.0f * 16.0f;
        School school{};
        for (std::size_t river = 0; river < m_rivers.size(); ++river)
        {
            const auto& sections = m_rivers[river];
            if (sections.size() < 2) continue;
            for (std::size_t section = 0; section < sections.size(); ++section)
            {
                const auto& center = sections[section].center;
                const float x = fishHabitat[i].x - center.x, z = fishHabitat[i].y - center.y;
                const float distance = x * x + z * z;
                if (distance >= closest) continue;
                closest = distance;
                school = { i, river, section };
            }
        }
        if (closest < 16.0f * 16.0f) m_schools.push_back(school);
    }
}

void RuralWildlife::draw(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye, float dt)
{
    m_time += std::clamp(dt, 0.0f, 0.1f);
    if (m_fish && m_tail) drawFish(water, terrain, eye);
    if (m_frog) drawFrogs(water, terrain, eye);
    drawInsects(terrain, eye);
}

void RuralWildlife::drawFish(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye)
{
    if (m_riverDirty) rebuildSchools(water);
    for (const auto& school : m_schools)
    {
        const auto index = school.habitat;
        const auto& sections = m_rivers[school.river];
        const auto& river = water.rivers[school.river];
        const auto& home = sections[school.section];
        if (!nearby(eye, home.center.x, home.center.y, 33.0f)) continue;
        const float deltaX = fishHabitat[index].x - home.center.x;
        const float deltaZ = fishHabitat[index].y - home.center.y;
        const float offset = deltaZ * home.tangent.x - deltaX * home.tangent.y;
        const float lane = std::clamp(offset, -river.width * 0.31f, river.width * 0.31f);
        const float sweep = std::min(1.30f, river.width * 0.082f);

        const auto sample = [&](float phase, float stagger, float spread)
        {
            const float end = sections.back().distance;
            const float start = std::min(0.55f, end * 0.25f);
            const float d = std::clamp(home.distance + 4.5f * std::sin(phase) + stagger, start, end - start);
            const auto next = std::lower_bound(sections.begin() + 1, sections.end(), d,
                [](const auto& section, float distance) { return section.distance < distance; });
            const auto& a = *(next - 1);
            const float s = (d - a.distance) / std::max(0.0001f, next->distance - a.distance);
            const float across = std::clamp(lane + sweep * std::cos(phase) + spread,
                -river.width * 0.41f, river.width * 0.41f);
            const auto u = a.across(across), v = next->across(across);
            return XMFLOAT2(u.x + (v.x - u.x) * s, u.y + (v.y - u.y) * s);
        };

        for (int member = 0; member < 5; ++member)
        {
            const float phase = m_time * (0.38f + (index % 3) * 0.032f) + index * 1.22f - member * 0.23f;
            const float stagger = (member - 2) * 0.66f;
            const float spread = ((member % 2) ? 0.38f : -0.30f) + stagger * 0.22f;
            const auto pos = sample(phase, stagger, spread), ahead = sample(phase + 0.025f, stagger, spread);
            float floor;
            if (!terrain.tryGetHeight(pos.x, pos.y, floor) || floor > river.height - 0.30f) continue;
            const float depth = 0.27f + 0.035f * member + 0.065f * std::sin(phase * 1.7f);
            const float y = std::max(river.height - depth, floor + 0.14f);
            const float flick = std::sin(m_time * 14.0f + member * 0.88f + index);
            const float facing = std::atan2(ahead.x - pos.x, ahead.y - pos.y);
            const float size = 0.54f + 0.045f * ((member * 3 + index) % 5);
            const auto world = XMMatrixRotationY(facing + 0.065f * flick) * XMMatrixTranslation(pos.x, y, pos.y);
            render(*m_fish, XMMatrixScaling(size, size, size) * world);
            const float rear = m_fish->mesh.staticHalfExtents().z * size;
            const float tail = size * 0.37f;
            render(*m_tail, XMMatrixScaling(tail, tail, tail)
                * XMMatrixTranslation(0, 0, -tail * m_tail->mesh.staticHalfExtents().z)
                * XMMatrixRotationY(0.44f * flick) * XMMatrixTranslation(0, -size * 0.06f, -rear + 0.028f) * world);
        }
    }
}

void RuralWildlife::drawFrogs(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye)
{
    for (std::size_t i = 0; i < std::size(frogPerches); ++i)
    {
        const auto& perch = frogPerches[i];
        if (!nearby(eye, perch.x, perch.z, 21.0f)) continue;
        const float period = 4.0f + (i % 5) * 0.58f;
        const float cycle = std::fmod(m_time + 2.1f * i, period * 2.0f);
        const bool returning = cycle >= period;
        const float progress = std::clamp((std::fmod(cycle, period) - (period - 0.50f)) * 2.0f, 0.0f, 1.0f);
        const float along = returning ? 1.0f - progress : progress;
        const float direction = i * 2.39996f;
        const float x = perch.x + std::sin(direction) * along * 0.56f;
        const float z = perch.z + std::cos(direction) * along * 0.56f;
        float floor;
        if (!terrain.tryGetHeight(x, z, floor)) continue;
        if (perch.paddy && floor < water.pondHeight + 0.025f) continue;

        const float hop = 4.0f * progress * (1.0f - progress);
        const float size = 0.22f + 0.03f * (i % 4);
        const float height = size * (0.96f + 0.14f * hop + 0.035f * std::sin(m_time * 6.2f + i));
        render(*m_frog, XMMatrixScaling(size, height, size * (1.0f + hop * 0.10f))
            * XMMatrixRotationRollPitchYaw(-0.11f * hop, direction + (returning ? XM_PI : 0.0f), 0.0f)
            * XMMatrixTranslation(x, floor + m_frog->mesh.staticHalfExtents().y * height + 0.22f * hop, z));
    }
}

void RuralWildlife::drawInsects(const MeshField& terrain, const Vector3& eye)
{
    const auto flight = [&](Scenery::Model& body, Scenery::Model& wing, const XMFLOAT2& home, std::size_t i, bool butterfly)
    {
        if (!nearby(eye, home.x, home.y, butterfly ? 23.0f : 26.0f)) return;
        const float time = m_time * (butterfly ? 0.84f : 1.23f) + i * 2.27f;
        const float stretch = butterfly ? 1.32f : 2.55f;
        const float x = home.x + stretch * std::sin(time) + 0.34f * std::sin(time * 2.0f);
        const float z = home.y + stretch * 0.67f * std::cos(time * 1.31f);
        float floor;
        if (!terrain.tryGetHeight(x, z, floor)) return;
        const float y = std::max(floor, -0.08f) + (butterfly ? 0.72f : 1.03f)
            + 0.23f * std::sin(time * 2.4f) + 0.11f * std::sin(time * 4.1f);
        const float facing = std::atan2(stretch * std::cos(time) + 0.68f * std::cos(time * 2.0f),
            -stretch * 0.8777f * std::sin(time * 1.31f));
        const float flap = butterfly ? 0.40f + 0.80f * std::sin(m_time * 19.0f + i)
            : 0.30f * std::sin(m_time * 43.0f + i);
        const float length = butterfly ? 0.13f : 0.21f;
        const auto world = XMMatrixRotationRollPitchYaw(0.12f * std::sin(time * 2.4f), facing, 0.15f * std::sin(time))
            * XMMatrixTranslation(x, y, z);
        render(body, XMMatrixScaling(length, length, length) * world);
        const float span = butterfly ? 0.19f : 0.24f;
        for (float side : { -1.0f, 1.0f })
            render(wing, wingTransform(wing, side, flap) * XMMatrixScaling(span, span, span) * world);
    };

    if (m_dragonfly && m_dragonflyWing)
        for (std::size_t i = 0; i < std::size(dragonflyHabitat); ++i)
            flight(*m_dragonfly, *m_dragonflyWing, dragonflyHabitat[i], i, false);
    if (m_butterfly && m_butterflyWing)
        for (std::size_t i = 0; i < std::size(butterflyHabitat); ++i)
            flight(*m_butterfly, *m_butterflyWing, butterflyHabitat[i], i, true);
}
