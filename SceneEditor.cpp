#include "main.h"
#include "renderer.h"
#include "GameObject.h"
#include "manager.h"
#include "SceneEditor.h"
#include "SceneAudio.h"
#include "MeshField.h"
#include "Player.h"
#include "Enemy.h"
#include "Box.h"
#include "Grass.h"
#include "Particle.h"
#include "Camera.h"
#include "SkyDome.h"
#include "Score.h"
#include "animationModel.h"
#include "imgui/imgui.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <deque>
#include <memory>
#include <wrl/client.h>
#include <DirectXCollision.h>

namespace
{
    namespace fs = std::filesystem;
    constexpr char layoutPath[] = "asset/scene-layout.txt";
    constexpr char temporaryPath[] = "asset/scene-layout.txt.tmp";
    constexpr size_t maxObjects = 512;
    constexpr int layoutVersion = 7;

    enum class Kind { Player, Enemy, Boss, Box, Grass, Particle, Model };
    const char* const labels[] = { "Player spawn", "Enemy", "Mid boss", "Box", "Grass", "Particle", "Model" };

    struct Transform
    {
        Vector3 position{ 0, 0, 0 };
        Vector3 rotation{ 0, 0, 0 };
        Vector3 scale{ 1, 1, 1 };
    };
    struct MotionTrack
    {
        std::string clip;
        SceneAudio::Timeline sounds;
    };
    constexpr size_t playerMotionCount = static_cast<size_t>(PlayerActionKind::count);
    struct Placement
    {
        Kind kind;
        Transform transform;
        std::string asset;
        bool collider = false;
        bool gravity = false;
        std::vector<std::pair<std::string, Enemy::HurtboxAdjustment>> hurtboxes;
        std::array<MotionTrack, playerMotionCount> motions;
        Enemy::AttackSettings attack = Enemy::defaultAttackSettings(kind == Kind::Boss);
    };
    struct Entry
    {
        Placement placement;
        GameObject* object;
        float fallSpeed = 0.0f;
    };

    class StaticProp final : public GameObject
    {
    public:
        XMFLOAT3 halfExtents() const { return m_model->staticHalfExtents(); }
        bool load(const std::string& path)
        {
            m_model = AddGameComponent<AnimationModel>(this);
            if (!m_model->tryLoad(path.c_str(), true)) return false;
            Renderer::CreateVertexShader(m_vs.GetAddressOf(), m_layout.GetAddressOf(), "shader\\litTextureVS.cso");
            Renderer::CreatePixelShader(m_ps.GetAddressOf(), "shader\\litTexturePS.cso");
            return m_vs && m_layout && m_ps;
        }
        void Draw() override
        {
            if (!m_vs || !m_layout || !m_ps) return;
            auto* context = Renderer::GetDeviceContext();
            context->IASetInputLayout(m_layout.Get());
            context->VSSetShader(m_vs.Get(), nullptr, 0);
            context->PSSetShader(m_ps.Get(), nullptr, 0);
            const float halfHeight = m_model->staticHalfExtents().y;
            m_model->drawAt(XMMatrixTranslation(0, halfHeight, 0) * GetMatrx());
        }
    private:
        AnimationModel* m_model = nullptr;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_layout;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    };

    Player* player = nullptr;
    MeshField* terrain = nullptr;
    std::vector<Entry> entries;
    std::vector<std::string> models;
    std::string status;
    int selected = -1;
    bool open = false;
    bool terrainMode = false;
    int brushMode = 0;
    float radius = 5.0f;
    float strength = 4.0f;
    float flattenHeight = 0.0f;
    Vector3 focus{ 0, 1, 6 };
    float yaw = 0.0f;
    float pitch = 0.45f;
    float orbitDistance = 19.0f;
    bool painting = false;
    bool brushRecorded = false;
    bool numericRecorded = false;
    bool showHurtboxes = false;
    bool showAttackRanges = false;
    int activeHurtbox = 0;

    struct AttackField
    {
        const char* label;
        float Enemy::AttackSettings::* value;
        float minimum, maximum;
        bool bossOnly = false;
    };
    constexpr AttackField attackFields[] = {
        { "Detection distance", &Enemy::AttackSettings::detectionDistance, 0.0f, 100.0f },
        { "Attack interval (s)", &Enemy::AttackSettings::attackInterval, 0.0f, 60.0f },
        { "Punch start range", &Enemy::AttackSettings::punchStartRange, 0.1f, 30.0f },
        { "Punch repeat interval (s)", &Enemy::AttackSettings::punchRepeatInterval, 0.0f, 60.0f, true },
        { "Slam start range", &Enemy::AttackSettings::slamStartRange, 0.1f, 30.0f, true },
        { "Charge start range", &Enemy::AttackSettings::chargeStartRange, 0.1f, 30.0f, true },
        { "Charge travel distance", &Enemy::AttackSettings::chargeDistance, 0.0f, 30.0f, true },
        { "Shockwave max radius", &Enemy::AttackSettings::shockwaveRadius, 0.1f, 30.0f, true },
    };

    enum class UndoKind { Modify, Add, Remove, Terrain, Audio };
    struct EditRecord
    {
        UndoKind kind;
        size_t index = 0;
        Placement previous{};
        std::unique_ptr<MeshField::Heights> heights;
        SceneAudio::Setting audio;
    };
    std::deque<EditRecord> history;
    bool audioPanel = false;
    int audioCue = 0;
    std::vector<std::string> audioAssets;
    std::string audioStatus;
    bool audioVolumeRecorded = false;
    bool motionPanel = false;
    int motionSlot = 0;
    float motionTime = 0.0f;
    bool motionPlaying = false;
    bool motionWasSampled = false;
    bool motionFirstFrame = true;
    bool motionLoop = false;
    int draggedMarker = -1;
    std::string motionStatus;

    void remember(EditRecord edit)
    {
        constexpr size_t limit = 64;
        if (history.size() == limit) history.pop_front();
        history.push_back(std::move(edit));
    }

    struct AxisDrag
    {
        int axis = -1;
        ImVec2 mouse;
        ImVec2 unit;
        float screenUnits = 1.0f;
        Vector3 start{ 0, 0, 0 };
        bool recorded = false;
    } drag;

    float& axisValue(Vector3& point, int axis)
    {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    }
    XMVECTOR vec(const Vector3& p) { return XMVectorSet(p.x, p.y, p.z, 0); }
    Vector3 vec(FXMVECTOR v) { return { XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v) }; }

    bool isEnemy(Kind kind) { return kind == Kind::Enemy || kind == Kind::Boss; }
    bool modelPath(const std::string& text, std::string& normalized);
    void scanAudio();

    size_t motionCount(Kind kind)
    {
        if (kind == Kind::Player) return playerMotionCount;
        return isEnemy(kind) ? Enemy::MotionCount : 0;
    }

    void applyMotion(Entry& entry)
    {
        for (size_t i = 0; i < motionCount(entry.placement.kind); ++i)
        {
            const auto& track = entry.placement.motions[i];
            const auto playerSlot = static_cast<PlayerActionKind>(i);
            const auto enemySlot = static_cast<Enemy::Motion>(i);
            auto* enemy = isEnemy(entry.placement.kind) ? static_cast<Enemy*>(entry.object) : nullptr;
            if (enemy)
            {
                std::string clip;
                if ((!track.clip.empty() && !modelPath(track.clip, clip)) || !enemy->assignMotion(enemySlot, clip))
                {
                    motionStatus = "A saved animation is unavailable for this enemy.";
                    enemy->assignMotion(enemySlot, {});
                }
            }
            auto* playerActor = enemy ? nullptr : static_cast<Player*>(entry.object);
            const float duration = enemy ? enemy->motionDuration(enemySlot) : playerActor->motionDuration(playerSlot);
            SceneAudio::Timeline ready;
            for (const auto& marker : track.sounds)
                if (duration > 0 && marker.time <= duration && SceneAudio::prepareMarker(marker.file))
                    ready.push_back(marker);
            if (enemy) enemy->setMotionSounds(enemySlot, ready);
            else playerActor->setMotionSounds(playerSlot, ready);
        }
    }

    void stopMotionPreview()
    {
        if (motionWasSampled && selected >= 0)
        {
            const auto& entry = entries[selected];
            if (isEnemy(entry.placement.kind)) static_cast<Enemy*>(entry.object)->clearMotionPreview();
            else if (entry.placement.kind == Kind::Player) static_cast<Player*>(entry.object)->clearMotionPreview();
        }
        motionWasSampled = false;
        motionPlaying = false;
        motionFirstFrame = true;
        draggedMarker = -1;
        SceneAudio::stopMarkers();
    }

    void apply(Entry& entry)
    {
        const auto& t = entry.placement.transform;
        entry.object->SetPosition(t.position);
        entry.object->SetRotation(t.rotation);
        entry.object->SetScale(t.scale);
        if (isEnemy(entry.placement.kind))
        {
            auto* enemy = static_cast<Enemy*>(entry.object);
            enemy->SetMeshCollider(entry.placement.collider);
            enemy->setAttackSettings(entry.placement.attack);
            for (size_t i = 0; i < enemy->hurtboxCount(); ++i)
            {
                const auto& bone = enemy->hurtboxBone(i);
                const auto& edits = entry.placement.hurtboxes;
                const auto edit = std::find_if(edits.begin(), edits.end(), [&](const auto& item) { return item.first == bone; });
                enemy->setHurtboxAdjustment(i, edit != edits.end() ? edit->second : Enemy::HurtboxAdjustment{});
            }
        }
        entry.fallSpeed = 0.0f;
    }

    bool supportsPhysics(Kind kind)
    {
        return kind == Kind::Model || kind == Kind::Box || kind == Kind::Grass || kind == Kind::Particle;
    }

    BoundingOrientedBox bounds(const Entry& entry)
    {
        Vector3 center{ 0, 1, 0 };
        Vector3 half{ 1, 1, 1 };
        switch (entry.placement.kind)
        {
        case Kind::Model:
        {
            const auto size = static_cast<StaticProp*>(entry.object)->halfExtents();
            center.y = size.y;
            half = { size.x, size.y, size.z };
            break;
        }
        case Kind::Player: center.y = 0.9f; half = { 0.48f, 0.9f, 0.42f }; break;
        case Kind::Enemy:
        case Kind::Boss: center.y = 1.1f; half = { 1.65f, 1.1f, 0.68f }; break;
        case Kind::Grass: half.z = 0.10f; break;
        case Kind::Particle: center.y = 0.0f; half = { 0.4f, 0.45f, 0.4f }; break;
        case Kind::Box: break;
        }
        const Vector3& scale = entry.object->GetScale();
        const Vector3& rotation = entry.object->GetRotation();
        const XMMATRIX world = entry.object->GetMatrx();
        BoundingOrientedBox result;
        XMStoreFloat3(&result.Center, XMVector3TransformCoord(vec(center), world));
        result.Extents = { std::max(.03f, half.x * scale.x), std::max(.03f, half.y * scale.y),
            std::max(.03f, half.z * scale.z) };
        XMStoreFloat4(&result.Orientation, XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
        return result;
    }

    bool rayHit(const BoundingOrientedBox& box, FXMVECTOR origin, FXMVECTOR direction, float& hit)
    {
        if (box.Contains(origin) != DISJOINT)
        {
            hit = 0.0f;
            return true;
        }
        return box.Intersects(origin, direction, hit) && hit >= 0.0f;
    }

    template<class Visitor>
    void forEachCollider(const GameObject* exclude, const Visitor& visit)
    {
        for (const auto& entry : entries)
            if (entry.placement.collider && entry.object != exclude && !isEnemy(entry.placement.kind))
                visit(bounds(entry));
    }

    float floorHit(float x, float z, float startY, float candidate, const GameObject* exclude = nullptr)
    {
        const XMVECTOR origin = XMVectorSet(x, startY, z, 1.0f);
        const XMVECTOR down = XMVectorSet(0, -1, 0, 0);
        forEachCollider(exclude, [&](const BoundingOrientedBox& box)
        {
            float hit = 0.0f;
            if (box.Contains(origin) == DISJOINT && rayHit(box, origin, down, hit))
                candidate = std::max(candidate, startY - hit);
        });
        return candidate;
    }

    bool modelExtension(const fs::path& path)
    {
        auto extension = path.extension().wstring();
        for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
        return extension == L".fbx" || extension == L".obj" || extension == L".gltf" || extension == L".glb";
    }

    bool modelPath(const std::string& text, std::string& normalized)
    {
        std::error_code error;
        try
        {
            const auto path = fs::u8path(text);
            if (!modelExtension(path) || !fs::is_regular_file(path, error) || error) return false;
            const auto relative = fs::relative(path, fs::path("asset"), error);
            if (error || relative.empty() || relative.is_absolute() || *relative.begin() == L"..") return false;
            normalized = (fs::path("asset") / relative).generic_u8string();
            return true;
        }
        catch (const std::system_error&)
        {
            return false;
        }
    }

    bool spawn(const Placement& placement)
    {
        if (entries.size() >= maxObjects) return false;
        GameObject* object = nullptr;
        switch (placement.kind)
        {
        case Kind::Player: object = player; break;
        case Kind::Enemy:
        case Kind::Boss:
        {
            auto* enemy = Manager::AddGameObj<Enemy>();
            enemy->Configure(placement.kind == Kind::Boss);
            if (!enemy->loadCharacter(placement.asset))
            {
                enemy->SetDestory();
                return false;
            }
            object = enemy;
            break;
        }
        case Kind::Box: object = Manager::AddGameObj<Box>(); break;
        case Kind::Grass: object = Manager::AddGameObj<Grass>(); break;
        case Kind::Particle: object = Manager::AddGameObj<Particle>(); break;
        case Kind::Model:
        {
            auto* prop = Manager::AddGameObj<StaticProp>();
            if (!prop->load(placement.asset))
            {
                prop->SetDestory();
                return false;
            }
            object = prop;
            break;
        }
        }
        entries.push_back({ placement, object });
        apply(entries.back());
        applyMotion(entries.back());
        return true;
    }

    void defaults()
    {
        spawn({ Kind::Player, { player->GetPosition(), player->GetRotation(), player->GetScale() }, {} });
        spawn({ Kind::Enemy, { { -2.4f, 0, 5.4f }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {}, true });
        spawn({ Kind::Enemy, { { 0, 0, 5 }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {}, true });
        spawn({ Kind::Enemy, { { 2.4f, 0, 5.4f }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {}, true });
        spawn({ Kind::Boss, { { -1, 0, 15 }, { 0, XM_PI, 0 }, { 1.55f, 1.55f, 1.55f } }, {}, true });
        spawn({ Kind::Box, { { 5, 0, 5 }, { 0, 0, 0 }, { 2, 1, 2 } }, {}, true });
        spawn({ Kind::Grass, { { 10, 0, -5 } }, {} });
        spawn({ Kind::Particle, { { -2, 1, -1 } }, {} });
    }

    bool readVector(std::istream& input, Vector3& v)
    {
        return (input >> v.x >> v.y >> v.z)
            && std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    void writeVector(std::ostream& output, const Vector3& v)
    {
        output << v.x << ' ' << v.y << ' ' << v.z << ' ';
    }

    bool readHurtboxEdits(std::istream& input, std::vector<std::pair<std::string, Enemy::HurtboxAdjustment>>& edits)
    {
        size_t count = 0;
        if (!(input >> count) || count > 64) return false;
        for (size_t i = 0; i < count; ++i)
        {
            std::string bone;
            Enemy::HurtboxAdjustment edit;
            if (!(input >> std::quoted(bone)) || !readVector(input, edit.offset)
                || !(input >> edit.radiusScale >> edit.lengthScale)
                || !(edit.radiusScale >= 0.1f && edit.radiusScale <= 4.0f)
                || !(edit.lengthScale >= 0.1f && edit.lengthScale <= 4.0f)) return false;
            edits.emplace_back(std::move(bone), edit);
        }
        return true;
    }

    bool loadLayout()
    {
        std::ifstream input(layoutPath);
        std::string signature;
        int version = 0, size = 0;
        size_t count = 0;
        if (!(input >> signature >> version >> count >> size)
            || signature != "SceneLayout" || (version < 1 || version > layoutVersion) || count > maxObjects
            || size != MeshField::GridSize) return false;

        std::vector<Placement> placements;
        int playerCount = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Placement placement{};
            int kind = 0;
            auto& t = placement.transform;
            if (!(input >> kind) || kind < 0 || kind > static_cast<int>(Kind::Model)
                || !readVector(input, t.position) || !readVector(input, t.rotation) || !readVector(input, t.scale)
                || !(input >> std::quoted(placement.asset))
                || t.scale.x < .01f || t.scale.y < .01f || t.scale.z < .01f) return false;
            placement.kind = static_cast<Kind>(kind);
            placement.attack = Enemy::defaultAttackSettings(placement.kind == Kind::Boss);
            placement.collider = placement.kind == Kind::Box || isEnemy(placement.kind);
            if (version >= 2)
            {
                int collider = 0, gravity = 0;
                if (!(input >> collider >> gravity) || (collider != 0 && collider != 1)
                    || (gravity != 0 && gravity != 1)) return false;
                if (!isEnemy(placement.kind) || version >= 3)
                    placement.collider = (supportsPhysics(placement.kind) || isEnemy(placement.kind)) && collider != 0;
                placement.gravity = supportsPhysics(placement.kind) && gravity != 0;
            }
            if (placement.kind == Kind::Player) ++playerCount;
            if ((placement.kind == Kind::Model || (isEnemy(placement.kind) && !placement.asset.empty()))
                && !modelPath(placement.asset, placement.asset)) return false;
            if (version >= 5 && !readHurtboxEdits(input, placement.hurtboxes)) return false;
            if (version >= 6)
            {
                for (size_t slot = 0; slot < motionCount(placement.kind); ++slot)
                {
                    auto& track = placement.motions[slot];
                    size_t markers = 0;
                    if (!(input >> std::quoted(track.clip) >> markers) || markers > 24) return false;
                    for (size_t j = 0; j < markers; ++j)
                    {
                        SceneAudio::Marker marker;
                        if (!(input >> marker.time >> marker.volume >> std::quoted(marker.file))
                            || !(marker.time >= 0 && marker.time <= 3600)
                            || !(marker.volume >= 0 && marker.volume <= 1)) return false;
                        track.sounds.push_back(std::move(marker));
                    }
                }
            }
            if (version >= 7 && isEnemy(placement.kind))
            {
                for (const auto& field : attackFields)
                {
                    auto& value = placement.attack.*field.value;
                    if (!(input >> value) || !(value >= field.minimum && value <= field.maximum)) return false;
                }
            }
            placements.push_back(placement);
        }
        MeshField::Heights heights{};
        for (float& height : heights)
            if (!(input >> height) || !std::isfinite(height) || fabsf(height) > 30.0f) return false;
        if (playerCount != 1) return false;
        if (version == 4)
        {
            std::vector<std::pair<std::string, Enemy::HurtboxAdjustment>> legacyPlayerEdits;
            if (!readHurtboxEdits(input, legacyPlayerEdits)) return false;
        }

        for (const auto& placement : placements)
        {
            if (placement.kind != Kind::Player && !spawn(placement))
            {
                for (const auto& entry : entries) entry.object->SetDestory();
                entries.clear();
                return false;
            }
        }
        for (const auto& placement : placements)
            if (placement.kind == Kind::Player) spawn(placement);
        std::rotate(entries.begin(), entries.end() - 1, entries.end());
        terrain->setHeights(heights);
        return true;
    }

    bool saveLayout()
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output) return false;
        output << "SceneLayout " << layoutVersion << ' ' << entries.size() << ' ' << MeshField::GridSize << '\n';
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& entry : entries)
        {
            const auto& p = entry.placement;
            output << static_cast<int>(p.kind) << ' ';
            writeVector(output, p.transform.position);
            writeVector(output, p.transform.rotation);
            writeVector(output, p.transform.scale);
            output << std::quoted(p.asset) << ' ' << p.collider << ' ' << p.gravity << '\n';
            output << p.hurtboxes.size() << '\n';
            for (const auto& [bone, edit] : p.hurtboxes)
            {
                output << std::quoted(bone) << ' ';
                writeVector(output, edit.offset);
                output << edit.radiusScale << ' ' << edit.lengthScale << '\n';
            }
            for (size_t slot = 0; slot < motionCount(p.kind); ++slot)
            {
                const auto& track = p.motions[slot];
                output << std::quoted(track.clip) << ' ' << track.sounds.size() << '\n';
                for (const auto& marker : track.sounds)
                    output << marker.time << ' ' << marker.volume << ' ' << std::quoted(marker.file) << '\n';
            }
            if (isEnemy(p.kind))
            {
                for (const auto& field : attackFields)
                    output << p.attack.*field.value << (&field == &attackFields[std::size(attackFields) - 1] ? '\n' : ' ');
            }
        }
        const auto& heights = terrain->heights();
        for (size_t i = 0; i < heights.size(); ++i)
            output << heights[i] << ((i + 1) % MeshField::GridSize ? ' ' : '\n');
        output.close();
        return output && MoveFileExA(temporaryPath, layoutPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    void refreshModels()
    {
        models.clear();
        std::error_code error;
        fs::recursive_directory_iterator it("asset", fs::directory_options::skip_permission_denied, error), end;
        while (!error && it != end)
        {
            std::error_code entryError;
            if (it->is_regular_file(entryError) && modelExtension(it->path()))
                models.push_back(it->path().generic_u8string());
            it.increment(error);
        }
        std::sort(models.begin(), models.end());
    }

    void select(int index)
    {
        stopMotionPreview();
        selected = index;
        motionSlot = 0;
        motionTime = 0;
        drag.axis = -1;
        numericRecorded = false;
        activeHurtbox = 0;
    }

    void undo()
    {
        if (history.empty()) return;
        stopMotionPreview();
        const auto& edit = history.back();
        switch (edit.kind)
        {
        case UndoKind::Modify:
            if (isEnemy(edit.previous.kind)
                && !static_cast<Enemy*>(entries[edit.index].object)->loadCharacter(edit.previous.asset))
            {
                status = "Could not restore the previous model.";
                return;
            }
            entries[edit.index].placement = edit.previous;
            apply(entries[edit.index]);
            applyMotion(entries[edit.index]);
            select(static_cast<int>(edit.index));
            break;
        case UndoKind::Add:
            stopMotionPreview();
            entries[edit.index].object->SetDestory();
            entries.erase(entries.begin() + edit.index);
            select(-1);
            break;
        case UndoKind::Remove:
            if (!spawn(edit.previous))
            {
                status = "Could not restore the object.";
                return;
            }
            std::rotate(entries.begin() + edit.index, entries.end() - 1, entries.end());
            select(static_cast<int>(edit.index));
            break;
        case UndoKind::Terrain:
            terrain->setHeights(*edit.heights);
            break;
        case UndoKind::Audio:
        {
            const auto cue = static_cast<SceneAudio::Cue>(edit.index);
            if (SceneAudio::setting(cue).file != edit.audio.file && !SceneAudio::assign(cue, edit.audio.file))
            {
                audioStatus = "Could not restore the previous audio file.";
                return;
            }
            SceneAudio::setVolume(cue, edit.audio.volume);
            SceneAudio::setEnabled(cue, edit.audio.enabled);
            audioCue = static_cast<int>(edit.index);
            audioStatus = "Undid the last audio edit.";
            break;
        }
        }
        drag.axis = -1;
        painting = false;
        history.pop_back();
        status = "Undid the last edit.";
    }

    bool recordNumeric(bool changed, const Placement& before)
    {
        if (ImGui::IsItemActivated()) numericRecorded = false;
        if (changed && !numericRecorded)
        {
            remember({ UndoKind::Modify, static_cast<size_t>(selected), before });
            numericRecorded = true;
        }
        if (ImGui::IsItemDeactivated()) numericRecorded = false;
        return changed;
    }

    void editEnemyAttacks(Entry& entry)
    {
        if (!ImGui::CollapsingHeader("Enemy attack settings")) return;
        const bool boss = entry.placement.kind == Kind::Boss;
        auto& settings = entry.placement.attack;
        for (const auto& field : attackFields)
        {
            if (field.bossOnly && !boss) continue;
            auto& setting = settings.*field.value;
            float value = setting;
            ImGui::SetNextItemWidth(110.0f);
            if (recordNumeric(ImGui::DragFloat(field.label, &value, .05f, field.minimum, field.maximum,
                "%.2f", ImGuiSliderFlags_AlwaysClamp), entry.placement) && std::isfinite(value))
                setting = value;
            if (!boss && field.value == &Enemy::AttackSettings::punchStartRange)
                ImGui::SetItemTooltip("Range at the default enemy scale (0.75).");
        }
        ImGui::Checkbox("Show attack ranges", &showAttackRanges);
        if (showAttackRanges)
            ImGui::TextWrapped("Blue: detection. Orange: attack start. Gold: charge or shockwave preview.");
        if (ImGui::Button("Preview attacks"))
        {
            if (!motionPanel) scanAudio();
            motionPanel = true;
            showAttackRanges = true;
            if (motionSlot < static_cast<int>(Enemy::Motion::Punch))
            {
                stopMotionPreview();
                motionSlot = static_cast<int>(Enemy::Motion::Punch);
                motionTime = 0;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset attack settings"))
        {
            remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
            settings = Enemy::defaultAttackSettings(boss);
        }
    }

    void editEnemyCharacter(Entry& entry)
    {
        if (!ImGui::CollapsingHeader("Enemy character and hurtboxes")) return;
        auto* enemy = static_cast<Enemy*>(entry.object);
        ImGui::TextWrapped("%s", entry.placement.asset.empty() ? "Built-in enemy mesh" : entry.placement.asset.c_str());
        static char path[1024]{};
        ImGui::InputTextWithHint("Character path", "asset/model/enemy.fbx", path, IM_ARRAYSIZE(path));
        const bool assign = ImGui::Button("Assign rigged model");
        ImGui::SameLine();
        ImGui::BeginDisabled(!enemy->hasCharacter());
        const bool reset = ImGui::Button("Use built-in mesh");
        ImGui::EndDisabled();
        if (assign || reset)
        {
            std::string asset;
            if ((!assign || modelPath(path, asset))
                && enemy->loadCharacter(asset))
            {
                if (entry.placement.asset != asset)
                {
                    remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
                    entry.placement.asset = asset;
                    entry.placement.hurtboxes.clear();
                    for (auto& track : entry.placement.motions) track.clip.clear();
                    applyMotion(entry);
                    activeHurtbox = 0;
                }
                status = assign ? "Enemy model assigned." : "Built-in mesh restored.";
            }
            else status = "An asset/ model with weighted bones is required.";
        }
        if (!enemy->hurtboxCount()) return;
        ImGui::Checkbox("Show hurtboxes", &showHurtboxes);
        if (activeHurtbox >= static_cast<int>(enemy->hurtboxCount())) activeHurtbox = 0;
        if (ImGui::BeginCombo("Bone", enemy->hurtboxBone(activeHurtbox).c_str()))
        {
            for (size_t i = 0; i < enemy->hurtboxCount(); ++i)
                if (ImGui::Selectable(enemy->hurtboxBone(i).c_str(), activeHurtbox == static_cast<int>(i)))
                    activeHurtbox = static_cast<int>(i);
            ImGui::EndCombo();
        }

        const auto& bone = enemy->hurtboxBone(activeHurtbox);
        auto& edits = entry.placement.hurtboxes;
        auto item = std::find_if(edits.begin(), edits.end(), [&](const auto& e) { return e.first == bone; });
        auto edit = item == edits.end() ? Enemy::HurtboxAdjustment{} : item->second;
        float offset[] = { edit.offset.x, edit.offset.y, edit.offset.z };
        if (recordNumeric(ImGui::DragFloat3("Offset (bind XYZ)", offset, .005f, -1, 1,
            "%.3f", ImGuiSliderFlags_AlwaysClamp), entry.placement))
            edit.offset = { offset[0], offset[1], offset[2] };
        recordNumeric(ImGui::SliderFloat("Radius scale", &edit.radiusScale, .1f, 4, "%.2f",
            ImGuiSliderFlags_AlwaysClamp), entry.placement);
        recordNumeric(ImGui::SliderFloat("Length scale", &edit.lengthScale, .1f, 4, "%.2f",
            ImGuiSliderFlags_AlwaysClamp), entry.placement);
        if (ImGui::Button("Reset part") && item != edits.end())
        {
            remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
            edits.erase(item);
        }
        else if (item != edits.end()) item->second = edit;
        else if (edit.offset != Vector3{ 0, 0, 0 } || edit.radiusScale != 1.0f || edit.lengthScale != 1.0f)
            edits.emplace_back(bone, edit);
    }
    void scanAudio()
    {
        audioAssets.clear();
        std::error_code error;
        fs::recursive_directory_iterator it("asset/Audio", fs::directory_options::skip_permission_denied, error), end;
        while (!error && it != end)
        {
            auto extension = it->path().extension().wstring();
            for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
            std::error_code entryError;
            if (extension == L".wav" && it->is_regular_file(entryError))
                audioAssets.push_back(it->path().generic_u8string());
            it.increment(error);
        }
        std::sort(audioAssets.begin(), audioAssets.end());
    }

    bool drawMotionTimeline(Entry& entry, SceneAudio::Timeline& sounds, float duration, Enemy::AttackPhase phase)
    {
        const float width = std::max(24.0f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("##motionTimeline", { width, 48 });
        const ImVec2 start = ImGui::GetItemRectMin();
        const ImVec2 end = ImGui::GetItemRectMax();
        const auto mouse = ImGui::GetIO().MousePos;
        const float left = start.x + 8;
        const float right = end.x - 8;
        const float rulerY = start.y + 22;
        const float markerY = start.y + 36;
        const auto xAt = [&](float time)
        {
            return left + (right - left) * std::clamp(time / duration, 0.0f, 1.0f);
        };
        int hoveredMarker = -1;
        if (ImGui::IsItemHovered() && mouse.y > rulerY)
        {
            float distance = 9.0f;
            for (size_t i = 0; i < sounds.size(); ++i)
            {
                const float delta = fabsf(mouse.x - xAt(sounds[i].time));
                if (delta < distance)
                {
                    distance = delta;
                    hoveredMarker = static_cast<int>(i);
                }
            }
        }

        const bool clicked = ImGui::IsItemActivated();
        if (clicked)
        {
            draggedMarker = hoveredMarker;
            motionPlaying = false;
            SceneAudio::stopMarkers();
        }
        const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        const float mouseTime = std::clamp((mouse.x - left) / (right - left), 0.0f, 1.0f) * duration;
        bool moved = false;
        if (draggedMarker >= 0 && dragging)
        {
            auto& marker = sounds[draggedMarker];
            moved = recordNumeric(mouseTime != marker.time, entry.placement);
            if (moved) marker.time = mouseTime;
        }
        if (clicked || dragging)
        {
            motionTime = draggedMarker >= 0 ? std::min(sounds[draggedMarker].time, duration) : mouseTime;
            motionFirstFrame = motionTime == 0;
        }
        if (!ImGui::IsItemActive()) draggedMarker = -1;

        auto* drawing = ImGui::GetWindowDrawList();
        drawing->AddRectFilled(start, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 tick = ImGui::GetColorU32(ImGuiCol_Border);
        drawing->AddLine({ left, rulerY }, { right, rulerY }, tick);
        if (phase.name)
            drawing->AddLine({ xAt(phase.start), rulerY + 5 }, { xAt(phase.end), rulerY + 5 },
                IM_COL32(255, 157, 65, 255), 3.0f);
        for (int i = 0; i <= 4; ++i)
        {
            const float seconds = duration * i / 4;
            const float x = xAt(seconds);
            drawing->AddLine({ x, rulerY - 4 }, { x, rulerY + 3 }, tick);
            char label[32];
            snprintf(label, sizeof(label), "%.2f", seconds);
            const float labelWidth = ImGui::CalcTextSize(label).x;
            if (i == 0 || i == 4 || (right - left) / 4 > labelWidth + 12)
                drawing->AddText({ std::clamp(x - labelWidth / 2, left, std::max(left, right - labelWidth)),
                    start.y + 2 }, text, label);
        }
        const float cursor = xAt(motionTime);
        drawing->AddLine({ cursor, rulerY }, { cursor, end.y }, ImGui::GetColorU32(ImGuiCol_SliderGrabActive), 2);
        for (size_t i = 0; i < sounds.size(); ++i)
        {
            const auto& marker = sounds[i];
            const int index = static_cast<int>(i);
            const float x = xAt(marker.time);
            const ImU32 color = index == hoveredMarker || index == draggedMarker ? text
                : marker.file.empty() || marker.time > duration ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                : IM_COL32(255, 191, 88, 255);
            drawing->AddQuadFilled({ x, markerY - 6 }, { x + 6, markerY }, { x, markerY + 6 }, { x - 6, markerY }, color);
        }
        if (hoveredMarker >= 0 && !dragging)
        {
            const auto& marker = sounds[hoveredMarker];
            ImGui::SetTooltip("%.3f s | %s", marker.time, marker.file.empty() ? "Choose WAV" : marker.file.c_str());
        }
        if (moved) applyMotion(entry);
        return clicked || dragging;
    }

    void drawMotionPanel()
    {
        if (!motionPanel) return;
        ImGui::SetNextWindowPos({ 390, 360 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 650, 350 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Motion & SE", &motionPanel))
        {
            if (selected < 0 || !motionCount(entries[selected].placement.kind))
                ImGui::TextUnformatted("Select the player or an enemy in the scene.");
            else
            {
                auto& entry = entries[selected];
                const bool enemy = isEnemy(entry.placement.kind);
                const char* playerMotions[] = { "Idle", "Walk", "Run", "Yawn", "Attack", "Jump", "Running jump", "Land" };
                const char* enemyMotions[] = { "Idle", "Move", "Punch", "Slam", "Charge" };
                if (ImGui::Combo("Motion", &motionSlot, enemy ? enemyMotions : playerMotions,
                    static_cast<int>(motionCount(entry.placement.kind))))
                {
                    stopMotionPreview();
                    motionTime = 0;
                }
                auto& track = entry.placement.motions[motionSlot];
                const auto enemySlot = static_cast<Enemy::Motion>(motionSlot);
                const auto playerSlot = static_cast<PlayerActionKind>(motionSlot);
                auto* actor = enemy ? static_cast<Enemy*>(entry.object) : nullptr;
                const auto rememberMotion = [&]()
                {
                    remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
                };
                if (enemy)
                {
                    if (actor->hasCharacter())
                    {
                        ImGui::TextWrapped("Clip: %s", track.clip.empty() ? "Embedded / bind pose" : track.clip.c_str());
                        static char path[1024]{};
                        ImGui::InputTextWithHint("Clip file", "asset/model/EnemyWalk.fbx", path, IM_ARRAYSIZE(path));
                        const bool assign = ImGui::Button("Assign clip");
                        ImGui::SameLine();
                        const bool clear = ImGui::Button("Use default clip");
                        if (assign || clear)
                        {
                            std::string file;
                            if ((!assign || modelPath(path, file)) && actor->assignMotion(enemySlot, file))
                            {
                                if (track.clip != file)
                                {
                                    rememberMotion();
                                    track.clip = std::move(file);
                                    stopMotionPreview();
                                    motionTime = 0;
                                }
                                motionStatus = "Motion updated.";
                            }
                            else motionStatus = "Use a clip under asset/ with compatible bone names.";
                        }
                    }
                    else ImGui::TextUnformatted("Built-in motion. Assign a rigged model to replace clips.");
                }
                const float duration = enemy ? actor->motionDuration(enemySlot) : player->motionDuration(playerSlot);
                const auto sample = [&]()
                {
                    if (enemy) actor->previewMotion(enemySlot, motionTime);
                    else player->previewMotion(playerSlot, motionTime);
                    motionWasSampled = true;
                };
                if (duration > 0.0f)
                {
                    motionTime = std::min(motionTime, duration);
                    const bool canAttack = enemy && (enemySlot == Enemy::Motion::Punch || entry.placement.kind == Kind::Boss);
                    const auto phase = canAttack ? Enemy::attackPhase(enemySlot) : Enemy::AttackPhase{};
                    if (drawMotionTimeline(entry, track.sounds, duration, phase)) sample();
                    if (phase.name) ImGui::Text("%s  %.2f - %.2f s", phase.name, phase.start, phase.end);
                    if (ImGui::SliderFloat("Time (s)", &motionTime, 0, duration, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                    {
                        motionPlaying = false;
                        motionFirstFrame = motionTime == 0;
                        SceneAudio::stopMarkers();
                        sample();
                    }
                    if (ImGui::Button(motionPlaying ? "Pause" : "Play"))
                    {
                        motionPlaying = !motionPlaying;
                        if (motionPlaying && motionTime >= duration) { motionTime = 0; motionFirstFrame = true; }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stop motion")) { stopMotionPreview(); motionTime = 0; }
                    ImGui::SameLine();
                    if (ImGui::Button("+1 frame"))
                    {
                        motionPlaying = false;
                        motionTime = std::min(duration, motionTime + 1.0f / 60.0f);
                        motionFirstFrame = false;
                        sample();
                    }
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop preview", &motionLoop);
                    if (motionPlaying)
                    {
                        const float previous = motionFirstFrame ? -1.0f : motionTime;
                        motionFirstFrame = false;
                        const float next = motionTime + std::min(ImGui::GetIO().DeltaTime, 0.05f);
                        const bool wrapped = motionLoop && next >= duration;
                        motionTime = wrapped ? fmodf(next, duration) : std::min(next, duration);
                        SceneAudio::playMarkers(track.sounds, previous, motionTime, wrapped ? duration : 0.0f, true);
                        sample();
                        if (!motionLoop && motionTime >= duration) motionPlaying = false;
                    }
                }
                else ImGui::TextUnformatted("This player motion is not available.");

                if (ImGui::Button("Rescan WAV files")) scanAudio();
                ImGui::SameLine();
                ImGui::BeginDisabled(duration <= 0 || track.sounds.size() >= 24);
                if (ImGui::Button("Add SE at cursor"))
                {
                    rememberMotion();
                    track.sounds.push_back({ motionTime, {}, 1.0f });
                    applyMotion(entry);
                }
                ImGui::EndDisabled();
                bool changed = false;
                for (size_t i = 0; i < track.sounds.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    auto& marker = track.sounds[i];
                    float time = marker.time, volume = marker.volume;
                    ImGui::SetNextItemWidth(90);
                    if (recordNumeric(ImGui::DragFloat("##time", &time, .005f, 0,
                        std::max(duration, marker.time), "%.3f s", ImGuiSliderFlags_AlwaysClamp), entry.placement))
                    {
                        marker.time = time;
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(240);
                    if (ImGui::BeginCombo("##file", marker.file.empty() ? "Choose WAV" : marker.file.c_str()))
                    {
                        for (const auto& file : audioAssets)
                            if (ImGui::Selectable(file.c_str(), marker.file == file) && marker.file != file)
                            {
                                if (SceneAudio::prepareMarker(file))
                                {
                                    rememberMotion();
                                    marker.file = file;
                                    changed = true;
                                }
                                else motionStatus = "Could not load this WAV.";
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    if (recordNumeric(ImGui::SliderFloat("##gain", &volume, 0, 1, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp), entry.placement))
                    {
                        marker.volume = volume;
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Preview")) SceneAudio::playMarker(marker, true);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Delete"))
                    {
                        rememberMotion();
                        track.sounds.erase(track.sounds.begin() + i);
                        ImGui::PopID();
                        changed = true;
                        break;
                    }
                    ImGui::PopID();
                }
                if (changed) applyMotion(entry);
                if (!motionStatus.empty()) ImGui::TextWrapped("%s", motionStatus.c_str());
            }
        }
        ImGui::End();
        if (!motionPanel) stopMotionPreview();
    }

    void drawAudioPanel()
    {
        if (!audioPanel) return;
        ImGui::SetNextWindowPos({ 382, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 390, 320 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Audio", &audioPanel))
        {
            constexpr const char* names[] = { "BGM", "Player jump", "Player landing", "Enemy punch",
                "Boss slam", "Boss charge" };
            static_assert(IM_ARRAYSIZE(names) == static_cast<int>(SceneAudio::Cue::Count), "Audio cue labels");
            if (ImGui::Combo("Cue", &audioCue, names, IM_ARRAYSIZE(names))) SceneAudio::stopPreview();
            const auto cue = static_cast<SceneAudio::Cue>(audioCue);
            const auto rememberAudio = [&]()
            {
                remember({ UndoKind::Audio, static_cast<size_t>(audioCue), {}, {}, SceneAudio::setting(cue) });
            };
            const auto& setting = SceneAudio::setting(cue);
            if (ImGui::Button("Rescan WAV files")) scanAudio();
            if (ImGui::BeginCombo("Sound", setting.file.empty() ? "None" : setting.file.c_str()))
            {
                if (ImGui::Selectable("None", setting.file.empty()) && !setting.file.empty())
                {
                    rememberAudio();
                    SceneAudio::assign(cue, {});
                    audioStatus.clear();
                }
                for (const auto& file : audioAssets)
                {
                    if (!ImGui::Selectable(file.c_str(), setting.file == file) || setting.file == file) continue;
                    const auto before = setting;
                    if (SceneAudio::assign(cue, file))
                    {
                        remember({ UndoKind::Audio, static_cast<size_t>(audioCue), {}, {}, before });
                        audioStatus = "Sound assigned.";
                    }
                    else audioStatus = "Could not load this WAV. Use PCM or compatible float WAV.";
                }
                ImGui::EndCombo();
            }
            float volume = setting.volume * 100.0f;
            const bool volumeChanged = ImGui::SliderFloat("Volume", &volume, 0.0f, 100.0f, "%.0f%%",
                ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemActivated()) audioVolumeRecorded = false;
            if (volumeChanged)
            {
                if (!audioVolumeRecorded) rememberAudio();
                audioVolumeRecorded = true;
                SceneAudio::setVolume(cue, volume / 100.0f);
            }
            if (ImGui::IsItemDeactivated()) audioVolumeRecorded = false;
            bool enabled = setting.enabled;
            ImGui::BeginDisabled(setting.file.empty());
            if (ImGui::Checkbox(cue == SceneAudio::Cue::Bgm ? "Loop during play" : "Play in game", &enabled))
            {
                rememberAudio();
                SceneAudio::setEnabled(cue, enabled);
            }
            if (ImGui::Button("Preview"))
                audioStatus = SceneAudio::preview(cue) ? "Playing preview." : "Could not play this WAV.";
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Stop")) { SceneAudio::stopPreview(); audioStatus.clear(); }
            if (ImGui::Button("Save audio"))
                audioStatus = SceneAudio::save() ? "Saved asset/scene-audio.txt." : "Could not save audio settings.";
            if (!audioStatus.empty()) ImGui::TextWrapped("%s", audioStatus.c_str());
            ImGui::TextWrapped("Place new WAV files under asset/Audio/ and rescan.");
        }
        ImGui::End();
        if (!audioPanel) SceneAudio::stopPreview();
    }

    void inspector()
    {
        ImGui::SetNextWindowPos({ 14, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 350, 620 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Editor"))
        {
            ImGui::TextWrapped("F2: edit/play   WASD: move   Shift: faster");
            ImGui::TextWrapped("LMB: select   RMB: orbit   MMB: pan   Wheel: zoom");
            if (ImGui::Button("Audio"))
            {
                audioPanel = !audioPanel;
                if (audioPanel) scanAudio();
                else SceneAudio::stopPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button("Motion & SE"))
            {
                motionPanel = !motionPanel;
                if (motionPanel) scanAudio();
                else stopMotionPreview();
            }
            if (ImGui::Button("Save layout"))
                status = saveLayout() ? "Saved asset/scene-layout.txt." : "Could not save the scene layout.";
            ImGui::SameLine();
            ImGui::BeginDisabled(history.empty());
            if (ImGui::Button("Undo (Ctrl+Z)")) undo();
            ImGui::EndDisabled();
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
            ImGui::Separator();
            ImGui::Checkbox("Terrain brush", &terrainMode);
            if (terrainMode)
            {
                ImGui::Combo("Brush", &brushMode, "Raise\0Lower\0Flatten\0");
                ImGui::SliderFloat("Radius", &radius, 2.5f, 25.0f);
                ImGui::SliderFloat("Strength", &strength, 0.5f, 15.0f);
                if (brushMode == 2) ImGui::SliderFloat("Height", &flattenHeight, -30.0f, 30.0f);
                ImGui::TextWrapped("Hold LMB on the ground to sculpt.");
            }
            else
            {
                ImGui::TextUnformatted("Objects");
                if (ImGui::BeginChild("Hierarchy", { 0, 165 }, true))
                {
                    ImGui::TextDisabled("Camera / Terrain / Sky / Score (scene systems)");
                    for (size_t i = 0; i < entries.size(); ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));
                        const auto& p = entries[i].placement;
                        auto name = p.kind == Kind::Model ? fs::u8path(p.asset).filename().u8string()
                            : std::string(labels[static_cast<int>(p.kind)]);
                        if (p.kind != Kind::Player) name += " (" + std::to_string(i) + ")";
                        if (ImGui::Selectable(name.c_str(), selected == static_cast<int>(i))) select(static_cast<int>(i));
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                if (selected >= 0)
                {
                    auto& entry = entries[selected];
                    auto& t = entry.placement.transform;
                    float position[] = { t.position.x, t.position.y, t.position.z };
                    if (recordNumeric(ImGui::DragFloat3("Position", position, 0.1f), entry.placement))
                        t.position = { position[0], position[1], position[2] };
                    if (entry.placement.kind != Kind::Player)
                    {
                        float angle[] = { XMConvertToDegrees(t.rotation.x), XMConvertToDegrees(t.rotation.y), XMConvertToDegrees(t.rotation.z) };
                        float scale[] = { t.scale.x, t.scale.y, t.scale.z };
                        if (recordNumeric(ImGui::DragFloat3("Rotation (deg)", angle, 0.5f), entry.placement))
                            t.rotation = { XMConvertToRadians(angle[0]), XMConvertToRadians(angle[1]), XMConvertToRadians(angle[2]) };
                        if (recordNumeric(ImGui::DragFloat3("Scale", scale, 0.02f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp), entry.placement))
                            t.scale = { scale[0], scale[1], scale[2] };
                    }
                    const bool enemy = isEnemy(entry.placement.kind);
                    if (supportsPhysics(entry.placement.kind) || enemy)
                    {
                        const auto before = entry.placement;
                        if (ImGui::Checkbox(enemy ? "Mesh collider (auto)" : "Collider (fit to object)", &entry.placement.collider))
                            remember({ UndoKind::Modify, static_cast<size_t>(selected), before });
                    }
                    if (supportsPhysics(entry.placement.kind))
                    {
                        const auto before = entry.placement;
                        if (ImGui::Checkbox("Use gravity", &entry.placement.gravity))
                            remember({ UndoKind::Modify, static_cast<size_t>(selected), before });
                    }
                    if (enemy)
                    {
                        editEnemyAttacks(entry);
                        editEnemyCharacter(entry);
                    }
                    apply(entry);
                    if (ImGui::Button("Focus")) focus = t.position + Vector3{ 0, 1, 0 };
                    if (entry.placement.kind != Kind::Player)
                    {
                        ImGui::SameLine();
                        const bool duplicate = ImGui::Button("Duplicate");
                        ImGui::SameLine();
                        const bool remove = ImGui::Button("Delete");
                        if (duplicate)
                        {
                            Placement copy = entry.placement;
                            copy.transform.position.x += 1.0f;
                            if (spawn(copy))
                            {
                                remember({ UndoKind::Add, entries.size() - 1 });
                                select(static_cast<int>(entries.size() - 1));
                            }
                            else status = "Could not duplicate this object.";
                        }
                        else if (remove)
                        {
                            stopMotionPreview();
                            remember({ UndoKind::Remove, static_cast<size_t>(selected), entry.placement });
                            entry.object->SetDestory();
                            entries.erase(entries.begin() + selected);
                            select(-1);
                        }
                    }
                }
                ImGui::Separator();
                static char path[1024]{};
                if (ImGui::Button("Scan models")) refreshModels();
                ImGui::SameLine();
                if (ImGui::BeginCombo("##models", "Choose a project model"))
                {
                    for (const auto& model : models)
                        if (ImGui::Selectable(model.c_str())) strncpy_s(path, model.c_str(), _TRUNCATE);
                    ImGui::EndCombo();
                }
                ImGui::InputTextWithHint("##path", "asset/model/example.fbx", path, IM_ARRAYSIZE(path));
                if (ImGui::Button("Place model"))
                {
                    Placement item{ Kind::Model, {}, {}, true };
                    item.transform.position = focus;
                    terrain->tryGetHeight(focus.x, focus.z, item.transform.position.y);
                    if (modelPath(path, item.asset) && spawn(item))
                    {
                        status = "Model added.";
                        remember({ UndoKind::Add, entries.size() - 1 });
                        select(static_cast<int>(entries.size() - 1));
                    }
                    else status = "Could not load the model. Use FBX, OBJ or glTF under asset/.";
                }
            }
        }
        ImGui::End();
    }

    bool project(const Vector3& point, FXMMATRIX vp, const ImVec2& size, ImVec2& pixel)
    {
        const XMVECTOR clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 0.01f) return false;
        pixel = { (XMVectorGetX(clip) / w + 1.0f) * size.x * 0.5f,
                  (1.0f - XMVectorGetY(clip) / w) * size.y * 0.5f };
        return true;
    }

    void drawCapsule(const Enemy::HurtboxPose& pose, FXMMATRIX viewProjection, const ImVec2& size,
        ImDrawList& overlay, ImU32 color, bool active)
    {
        const XMVECTOR start = vec(pose.start), end = vec(pose.end);
        const XMVECTOR segment = end - start;
        const XMVECTOR axis = XMVectorGetX(XMVector3LengthSq(segment)) > 0.000001f
            ? XMVector3Normalize(segment) : XMVectorSet(0, 1, 0, 0);
        const XMVECTOR u = XMVector3Normalize(XMVector3Orthogonal(axis));
        const XMVECTOR v = XMVector3Cross(axis, u);
        for (int plane = 0; plane < 2; ++plane)
        {
            const auto radial = plane == 0 ? u : v;
            ImVec2 first{}, previous{};
            bool firstVisible = false, previousVisible = false;
            for (int point = 0; point < 26; ++point)
            {
                const bool lower = point < 13;
                const float angle = (point % 13) * XM_PI / 12.0f + (lower ? XM_PI : 0.0f);
                const auto p = (lower ? start : end) + (radial * cosf(angle) + axis * sinf(angle)) * pose.radius;
                ImVec2 screen;
                const bool visible = project(vec(p), viewProjection, size, screen);
                if (visible && previousVisible) overlay.AddLine(previous, screen, color, active ? 2.0f : 1.0f);
                if (point == 0 && visible) { first = screen; firstVisible = true; }
                if (visible) previous = screen;
                previousVisible = visible;
            }
            if (firstVisible && previousVisible) overlay.AddLine(previous, first, color, active ? 2.0f : 1.0f);
        }
    }

    void drawAttackRanges(Enemy& enemy, Kind kind, FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        auto motion = static_cast<Enemy::Motion>(motionSlot);
        if (!motionPanel || !Enemy::attackPhase(motion).name || kind == Kind::Enemy)
            motion = Enemy::Motion::Punch;
        const auto phase = Enemy::attackPhase(motion);
        const bool active = motionPanel && motionWasSampled && motionTime >= phase.start && motionTime <= phase.end;
        const auto origin = enemy.GetPosition() + Vector3{ 0, 0.07f, 0 };
        const auto ring = [&](float radius, ImU32 color, float thickness)
        {
            if (radius <= 0.0f) return;
            ImVec2 previous{};
            bool wasVisible = false;
            for (int i = 0; i <= 48; ++i)
            {
                const float angle = i * XM_2PI / 48;
                const auto point = origin + Vector3{ radius * cosf(angle), 0, radius * sinf(angle) };
                ImVec2 pixel;
                const bool visible = project(point, vp, size, pixel);
                if (wasVisible && visible) overlay.AddLine(previous, pixel, color, thickness);
                if (visible) previous = pixel;
                wasVisible = visible;
            }
        };
        constexpr ImU32 gold = IM_COL32(255, 218, 95, 240);
        ring(enemy.attackSettings().detectionDistance, IM_COL32(90, 167, 255, 220), 1.5f);
        ring(enemy.attackStartRange(motion), IM_COL32(255, 141, 60, 235), 1.5f);
        if (motion == Enemy::Motion::Slam)
        {
            ring(enemy.attackSettings().shockwaveRadius, IM_COL32(255, 218, 95, 150), 1.0f);
            if (active) ring(enemy.shockwaveRadiusAt(motionTime), gold, 2.5f);
        }
        if (motion == Enemy::Motion::Charge)
        {
            const auto forward = enemy.GetFoward();
            const auto pointAt = [&](float time)
            {
                return origin + Vector3{ forward.x, 0, forward.z } * enemy.chargeOffsetAt(time);
            };
            ImVec2 start, end;
            if (project(pointAt(phase.start), vp, size, start) && project(pointAt(phase.end), vp, size, end))
            {
                overlay.AddLine(start, end, gold, 2.0f);
                overlay.AddCircle(start, 4.0f, gold);
                overlay.AddCircleFilled(end, 4.0f, gold);
            }
            ImVec2 cursor;
            if (active && project(pointAt(motionTime), vp, size, cursor))
                overlay.AddCircleFilled(cursor, 5.0f, IM_COL32(255, 255, 255, 255));
        }
    }

    struct ViewRay { XMVECTOR origin; XMVECTOR direction; };

    ViewRay viewRay(const ImVec2& mouse, const ImVec2& size, FXMMATRIX vp)
    {
        const XMMATRIX inverse = XMMatrixInverse(nullptr, vp);
        const float x = mouse.x * 2.0f / size.x - 1.0f;
        const float y = 1.0f - mouse.y * 2.0f / size.y;
        const XMVECTOR start = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverse);
        const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverse);
        return { start, XMVector3Normalize(farPoint - start) };
    }

    bool terrainHit(const ViewRay& ray, Vector3& point)
    {
        const auto at = [&](float t) { return vec(ray.origin + ray.direction * t); };
        bool above = false;
        for (float t = 0.0f; t <= 360.0f; t += 0.75f)
        {
            auto test = at(t);
            float ground = 0.0f;
            if (!terrain->tryGetHeight(test.x, test.z, ground)) { above = false; continue; }
            if (test.y <= ground && above)
            {
                float lo = t - 0.75f, hi = t;
                for (int i = 0; i < 9; ++i)
                {
                    const float mid = (lo + hi) * 0.5f;
                    const auto sample = at(mid);
                    if (terrain->tryGetHeight(sample.x, sample.z, ground) && sample.y <= ground) hi = mid;
                    else lo = mid;
                }
                point = at(hi);
                terrain->tryGetHeight(point.x, point.z, point.y);
                return true;
            }
            above = test.y > ground;
        }
        return false;
    }

    void viewport()
    {
        auto& io = ImGui::GetIO();
        const auto size = io.DisplaySize;
        if (size.x <= 0 || size.y <= 0) return;
        const bool overScene = !io.WantCaptureMouse && ImGui::IsMousePosValid()
            && io.MousePos.x >= 0 && io.MousePos.y >= 0 && io.MousePos.x < size.x && io.MousePos.y < size.y;
        if (overScene && ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            yaw = XMScalarModAngle(yaw - io.MouseDelta.x * 0.008f);
            pitch = std::max(-0.15f, std::min(1.48f, pitch + io.MouseDelta.y * 0.008f));
        }
        if (overScene && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            const float pan = orbitDistance * 0.002f;
            focus += Vector3{ cosf(yaw), 0, -sinf(yaw) } * (-io.MouseDelta.x * pan);
            focus += Vector3{ sinf(yaw), 0, cosf(yaw) } * (io.MouseDelta.y * pan);
        }
        if (overScene && io.MouseWheel != 0)
            orbitDistance = std::max(2.0f, std::min(150.0f, orbitDistance * powf(.87f, io.MouseWheel)));
        if (!io.WantTextInput && !ImGui::IsAnyItemActive())
        {
            const float forward = float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S));
            const float right = float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A));
            const float magnitude = hypotf(forward, right);
            if (magnitude > 0.0f)
            {
                const float speed = io.KeyShift ? 24.0f : 8.0f;
                focus += (Vector3{ sinf(yaw), 0, cosf(yaw) } * forward
                    + Vector3{ cosf(yaw), 0, -sinf(yaw) } * right)
                    * (speed * std::min(io.DeltaTime, 0.05f) / magnitude);
            }
        }

        Vector3 eye;
        XMMATRIX view, projection;
        SceneEditor::cameraMatrices(eye, view, projection);
        const XMMATRIX vp = view * projection;
        auto* overlay = ImGui::GetBackgroundDrawList();

        if (terrainMode)
        {
            drag.axis = -1;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                painting = overScene;
                brushRecorded = false;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) painting = false;
            Vector3 hit;
            if (!overScene || !terrainHit(viewRay(io.MousePos, size, vp), hit)) return;
            if (painting)
            {
                auto before = brushRecorded ? nullptr : std::make_unique<MeshField::Heights>(terrain->heights());
                const float amount = strength * std::min(0.05f, io.DeltaTime);
                terrain->sculpt(hit.x, hit.z, radius, brushMode == 1 ? -amount : amount,
                    brushMode == 2 ? &flattenHeight : nullptr);
                if (before && *before != terrain->heights())
                {
                    remember({ UndoKind::Terrain, 0, {}, std::move(before) });
                    brushRecorded = true;
                }
            }
            ImVec2 previous;
            bool hasPrevious = false;
            for (int i = 0; i <= 40; ++i)
            {
                const float angle = i * XM_2PI / 40;
                Vector3 point = hit + Vector3{ cosf(angle) * radius, 0, sinf(angle) * radius };
                ImVec2 pixel;
                const bool visible = terrain->tryGetHeight(point.x, point.z, point.y)
                    && project(point, vp, size, pixel);
                if (visible && hasPrevious) overlay->AddLine(previous, pixel, IM_COL32(255, 206, 77, 255), 2.0f);
                if (visible) previous = pixel;
                hasPrevious = visible;
            }
            return;
        }

        const bool click = overScene && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag.axis = -1;
        if (selected >= 0 && drag.axis >= 0)
        {
            auto& entry = entries[selected];
            auto& position = entry.placement.transform.position;
            Vector3 next = drag.start;
            axisValue(next, drag.axis) += ((io.MousePos.x - drag.mouse.x) * drag.unit.x
                + (io.MousePos.y - drag.mouse.y) * drag.unit.y) / drag.screenUnits;
            if (!drag.recorded && next != drag.start)
            {
                remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
                drag.recorded = true;
            }
            position = next;
            apply(entry);
        }

        for (size_t i = 0; i < entries.size(); ++i)
        {
            ImVec2 pixel;
            auto point = entries[i].placement.transform.position;
            point.y += 0.35f;
            if (!project(point, vp, size, pixel)) continue;
            const bool active = selected == static_cast<int>(i);
            overlay->AddCircleFilled(pixel, active ? 6.0f : 4.0f,
                active ? IM_COL32(255, 211, 88, 255) : IM_COL32(232, 239, 250, 210));
        }

        if (selected >= 0)
        {
            const auto& entry = entries[selected];
            const auto& point = entry.placement.transform.position;
            if (showAttackRanges && isEnemy(entry.placement.kind))
                drawAttackRanges(*static_cast<Enemy*>(entry.object), entry.placement.kind, vp, size, *overlay);
            if (entry.placement.kind == Kind::Player)
            {
                const float centers[] = { Player::BodyLowerCenter, Player::BodyUpperCenter };
                for (float y : centers)
                {
                    const auto center = point + Vector3{ 0, y, 0 };
                    drawCapsule({ center, center, Player::BodyRadius },
                        vp, size, *overlay, IM_COL32(90, 234, 182, 200), false);
                }
            }
            else if (isEnemy(entry.placement.kind) && static_cast<Enemy*>(entry.object)->hasCharacter())
            {
                auto* enemy = static_cast<Enemy*>(entry.object);
                if (entry.placement.collider && showHurtboxes)
                    for (size_t i = 0; i < enemy->hurtboxCount(); ++i)
                    {
                        const bool current = activeHurtbox == static_cast<int>(i);
                        drawCapsule(enemy->hurtboxPose(i), vp, size, *overlay,
                            current ? IM_COL32(255, 203, 74, 255) : IM_COL32(97, 231, 195, 185), current);
                    }
            }
            else if (entry.placement.collider && isEnemy(entry.placement.kind))
            {
                static_cast<Enemy*>(entry.object)->VisitColliderParts([&](const XMMATRIX& world)
                {
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        ImVec2 previous;
                        bool visible = false;
                        for (int i = 0; i <= 24; ++i)
                        {
                            const float angle = i * XM_2PI / 24;
                            Vector3 p{ 0, 0, 0 };
                            axisValue(p, (axis + 1) % 3) = cosf(angle);
                            axisValue(p, (axis + 2) % 3) = sinf(angle);
                            ImVec2 current;
                            const bool projected = project(vec(XMVector3TransformCoord(vec(p), world)), vp, size, current);
                            if (visible && projected)
                                overlay->AddLine(previous, current, IM_COL32(90, 234, 182, 200), 1.5f);
                            if (projected) previous = current;
                            visible = projected;
                        }
                    }
                });
            }
            else if (entry.placement.collider)
            {
                XMFLOAT3 corners[8];
                bounds(entry).GetCorners(corners);
                ImVec2 pixels[8];
                bool visible[8];
                for (int i = 0; i < 8; ++i)
                    visible[i] = project({ corners[i].x, corners[i].y, corners[i].z }, vp, size, pixels[i]);
                const int edges[][2] = { {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4},
                    {0,4}, {1,5}, {2,6}, {3,7} };
                for (const auto& edge : edges)
                    if (visible[edge[0]] && visible[edge[1]])
                        overlay->AddLine(pixels[edge[0]], pixels[edge[1]], IM_COL32(90, 234, 182, 200), 1.5f);
            }
            ImVec2 origin;
            if (project(point, vp, size, origin))
            {
                const ImU32 colors[] = { IM_COL32(239, 88, 82, 255), IM_COL32(104, 221, 108, 255), IM_COL32(96, 157, 255, 255) };
                const char* const axisNames[] = { "X", "Y", "Z" };
                float bestAxisGap = 10.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    Vector3 end = point;
                    const float length = std::max(1.0f, (point - eye).lenght() * 0.12f);
                    axisValue(end, axis) += length;
                    ImVec2 tip;
                    if (!project(end, vp, size, tip)) continue;
                    const float dx = tip.x - origin.x, dy = tip.y - origin.y;
                    const float pixels = hypotf(dx, dy);
                    if (pixels < 12) continue;
                    overlay->AddLine(origin, tip, colors[axis], 3.0f);
                    overlay->AddCircleFilled(tip, 5.0f, colors[axis]);
                    overlay->AddText({ tip.x + 7.0f, tip.y - 7.0f }, colors[axis], axisNames[axis]);
                    const float ux = dx / pixels, uy = dy / pixels;
                    const float mx = io.MousePos.x - origin.x, my = io.MousePos.y - origin.y;
                    const float along = mx * ux + my * uy;
                    const float gap = fabsf(mx * uy - my * ux);
                    if (click && along >= 10.0f && along <= pixels + 7.0f && gap < bestAxisGap)
                    {
                        bestAxisGap = gap;
                        drag = { axis, io.MousePos, { ux, uy }, pixels / length, point };
                    }
                }
            }
        }
        if (click && drag.axis < 0)
        {
            const ViewRay ray = viewRay(io.MousePos, size, vp);
            Vector3 ground;
            float nearest = terrainHit(ray, ground) ? (ground - vec(ray.origin)).lenght() + 0.03f : 450.0f;
            int pick = -1;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                float hit;
                const auto& entry = entries[i];
                const bool hitObject = isEnemy(entry.placement.kind)
                    ? static_cast<Enemy*>(entry.object)->RaycastMesh(ray.origin, ray.direction, hit)
                    : rayHit(bounds(entry), ray.origin, ray.direction, hit);
                if (hitObject && hit < nearest)
                {
                    nearest = hit;
                    pick = static_cast<int>(i);
                }
            }
            select(pick);
        }
    }
}

void SceneEditor::init(Player& scenePlayer, MeshField& sceneTerrain)
{
    player = &scenePlayer;
    terrain = &sceneTerrain;
    const bool exists = GetFileAttributesA(layoutPath) != INVALID_FILE_ATTRIBUTES;
    if (!loadLayout())
    {
        defaults();
        if (exists) status = "Saved layout could not be loaded. Using default placements.";
    }
    focus = player->GetPosition() + Vector3{ 0, 1, 6 };
}

void SceneEditor::shutdown()
{
    stopMotionPreview();
    history.clear();
    audioAssets.clear();
    audioStatus.clear();
    audioPanel = false;
    audioCue = 0;
    audioVolumeRecorded = false;
    motionPanel = false;
    motionStatus.clear();
    motionLoop = false;
    entries.clear();
    models.clear();
    status.clear();
    player = nullptr;
    terrain = nullptr;
    open = false;
    terrainMode = false;
    showHurtboxes = false;
    showAttackRanges = false;
    activeHurtbox = 0;
    select(-1);
}

bool SceneEditor::isAvailable() { return player && terrain; }
bool SceneEditor::isOpen() { return open; }

void SceneEditor::toggle()
{
    if (!isAvailable()) return;
    if (open) stopMotionPreview();
    open = !open;
    SceneAudio::setEditing(open);
    drag.axis = -1;
    painting = false;
    if (open)
        for (auto& entry : entries) apply(entry);
}

bool SceneEditor::cameraMatrices(Vector3& eye, XMMATRIX& view, XMMATRIX& projection)
{
    if (!open) return false;
    const float horizontal = orbitDistance * cosf(pitch);
    eye = focus + Vector3{ -sinf(yaw) * horizontal, sinf(pitch) * orbitDistance, -cosf(yaw) * horizontal };
    view = XMMatrixLookAtLH(vec(eye), vec(focus), XMVectorSet(0, 1, 0, 0));
    projection = XMMatrixPerspectiveFovLH(1.0f, static_cast<float>(SCREEN_WIDTH) / SCREEN_HEIGHT, 0.1f, 450.0f);
    return true;
}

void SceneEditor::draw()
{
    if (!ImGui::IsAnyItemActive()) numericRecorded = false;
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive()
        && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo();
    inspector();
    drawAudioPanel();
    drawMotionPanel();
    viewport();
}

void SceneEditor::updatePhysics(float dt)
{
    if (!terrain || open || dt <= 0.0f) return;
    for (auto& entry : entries)
    {
        if (!entry.placement.gravity) continue;
        auto position = entry.object->GetPosition();
        const auto box = bounds(entry);
        XMFLOAT3 corners[8];
        box.GetCorners(corners);
        float bottom = corners[0].y;
        for (const auto& corner : corners) bottom = std::min(bottom, corner.y);

        float floor = -std::numeric_limits<float>::infinity();
        terrain->tryGetHeight(position.x, position.z, floor);
        if (entry.placement.collider)
            floor = floorHit(box.Center.x, box.Center.z, bottom + 0.04f, floor, entry.object);
        entry.fallSpeed = std::max(-35.0f, entry.fallSpeed - 18.0f * dt);
        const float dy = entry.fallSpeed * dt;
        if (bottom + dy <= floor)
        {
            position.y += floor - bottom;
            entry.fallSpeed = 0.0f;
        }
        else position.y += dy;
        entry.object->SetPosition(position);
    }
}

void SceneEditor::resolvePlayerCollision(const Vector3& previous, Vector3& position, Vector3& velocity, bool& grounded)
{
    constexpr float floorTolerance = 0.065f;
    const auto overlaps = [&](float x, float z)
    {
        const float y = std::max(previous.y, position.y);
        const BoundingSphere lower({ x, y + Player::BodyLowerCenter, z }, Player::BodyRadius);
        const BoundingSphere upper({ x, y + Player::BodyUpperCenter, z }, Player::BodyRadius);
        bool blocked = false;
        forEachCollider(nullptr, [&](const BoundingOrientedBox& box)
        {
            blocked = blocked || box.Intersects(lower) || box.Intersects(upper);
        });
        for (const auto& entry : entries)
        {
            if (blocked) break;
            if (!entry.placement.collider || !isEnemy(entry.placement.kind)) continue;
            auto* enemy = static_cast<Enemy*>(entry.object);
            blocked = enemy->IntersectsMesh(lower) || enemy->IntersectsMesh(upper);
        }
        return blocked;
    };

    if (!overlaps(previous.x, previous.z))
    {
        if (overlaps(position.x, previous.z))
        {
            position.x = previous.x;
            velocity.x = 0.0f;
        }
        if (overlaps(position.x, position.z))
        {
            position.z = previous.z;
            velocity.z = 0.0f;
        }
    }

    if (velocity.y <= 0.0f)
    {
        const float floor = floorHit(position.x, position.z, previous.y + floorTolerance,
            -std::numeric_limits<float>::infinity());
        if (position.y <= floor)
        {
            position.y = floor;
            velocity.y = 0.0f;
            grounded = true;
        }
    }
    else
    {
        constexpr float headHeight = Player::BodyUpperCenter + Player::BodyRadius;
        const float oldHead = previous.y + headHeight;
        const XMVECTOR origin = XMVectorSet(position.x, oldHead, position.z, 1.0f);
        const XMVECTOR up = XMVectorSet(0, 1, 0, 0);
        float ceiling = std::numeric_limits<float>::infinity();
        forEachCollider(nullptr, [&](const BoundingOrientedBox& box)
        {
            float hit = 0.0f;
            if (box.Contains(origin) == DISJOINT && rayHit(box, origin, up, hit))
                ceiling = std::min(ceiling, oldHead + hit);
        });
        if (position.y + headHeight >= ceiling)
        {
            position.y = ceiling - headHeight - 0.01f;
            velocity.y = 0.0f;
        }
    }
}

bool SceneEditor::blocksEnemy(const Vector3& position, float radius)
{
    const BoundingSphere body({ position.x, position.y + radius + 0.02f, position.z }, radius);
    bool blocked = false;
    forEachCollider(nullptr, [&](const BoundingOrientedBox& box) { blocked = blocked || box.Intersects(body); });
    return blocked;
}
