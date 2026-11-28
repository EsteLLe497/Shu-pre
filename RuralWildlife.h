#pragma once

#include <memory>

#include "Vector3.h"
#include "Wave.h"

namespace Scenery { struct Model; }

class RuralWildlife
{
public:
    void load();
    void invalidateRiver() { m_riverDirty = true; }
    void draw(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye, float dt);

private:
    struct School
    {
        std::size_t habitat;
        std::size_t river;
        std::size_t section;
    };

    void rebuildSchools(const Wave::Layout& water);
    void drawFish(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye);
    void drawFrogs(const Wave::Layout& water, const MeshField& terrain, const Vector3& eye);
    void drawInsects(const MeshField& terrain, const Vector3& eye);

    std::shared_ptr<Scenery::Model> m_fish, m_tail, m_frog;
    std::shared_ptr<Scenery::Model> m_dragonfly, m_dragonflyWing, m_butterfly, m_butterflyWing;
    std::vector<std::vector<Wave::River::Section>> m_rivers;
    std::vector<School> m_schools;
    float m_time = 0.0f;
    bool m_riverDirty = true;
};
