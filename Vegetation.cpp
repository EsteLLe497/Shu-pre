#include "main.h"
#include "renderer.h"
#include "Vegetation.h"
#include "MeshField.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <map>
#include <ostream>
#include <tuple>
#include <unordered_set>

namespace
{
    struct Species
    {
        const char* model;
        float size, gap, range;
    };

    constexpr Species speciesInfo[Vegetation::speciesCount]{
        { "roadside-grasses.obj", 2.9f, 1.25f, 65.0f },
        { "shrine-ferns.obj", 1.65f, 1.50f, 65.0f },
        { "shrine-forest-understory.obj", 2.3f, 2.1f, 90.0f },
        { "summer-wildflowers.obj", 2.3f, 1.50f, 65.0f },
        { "riverbank-reeds.obj", 2.9f, 2.1f, 80.0f },
    };

    std::uint32_t cellKey(int x, int z)
    {
        return (static_cast<std::uint32_t>(x) & 0xffffu) << 16
            | (static_cast<std::uint32_t>(z) & 0xffffu);
    }

    float sample(std::uint32_t key)
    {
        key ^= key >> 16;
        key *= 0x7feb352du;
        key ^= key >> 15;
        key *= 0x846ca68bu;
        key ^= key >> 16;
        return static_cast<float>(key >> 8) / 16777216.0f;
    }
}

bool Vegetation::read(std::istream& input, std::vector<Plant>& plants)
{
    std::string signature;
    size_t count = 0;
    if (!(input >> signature >> count) || signature != "Vegetation" || count > maxPlants) return false;
    std::vector<Plant> loaded(count);
    for (auto& plant : loaded)
        if (!(input >> plant.species >> plant.x >> plant.z >> plant.offset >> plant.angle >> plant.scale)
            || plant.species < 0 || plant.species >= speciesCount
            || !(std::abs(plant.x) <= 100.0f && std::abs(plant.z) <= 100.0f && std::abs(plant.offset) <= 10.0f
                && std::isfinite(plant.angle) && plant.scale > 0.0f && plant.scale <= 20.0f))
            return false;
    plants = std::move(loaded);
    return true;
}

void Vegetation::write(std::ostream& output, const std::vector<Plant>& plants)
{
    output << "Vegetation " << plants.size() << '\n';
    for (const auto& p : plants)
        output << p.species << ' ' << p.x << ' ' << p.z << ' ' << p.offset << ' ' << p.angle << ' ' << p.scale << '\n';
}

void Vegetation::brush(std::vector<Plant>& plants, const MeshField& terrain, const Vector3& from, const Vector3& to,
    float radius, float density, int species, bool erase)
{
    const float dx = to.x - from.x, dz = to.z - from.z;
    const float lengthSquared = dx * dx + dz * dz;
    const auto distanceSquared = [&](float x, float z)
    {
        const float px = x - from.x, pz = z - from.z;
        const float t = lengthSquared > 0.0001f
            ? std::clamp((px * dx + pz * dz) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        const float gapX = px - t * dx, gapZ = pz - t * dz;
        return gapX * gapX + gapZ * gapZ;
    };
    if (erase)
    {
        plants.erase(std::remove_if(plants.begin(), plants.end(), [&](const Plant& plant)
        {
            return (species < 0 || species == plant.species) && distanceSquared(plant.x, plant.z) <= radius * radius;
        }), plants.end());
        return;
    }
    const auto& kind = speciesInfo[species];
    const auto cell = [&](float p) { return static_cast<int>(std::floor(p / kind.gap)); };
    const int left = cell(std::min(from.x, to.x) - radius), right = cell(std::max(from.x, to.x) + radius);
    const int low = cell(std::min(from.z, to.z) - radius), high = cell(std::max(from.z, to.z) + radius);
    std::unordered_set<std::uint32_t> occupied;
    for (const auto& p : plants)
    {
        if (p.species != species) continue;
        const int x = cell(p.x), z = cell(p.z);
        if (x >= left && x <= right && z >= low && z <= high) occupied.insert(cellKey(x, z));
    }
    for (int z = low; z <= high && plants.size() < maxPlants; ++z)
        for (int x = left; x <= right && plants.size() < maxPlants; ++x)
        {
            const auto key = cellKey(x, z);
            const auto seed = key + static_cast<std::uint32_t>(species + 1) * 73939133u;
            if (occupied.count(key) || sample(seed) >= density) continue;
            const float px = (x + 0.14f + 0.72f * sample(seed + 1)) * kind.gap;
            const float pz = (z + 0.14f + 0.72f * sample(seed + 2)) * kind.gap;
            float height;
            if (distanceSquared(px, pz) > radius * radius || !terrain.tryGetHeight(px, pz, height)) continue;
            plants.push_back({ species, px, pz, -0.035f, XM_2PI * sample(seed + 3),
                kind.size * (0.65f + 0.65f * sample(seed + 4)) });
        }
}

void Vegetation::Renderer::rebuild(const std::vector<Plant>& plants, const MeshField& terrain)
{
    // Keep still-used models alive while rebuilding after a brush stroke or terrain edit.
    auto previous = std::move(m_patches);
    m_patches.clear();
    std::shared_ptr<Scenery::Model> models[speciesCount];
    bool attempted[speciesCount]{};
    std::map<std::tuple<int, int, int>, size_t> cells;
    for (const auto& plant : plants)
    {
        const auto& kind = speciesInfo[plant.species];
        auto& model = models[plant.species];
        if (!attempted[plant.species])
        {
            attempted[plant.species] = true;
            model = Scenery::load(std::string("asset/maps/rural-town/") + kind.model);
        }
        float height;
        if (!model || !terrain.tryGetHeight(plant.x, plant.z, height)) continue;
        const auto extents = model->mesh.staticHalfExtents();
        const XMMATRIX world = XMMatrixScaling(plant.scale, plant.scale, plant.scale)
            * XMMatrixRotationY(plant.angle)
            * XMMatrixTranslation(plant.x, height + plant.offset + extents.y * plant.scale, plant.z);
        BoundingBox transformed, box({ 0, 0, 0 }, extents);
        box.Transform(transformed, world);
        const auto cell = std::make_tuple(plant.species, static_cast<int>(std::floor(plant.x / 16.0f)),
            static_cast<int>(std::floor(plant.z / 16.0f)));
        const auto [found, inserted] = cells.try_emplace(cell, m_patches.size());
        if (inserted) m_patches.push_back({ model, transformed, kind.range, {} });
        auto& patch = m_patches[found->second];
        if (!inserted) BoundingBox::CreateMerged(patch.bounds, patch.bounds, transformed);
        XMStoreFloat4x4(&patch.instances.emplace_back(), world);
    }
    m_terrain = &terrain;
    m_revision = terrain.revision();
    m_dirty = false;
}

void Vegetation::Renderer::draw(const std::vector<Plant>& plants, const MeshField& terrain, const Vector3& eye)
{
    if (m_dirty || m_terrain != &terrain || m_revision != terrain.revision()) rebuild(plants, terrain);
    for (auto& patch : m_patches)
    {
        const auto& bounds = patch.bounds;
        const float x = std::max(0.0f, std::abs(bounds.Center.x - eye.x) - bounds.Extents.x);
        const float z = std::max(0.0f, std::abs(bounds.Center.z - eye.z) - bounds.Extents.z);
        if (x * x + z * z > patch.range * patch.range || !Scenery::visible(bounds)) continue;
        for (const auto& world : patch.instances) Scenery::draw(*patch.model, XMLoadFloat4x4(&world), true);
    }
}
