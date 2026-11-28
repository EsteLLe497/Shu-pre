#pragma once

#include <iosfwd>
#include <memory>
#include <vector>

#include "Scenery.h"

class MeshField;
class Vector3;

namespace Vegetation
{
    constexpr size_t maxPlants = 18000;
    constexpr int speciesCount = 5;

    struct Plant
    {
        int species;
        float x, z, offset, angle, scale;
    };

    bool read(std::istream& input, std::vector<Plant>& plants);
    void write(std::ostream& output, const std::vector<Plant>& plants);
    void brush(std::vector<Plant>& plants, const MeshField& terrain, const Vector3& from, const Vector3& to,
        float radius, float density, int species, bool erase);

    class Renderer
    {
    public:
        void invalidate() { m_dirty = true; }
        void draw(const std::vector<Plant>& plants, const MeshField& terrain, const Vector3& eye);

    private:
        struct Patch
        {
            std::shared_ptr<Scenery::Model> model;
            BoundingBox bounds{};
            float range = 0;
            std::vector<XMFLOAT4X4> instances;
        };
        void rebuild(const std::vector<Plant>& plants, const MeshField& terrain);
        std::vector<Patch> m_patches;
        const MeshField* m_terrain = nullptr;
        unsigned int m_revision = 0;
        bool m_dirty = true;
    };
}
