#include "main.h"
#include "renderer.h"
#include "GameObject.h"
#include "manager.h"
#include "Game.h"
#include "input.h"
#include "SceneEditor.h"
#include "SceneAudio.h"
#include "MeshField.h"
#include "Wave.h"
#include "Player.h"
#include "Heroine.h"
#include "Enemy.h"
#include "Box.h"
#include "Grass.h"
#include "Particle.h"
#include "Camera.h"
#include "SkyDome.h"
#include "animationModel.h"
#include "imgui/imgui.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <wrl/client.h>
#include <DirectXCollision.h>

namespace
{
    namespace fs = std::filesystem;
    const char* layoutPath = "asset/scene-layout.txt";
    const char* temporaryPath = "asset/scene-layout.txt.tmp";
    constexpr size_t maxObjects = 512;
    constexpr int layoutVersion = 12;
    constexpr size_t maxAmbientZones = 32;
    constexpr size_t maxPrefabs = 128;

    enum class Kind { Player, Enemy, Boss, Box, Grass, Particle, Model, Heroine };
    const char* const labels[] = { u8"プレイヤー開始地点", u8"敵", u8"中ボス", u8"箱", u8"草", u8"パーティクル", u8"モデル", u8"ヒロイン" };

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
    constexpr size_t heroineMotionCount = static_cast<size_t>(HeroineActionKind::count);
    struct Placement
    {
        Kind kind;
        Transform transform;
        std::string asset;
        bool collider = false;
        bool gravity = false;
        std::vector<std::pair<std::string, Enemy::HurtboxAdjustment>> hurtboxes;
        std::array<MotionTrack, 8> motions;
        Enemy::AttackSettings attack = Enemy::defaultAttackSettings(kind == Kind::Boss);
        std::string prefab;
        bool alphaBlend = false;
        float opacity = 1.0f;
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
    Heroine* heroine = nullptr;
    MeshField* terrain = nullptr;
    std::vector<Entry> entries;
    struct EditWorld
    {
        Player* player;
        Heroine* heroine;
        MeshField* terrain;
        std::vector<Entry> entries;
    };
    std::optional<EditWorld> editWorld;
    bool playModeRequested = false;
    std::map<std::string, Placement> prefabs;
    bool prefabPanel = false;
    std::string prefabChoice;
    std::string prefabToPlace;
    char newPrefabName[65]{};
    std::vector<std::string> models;
    bool modelsScanned = false;
    ImGuiTextFilter hierarchySearch;
    int hierarchyKind = 0;
    ImGuiTextFilter assetSearch;
    std::string status;
    int selected = -1;
    bool open = false;
    bool terrainMode = false;
    Wave waterRenderer;
    Wave::Layout water;
    Wave::River riverDefaults;
    bool waterMode = false;
    int waterTool = 0;
    const char* basinMessage = nullptr;
    int selectedRiver = -1;
    float waterBrushRadius = 4.0f;
    bool waterStroke = false;
    bool waterUndoRecorded = false;
    std::optional<Vector3> previousWaterHit;
    std::vector<XMFLOAT2> riverDraft;
    int riverPoint = -1;
    bool riverDragging = false;
    bool riverMovementRecorded = false;
    bool gridSnap = false;
    float gridStep = 1.0f;
    int brushMode = 0;
    float radius = 5.0f;
    float strength = 4.0f;
    float materialOpacity = 65.0f;
    float flattenHeight = 0.0f;
    Vector3 focus{ 0, 1, 6 };
    float yaw = 0.0f;
    float pitch = 0.45f;
    float orbitDistance = 19.0f;
    bool painting = false;
    bool brushRecorded = false;
    std::optional<Vector3> previousMaterialHit;
    bool numericRecorded = false;
    bool showHurtboxes = false;
    int activeHurtbox = 0;

    struct AttackField
    {
        const char* label;
        float Enemy::AttackSettings::* value;
        float minimum, maximum;
        bool bossOnly = false;
    };
    constexpr AttackField attackFields[] = {
        { u8"検知距離", &Enemy::AttackSettings::detectionDistance, 0.0f, 100.0f },
        { u8"攻撃間隔 (秒)", &Enemy::AttackSettings::attackInterval, 0.0f, 60.0f },
        { u8"パンチ開始距離", &Enemy::AttackSettings::punchStartRange, 0.1f, 30.0f },
        { u8"パンチ再使用間隔 (秒)", &Enemy::AttackSettings::punchRepeatInterval, 0.0f, 60.0f, true },
        { u8"叩きつけ開始距離", &Enemy::AttackSettings::slamStartRange, 0.1f, 30.0f, true },
        { u8"突進開始距離", &Enemy::AttackSettings::chargeStartRange, 0.1f, 30.0f, true },
        { u8"突進距離", &Enemy::AttackSettings::chargeDistance, 0.0f, 30.0f, true },
        { u8"衝撃波の最大半径", &Enemy::AttackSettings::shockwaveRadius, 0.1f, 30.0f, true },
    };

    enum class UndoKind { Modify, Add, Remove, Terrain, Audio, Prefab, Water, Appearance, TerrainPaint, Ambient };
    using PlacementChanges = std::vector<std::pair<size_t, Placement>>;
    struct PrefabEdit
    {
        std::string name;
        std::optional<Placement> previous;
        PlacementChanges instances;
    };
    struct EditRecord
    {
        UndoKind kind;
        size_t index = 0;
        Placement previous{};
        std::unique_ptr<MeshField::Heights> heights;
        SceneAudio::Setting audio;
        std::unique_ptr<PrefabEdit> prefab;
        std::unique_ptr<Wave::Layout> water;
        std::unique_ptr<MeshField::MaterialPaint> materialPaint;
        std::vector<SceneAudio::AmbientZone> ambient;
    };
    std::deque<EditRecord> history;
    bool audioPanel = false;
    int audioCue = 0;
    std::vector<std::string> audioAssets;
    std::string audioStatus;
    bool audioVolumeRecorded = false;
    std::vector<SceneAudio::AmbientZone> ambientZones;
    int selectedAmbient = -1;
    bool ambientPanel = false;
    bool placingAmbient = false;
    bool ambientDragRecorded = false;
    std::string ambientStatus;
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

    void rememberWater()
    {
        EditRecord edit{ UndoKind::Water };
        edit.water = std::make_unique<Wave::Layout>(water);
        remember(std::move(edit));
    }

    void rememberAmbient()
    {
        EditRecord edit{ UndoKind::Ambient };
        edit.ambient = ambientZones;
        remember(std::move(edit));
    }

    void stopWaterEdit()
    {
        basinMessage = nullptr;
        waterStroke = false;
        waterUndoRecorded = false;
        previousWaterHit.reset();
        riverDraft.clear();
        riverPoint = -1;
        riverDragging = false;
        riverMovementRecorded = false;
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

    float snapCoordinate(float value)
    {
        return std::round(value / gridStep) * gridStep;
    }

    bool snapPlacementToGround(Vector3& point)
    {
        if (gridSnap)
        {
            point.x = snapCoordinate(point.x);
            point.z = snapCoordinate(point.z);
        }
        return terrain->tryGetHeight(point.x, point.z, point.y);
    }
    XMVECTOR vec(const Vector3& p) { return XMVectorSet(p.x, p.y, p.z, 0); }
    Vector3 vec(FXMVECTOR v) { return { XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v) }; }

    bool isEnemy(Kind kind) { return kind == Kind::Enemy || kind == Kind::Boss; }
    bool modelPath(const std::string& text, std::string& normalized);
    void scanAudio();

    size_t motionCount(Kind kind)
    {
        if (kind == Kind::Heroine) return heroineMotionCount;
        return isEnemy(kind) ? Enemy::MotionCount : 0;
    }

    void applyMotion(Entry& entry)
    {
        for (size_t i = 0; i < motionCount(entry.placement.kind); ++i)
        {
            const auto& track = entry.placement.motions[i];
            const auto heroineSlot = static_cast<HeroineActionKind>(i);
            const auto enemySlot = static_cast<Enemy::Motion>(i);
            auto* enemy = isEnemy(entry.placement.kind) ? static_cast<Enemy*>(entry.object) : nullptr;
            if (enemy)
            {
                std::string clip;
                if ((!track.clip.empty() && !modelPath(track.clip, clip)) || !enemy->assignMotion(enemySlot, clip))
                {
                    motionStatus = u8"この敵の保存済みモーションを読み込めません。";
                    enemy->assignMotion(enemySlot, {});
                }
            }
            auto* heroineActor = enemy ? nullptr : static_cast<Heroine*>(entry.object);
            const float duration = enemy ? enemy->motionDuration(enemySlot) : heroineActor->motionDuration(heroineSlot);
            SceneAudio::Timeline ready;
            for (const auto& marker : track.sounds)
                if (duration > 0 && marker.time <= duration && SceneAudio::prepareMarker(marker.file))
                    ready.push_back(marker);
            if (enemy) enemy->setMotionSounds(enemySlot, ready);
            else heroineActor->setMotionSounds(heroineSlot, ready);
        }
    }

    void stopMotionPreview()
    {
        if (motionWasSampled && selected >= 0)
        {
            const auto& entry = entries[selected];
            if (isEnemy(entry.placement.kind)) static_cast<Enemy*>(entry.object)->clearMotionPreview();
            else if (entry.placement.kind == Kind::Heroine) static_cast<Heroine*>(entry.object)->clearMotionPreview();
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
        entry.object->setAlphaBlend(entry.placement.alphaBlend, entry.placement.opacity);
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
        case Kind::Heroine:
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

    std::unique_ptr<Enemy> prepareEnemyCharacter(Enemy& enemy, const Placement& placement)
    {
        std::string model;
        if (!placement.asset.empty() && !modelPath(placement.asset, model)) return nullptr;
        std::array<std::string, Enemy::MotionCount> clips;
        for (size_t i = 0; i < clips.size(); ++i)
        {
            const auto& file = placement.motions[i].clip;
            if (!file.empty() && !modelPath(file, clips[i])) return nullptr;
        }
        return enemy.prepareCharacter(model, clips);
    }

    bool prefabAudioAvailable(const Placement& placement)
    {
        for (size_t i = 0; i < Enemy::MotionCount; ++i)
            for (const auto& marker : placement.motions[i].sounds)
                if (!marker.file.empty() && !SceneAudio::prepareMarker(marker.file)) return false;
        return true;
    }

    bool spawn(const Placement& placement, bool requirePrefabAssets = false)
    {
        if (entries.size() >= maxObjects) return false;
        if ((placement.kind == Kind::Player || placement.kind == Kind::Heroine)
            && std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.placement.kind == placement.kind; })) return false;
        GameObject* object = nullptr;
        switch (placement.kind)
        {
        case Kind::Player: object = player; break;
        case Kind::Heroine: object = heroine; break;
        case Kind::Enemy:
        case Kind::Boss:
        {
            auto* enemy = Manager::AddGameObj<Enemy>();
            enemy->Configure(placement.kind == Kind::Boss);
            if (requirePrefabAssets)
            {
                auto prepared = prepareEnemyCharacter(*enemy, placement);
                if (!prepared || !prefabAudioAvailable(placement))
                {
                    enemy->SetDestory();
                    return false;
                }
                enemy->adoptCharacter(*prepared);
            }
            else if (!enemy->loadCharacter(placement.asset))
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
        if (!object) return false;
        entries.push_back({ placement, object });
        apply(entries.back());
        applyMotion(entries.back());
        return true;
    }

    void defaults()
    {
        spawn({ Kind::Player, { player->GetPosition(), player->GetRotation(), player->GetScale() }, {} });
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

    bool validPrefabName(const std::string& name)
    {
        return !name.empty() && name.size() < sizeof(newPrefabName)
            && name.front() != ' ' && name.back() != ' '
            && std::none_of(name.begin(), name.end(), [](unsigned char c) {
                return c < 32 || c == 127 || c == '#';
            });
    }

    bool readPlacement(std::istream& input, int version, Placement& placement)
    {
        int kind = 0;
        auto& t = placement.transform;
        if (!(input >> kind) || kind < 0 || kind > static_cast<int>(version >= 11 ? Kind::Heroine : Kind::Model)
            || !readVector(input, t.position) || !readVector(input, t.rotation) || !readVector(input, t.scale)
            || !(input >> std::quoted(placement.asset))
            || t.scale.x < .01f || t.scale.y < .01f || t.scale.z < .01f) return false;
        placement.kind = static_cast<Kind>(kind);
        const bool enemy = isEnemy(placement.kind);
        placement.attack = Enemy::defaultAttackSettings(placement.kind == Kind::Boss);
        placement.collider = placement.kind == Kind::Box || enemy;
        if (version >= 2)
        {
            int collider = 0, gravity = 0;
            if (!(input >> collider >> gravity) || (collider != 0 && collider != 1)
                || (gravity != 0 && gravity != 1)) return false;
            if (!enemy || version >= 3)
                placement.collider = (supportsPhysics(placement.kind) || enemy) && collider != 0;
            placement.gravity = supportsPhysics(placement.kind) && gravity != 0;
        }
        if (version >= 5 && !readHurtboxEdits(input, placement.hurtboxes)) return false;
        if (version >= 6)
        {
            for (size_t slot = 0; slot < (version <= 10 && placement.kind == Kind::Player ? 8 : motionCount(placement.kind)); ++slot)
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
        if (version >= 7 && enemy)
        {
            for (const auto& field : attackFields)
            {
                auto& value = placement.attack.*field.value;
                if (!(input >> value) || !(value >= field.minimum && value <= field.maximum)) return false;
            }
        }
        if (version >= 8 && enemy && !(input >> std::quoted(placement.prefab))) return false;
        if (version >= 10)
        {
            int alpha = 0;
            if (!(input >> alpha >> placement.opacity) || (alpha != 0 && alpha != 1)
                || !(placement.opacity >= 0.0f && placement.opacity <= 1.0f)) return false;
            placement.alphaBlend = alpha != 0;
        }
        return true;
    }

    void writePlacement(std::ostream& output, const Placement& p, int version)
    {
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
            for (size_t i = 0; i < std::size(attackFields); ++i)
            {
                if (i > 0) output << ' ';
                output << p.attack.*attackFields[i].value;
            }
            output << '\n';
            if (version >= 8) output << std::quoted(p.prefab) << '\n';
        }
        if (version >= 10) output << p.alphaBlend << ' ' << p.opacity << '\n';
    }

    bool readAmbient(std::istream& input, std::vector<SceneAudio::AmbientZone>& zones)
    {
        std::string signature;
        size_t count = 0;
        if (!(input >> signature >> count) || signature != "AmbientZones" || count > maxAmbientZones)
            return false;
        const auto inRange = [](float value, float low, float high) { return value >= low && value <= high; };
        zones.resize(count);
        for (auto& zone : zones)
            if (!(input >> std::quoted(zone.file) >> zone.x >> zone.z >> zone.radius >> zone.volume)
                || !inRange(zone.x, -100.0f, 100.0f) || !inRange(zone.z, -100.0f, 100.0f)
                || !inRange(zone.radius, 1.0f, 100.0f) || !inRange(zone.volume, 0.0f, 1.0f))
                return false;
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
        int playerCount = 0, heroineCount = 0;
        for (size_t i = 0; i < count; ++i)
        {
            Placement placement{};
            if (!readPlacement(input, version, placement)) return false;
            if (placement.kind == Kind::Player) ++playerCount;
            if (placement.kind == Kind::Heroine) ++heroineCount;
            if ((placement.kind == Kind::Model || (isEnemy(placement.kind) && !placement.asset.empty()))
                && !modelPath(placement.asset, placement.asset)) return false;
            placements.push_back(std::move(placement));
        }
        MeshField::Heights heights{};
        for (float& height : heights)
            if (!(input >> height) || !std::isfinite(height) || fabsf(height) > 30.0f) return false;
        if (playerCount != 1 || heroineCount > 1) return false;
        if (version == 4)
        {
            std::vector<std::pair<std::string, Enemy::HurtboxAdjustment>> legacyPlayerEdits;
            if (!readHurtboxEdits(input, legacyPlayerEdits)) return false;
        }

        std::map<std::string, Placement> loadedPrefabs;
        if (version >= 8)
        {
            size_t prefabCount = 0;
            if (!(input >> prefabCount) || prefabCount > maxPrefabs) return false;
            for (size_t i = 0; i < prefabCount; ++i)
            {
                std::string name;
                Placement definition{};
                // Older prefab definitions use the version 7 placement fields.
                if (!(input >> std::quoted(name)) || !validPrefabName(name)
                    || !readPlacement(input, version >= 10 ? 10 : 7, definition) || !isEnemy(definition.kind)
                    || !loadedPrefabs.emplace(std::move(name), std::move(definition)).second) return false;
            }
            for (const auto& placement : placements)
            {
                if (placement.prefab.empty()) continue;
                const auto found = loadedPrefabs.find(placement.prefab);
                if (found == loadedPrefabs.end() || found->second.kind != placement.kind) return false;
            }
        }
        Wave::Layout loadedWater;
        if (version >= 9 && !loadedWater.read(input)) return false;
        MeshField::MaterialPaint loadedPaint;
        if (version >= 11 && !MeshField::readMaterialPaint(input, loadedPaint)) return false;
        std::vector<SceneAudio::AmbientZone> loadedAmbient;
        if (version >= 12 && !readAmbient(input, loadedAmbient)) return false;

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
        terrain->setMaterialPaint(std::move(loadedPaint));
        prefabs.swap(loadedPrefabs);
        water = std::move(loadedWater);
        ambientZones = std::move(loadedAmbient);
        selectedRiver = -1;
        stopWaterEdit();
        waterRenderer.invalidate();
        return true;
    }

    bool saveLayout()
    {
        std::ofstream output(temporaryPath, std::ios::trunc);
        if (!output) return false;
        const auto hasAppearance = [](const Placement& placement)
        {
            return placement.alphaBlend || placement.opacity != 1.0f;
        };
        const bool editsAppearance = std::any_of(entries.begin(), entries.end(),
            [&](const Entry& entry) { return hasAppearance(entry.placement); })
            || std::any_of(prefabs.begin(), prefabs.end(),
                [&](const auto& prefab) { return hasAppearance(prefab.second); });
        const int version = !ambientZones.empty() ? 12 : terrain->hasMaterialPaint() ? 11 : editsAppearance ? 10
            : water.edited() ? 9 : prefabs.empty() ? 7 : 8;
        output << "SceneLayout " << version << ' ' << entries.size() << ' ' << MeshField::GridSize << '\n';
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& entry : entries) writePlacement(output, entry.placement, version);
        const auto& heights = terrain->heights();
        for (size_t i = 0; i < heights.size(); ++i)
            output << heights[i] << ((i + 1) % MeshField::GridSize ? ' ' : '\n');
        if (version >= 8)
        {
            output << prefabs.size() << '\n';
            for (const auto& [name, definition] : prefabs)
            {
                output << std::quoted(name) << '\n';
                writePlacement(output, definition, version >= 10 ? 10 : 7);
            }
        }
        if (version >= 9) water.write(output);
        if (version >= 11) terrain->writeMaterialPaint(output);
        if (version >= 12)
        {
            output << "AmbientZones " << ambientZones.size() << '\n';
            for (const auto& zone : ambientZones)
                output << std::quoted(zone.file) << ' ' << zone.x << ' ' << zone.z << ' '
                    << zone.radius << ' ' << zone.volume << '\n';
        }
        output.close();
        return output && MoveFileExA(temporaryPath, layoutPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }


    void refreshModels()
    {
        modelsScanned = true;
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

    void deleteSelected()
    {
        if (selected < 0) return;
        auto& entry = entries[selected];
        if (entry.placement.kind == Kind::Player || entry.placement.kind == Kind::Heroine) return;

        stopMotionPreview();
        remember({ UndoKind::Remove, static_cast<size_t>(selected), entry.placement });
        entry.object->SetDestory();
        entries.erase(entries.begin() + selected);
        select(-1);
    }

    bool applyPrefabInstances(const PlacementChanges& changes)
    {
        std::vector<std::unique_ptr<Enemy>> prepared(changes.size());
        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto& [index, next] = changes[i];
            const auto& current = entries[index].placement;
            bool needsCharacter = current.asset != next.asset;
            for (size_t slot = 0; slot < Enemy::MotionCount && !needsCharacter; ++slot)
                needsCharacter = current.motions[slot].clip != next.motions[slot].clip;
            if (!needsCharacter) continue;
            prepared[i] = prepareEnemyCharacter(*static_cast<Enemy*>(entries[index].object), next);
            if (!prepared[i]) return false;
        }

        stopMotionPreview();
        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto& [index, next] = changes[i];
            auto& entry = entries[index];
            if (prepared[i]) static_cast<Enemy*>(entry.object)->adoptCharacter(*prepared[i]);
            entry.placement = next;
            apply(entry);
            applyMotion(entry);
        }
        motionTime = 0;
        activeHurtbox = 0;
        return true;
    }

    void createPrefab(const std::string& name, size_t sourceIndex)
    {
        if (!validPrefabName(name) || prefabs.count(name) != 0 || prefabs.size() >= maxPrefabs)
        {
            status = u8"重複しない名前を入力してください (1～64バイト。# と前後の空白は使用不可)。";
            return;
        }
        auto& entry = entries[sourceIndex];
        auto& source = entry.placement;
        if (!prepareEnemyCharacter(*static_cast<Enemy*>(entry.object), source))
        {
            status = u8"プレハブを作成できません。モデルとモーションクリップを確認してください。";
            return;
        }
        if (!prefabAudioAvailable(source))
        {
            status = u8"プレハブを作成できません。WAVファイルを確認してください。";
            return;
        }
        EditRecord edit{ UndoKind::Prefab };
        edit.prefab = std::make_unique<PrefabEdit>();
        edit.prefab->name = name;
        edit.prefab->instances.emplace_back(sourceIndex, source);
        auto definition = source;
        definition.transform.position = { 0, 0, 0 };
        definition.prefab.clear();
        prefabs.emplace(name, std::move(definition));
        source.prefab = name;
        prefabChoice = name;
        remember(std::move(edit));
        status = u8"プレハブを作成し、選択中の敵にリンクしました。「配置を保存」で保存できます。";
    }

    void updatePrefab(size_t sourceIndex)
    {
        const auto& source = entries[sourceIndex].placement;
        const auto found = prefabs.find(source.prefab);
        if (found == prefabs.end() || found->second.kind != source.kind) return;
        if (!prefabAudioAvailable(source))
        {
            status = u8"プレハブを更新できません。WAVファイルを確認してください。";
            return;
        }
        auto definition = source;
        definition.transform = found->second.transform;
        definition.prefab.clear();

        EditRecord edit{ UndoKind::Prefab };
        edit.prefab = std::make_unique<PrefabEdit>();
        edit.prefab->name = found->first;
        edit.prefab->previous = found->second;
        PlacementChanges changes;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& current = entries[i].placement;
            if (current.prefab != found->first) continue;
            auto next = definition;
            next.transform = current.transform;
            next.prefab = found->first;
            edit.prefab->instances.emplace_back(i, current);
            changes.emplace_back(i, std::move(next));
        }
        if (!applyPrefabInstances(changes))
        {
            status = u8"プレハブを更新できません。モデルとモーションクリップを確認してください。";
            return;
        }
        found->second = std::move(definition);
        remember(std::move(edit));
        status = std::to_string(changes.size()) + u8"体の敵を更新しました。「配置を保存」で変更を保存できます。";
    }

    bool placePrefab(const std::string& name, const Vector3& position)
    {
        const auto found = prefabs.find(name);
        if (found == prefabs.end()) return false;
        auto placement = found->second;
        placement.prefab = name;
        placement.transform.position = position;
        if (!spawn(placement, true)) return false;
        remember({ UndoKind::Add, entries.size() - 1 });
        select(static_cast<int>(entries.size() - 1));
        return true;
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
                status = u8"変更前のモデルに戻せませんでした。";
                return;
            }
            entries[edit.index].placement = edit.previous;
            apply(entries[edit.index]);
            applyMotion(entries[edit.index]);
            select(static_cast<int>(edit.index));
            break;
        case UndoKind::Appearance:
        {
            auto& entry = entries[edit.index];
            entry.placement.alphaBlend = edit.previous.alphaBlend;
            entry.placement.opacity = edit.previous.opacity;
            entry.object->setAlphaBlend(entry.placement.alphaBlend, entry.placement.opacity);
            select(static_cast<int>(edit.index));
            break;
        }
        case UndoKind::Add:
            stopMotionPreview();
            entries[edit.index].object->SetDestory();
            entries.erase(entries.begin() + edit.index);
            select(-1);
            break;
        case UndoKind::Remove:
            if (!spawn(edit.previous))
            {
                status = u8"オブジェクトを復元できませんでした。";
                return;
            }
            std::rotate(entries.begin() + edit.index, entries.end() - 1, entries.end());
            select(static_cast<int>(edit.index));
            break;
        case UndoKind::Terrain:
            terrain->setHeights(*edit.heights);
            break;
        case UndoKind::TerrainPaint:
            terrain->setMaterialPaint(*edit.materialPaint);
            painting = false;
            previousMaterialHit.reset();
            break;
        case UndoKind::Water:
            water = *edit.water;
            if (selectedRiver >= static_cast<int>(water.rivers.size())) selectedRiver = -1;
            stopWaterEdit();
            waterRenderer.invalidate();
            break;
        case UndoKind::Ambient:
            ambientZones = std::move(edit.ambient);
            if (selectedAmbient >= static_cast<int>(ambientZones.size())) selectedAmbient = -1;
            SceneAudio::stopAmbientPreview();
            ambientStatus.clear();
            break;
        case UndoKind::Audio:
        {
            const auto cue = static_cast<SceneAudio::Cue>(edit.index);
            if (SceneAudio::setting(cue).file != edit.audio.file && !SceneAudio::assign(cue, edit.audio.file))
            {
                audioStatus = u8"変更前の音声ファイルに戻せませんでした。";
                return;
            }
            SceneAudio::setVolume(cue, edit.audio.volume);
            SceneAudio::setEnabled(cue, edit.audio.enabled);
            audioCue = static_cast<int>(edit.index);
            audioStatus = u8"直前の音声編集を取り消しました。";
            break;
        }
        case UndoKind::Prefab:
        {
            const auto& prefabEdit = *edit.prefab;
            if (!applyPrefabInstances(prefabEdit.instances))
            {
                status = u8"プレハブの変更を取り消せません。変更前のモデルとモーションクリップを確認してください。";
                return;
            }
            if (prefabEdit.previous) prefabs.insert_or_assign(prefabEdit.name, *prefabEdit.previous);
            else
            {
                prefabs.erase(prefabEdit.name);
                if (prefabChoice == prefabEdit.name) prefabChoice.clear();
                if (prefabToPlace == prefabEdit.name) prefabToPlace.clear();
            }
            break;
        }
        }
        drag.axis = -1;
        painting = false;
        history.pop_back();
        status = u8"直前の編集を取り消しました。";
    }

    bool recordNumeric(bool changed, const Placement& before, UndoKind kind = UndoKind::Modify)
    {
        if (ImGui::IsItemActivated()) numericRecorded = false;
        if (changed && !numericRecorded)
        {
            remember({ kind, static_cast<size_t>(selected), before });
            numericRecorded = true;
        }
        if (ImGui::IsItemDeactivated()) numericRecorded = false;
        return changed;
    }

    std::optional<std::string> chooseAsset(const char* label, const char* preview,
        const std::vector<std::string>& assets, bool allowNone = false)
    {
        if (!ImGui::BeginCombo(label, preview)) return std::nullopt;
        if (ImGui::IsWindowAppearing())
        {
            assetSearch.Clear();
            ImGui::SetKeyboardFocusHere();
        }
        assetSearch.Draw(u8"素材を検索##assetSearch", 180.0f);
        std::optional<std::string> choice;
        if (allowNone && ImGui::Selectable(u8"なし")) choice = std::string{};
        bool found = false;
        for (const auto& path : assets)
        {
            if (!assetSearch.PassFilter(path.c_str())) continue;
            found = true;
            if (ImGui::Selectable(path.c_str())) choice = path;
        }
        if (!found) ImGui::TextDisabled(u8"該当する素材がありません。");
        ImGui::EndCombo();
        return choice;
    }



    void editEnemyCharacter(Entry& entry)
    {
        if (!ImGui::CollapsingHeader(u8"敵モデルと被弾判定")) return;
        auto* enemy = static_cast<Enemy*>(entry.object);
        ImGui::TextWrapped("%s", entry.placement.asset.empty() ? u8"内蔵モデル" : entry.placement.asset.c_str());
        static char path[1024]{};
        ImGui::InputTextWithHint(u8"モデルのパス", "asset/model/enemy.fbx", path, IM_ARRAYSIZE(path));
        if (const auto file = chooseAsset("##enemyModel", u8"モデルを選択", models))
            strncpy_s(path, file->c_str(), _TRUNCATE);
        const bool assign = ImGui::Button(u8"リグ付きモデルを適用");
        ImGui::SameLine();
        ImGui::BeginDisabled(!enemy->hasCharacter());
        const bool reset = ImGui::Button(u8"内蔵モデルに戻す");
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
                status = assign ? u8"敵モデルを適用しました。" : u8"内蔵モデルに戻しました。";
            }
            else status = u8"asset/ 内の、ボーンにウェイトが設定されたモデルを指定してください。";
        }
        if (!enemy->hurtboxCount()) return;
        ImGui::Checkbox(u8"被弾判定を表示", &showHurtboxes);
        if (activeHurtbox >= static_cast<int>(enemy->hurtboxCount())) activeHurtbox = 0;
        if (ImGui::BeginCombo(u8"ボーン", enemy->hurtboxBone(activeHurtbox).c_str()))
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
        if (recordNumeric(ImGui::DragFloat3(u8"オフセット (バインド時のXYZ)", offset, .005f, -1, 1,
            "%.3f", ImGuiSliderFlags_AlwaysClamp), entry.placement))
            edit.offset = { offset[0], offset[1], offset[2] };
        recordNumeric(ImGui::SliderFloat(u8"半径の倍率", &edit.radiusScale, .1f, 4, "%.2f",
            ImGuiSliderFlags_AlwaysClamp), entry.placement);
        recordNumeric(ImGui::SliderFloat(u8"長さの倍率", &edit.lengthScale, .1f, 4, "%.2f",
            ImGuiSliderFlags_AlwaysClamp), entry.placement);
        if (ImGui::Button(u8"この部位を初期化") && item != edits.end())
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
            ImGui::SetTooltip(u8"%.3f 秒  %s", marker.time, marker.file.empty() ? u8"WAVを選択" : marker.file.c_str());
        }
        if (moved) applyMotion(entry);
        return clicked || dragging;
    }

    void drawMotionPanel()
    {
        if (!motionPanel) return;
        ImGui::SetNextWindowPos({ 390, 360 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 650, 350 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"モーションとSE", &motionPanel))
        {
            if (selected < 0 || !motionCount(entries[selected].placement.kind))
                ImGui::TextUnformatted(u8"シーン内のヒロインか配置キャラクターを選択してください。");
            else
            {
                auto& entry = entries[selected];
                const bool enemy = isEnemy(entry.placement.kind);
                const char* heroineMotions[] = { u8"待機", u8"歩き", u8"走り", u8"あくび" };
                const char* enemyMotions[] = { u8"待機", u8"移動", u8"パンチ", u8"叩きつけ", u8"突進" };
                if (ImGui::Combo(u8"モーション", &motionSlot, enemy ? enemyMotions : heroineMotions,
                    static_cast<int>(enemy ? 2 : motionCount(entry.placement.kind))))
                {
                    stopMotionPreview();
                    motionTime = 0;
                }
                auto& track = entry.placement.motions[motionSlot];
                const auto enemySlot = static_cast<Enemy::Motion>(motionSlot);
                const auto heroineSlot = static_cast<HeroineActionKind>(motionSlot);
                auto* actor = enemy ? static_cast<Enemy*>(entry.object) : nullptr;
                const auto rememberMotion = [&]()
                {
                    remember({ UndoKind::Modify, static_cast<size_t>(selected), entry.placement });
                };
                if (enemy)
                {
                    if (actor->hasCharacter())
                    {
                        ImGui::TextWrapped(u8"クリップ  %s", track.clip.empty() ? u8"内蔵モーション／バインドポーズ" : track.clip.c_str());
                        static char path[1024]{};
                        ImGui::InputTextWithHint(u8"クリップのパス", "asset/model/EnemyWalk.fbx", path, IM_ARRAYSIZE(path));
                        if (const auto file = chooseAsset("##enemyClip", u8"クリップを選択", models))
                            strncpy_s(path, file->c_str(), _TRUNCATE);
                        const bool assign = ImGui::Button(u8"クリップを適用");
                        ImGui::SameLine();
                        const bool clear = ImGui::Button(u8"既定のクリップに戻す");
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
                                motionStatus = u8"モーションを更新しました。";
                            }
                            else motionStatus = u8"asset/ 内の、ボーン名が一致するクリップを指定してください。";
                        }
                    }
                    else ImGui::TextUnformatted(u8"内蔵モーションを再生します。リグ付きモデルを適用するとクリップを差し替えられます。");
                }
                const float duration = enemy ? actor->motionDuration(enemySlot) : heroine->motionDuration(heroineSlot);
                const auto sample = [&]()
                {
                    if (enemy) actor->previewMotion(enemySlot, motionTime);
                    else heroine->previewMotion(heroineSlot, motionTime);
                    motionWasSampled = true;
                };
                if (duration > 0.0f)
                {
                    motionTime = std::min(motionTime, duration);
                    const bool canAttack = false;
                    const auto phase = canAttack ? Enemy::attackPhase(enemySlot) : Enemy::AttackPhase{};
                    if (drawMotionTimeline(entry, track.sounds, duration, phase)) sample();
                    if (phase.name)
                    {
                        constexpr const char* phaseLabels[] = { u8"パンチ", u8"衝撃波", u8"突進" };
                        const auto phaseIndex = motionSlot - static_cast<int>(Enemy::Motion::Punch);
                        ImGui::Text(u8"%s  %.2f～%.2f 秒", phaseLabels[phaseIndex], phase.start, phase.end);
                    }
                    if (ImGui::SliderFloat(u8"再生位置 (秒)", &motionTime, 0, duration, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                    {
                        motionPlaying = false;
                        motionFirstFrame = motionTime == 0;
                        SceneAudio::stopMarkers();
                        sample();
                    }
                    if (ImGui::Button(motionPlaying ? u8"一時停止" : u8"再生"))
                    {
                        motionPlaying = !motionPlaying;
                        if (motionPlaying && motionTime >= duration) { motionTime = 0; motionFirstFrame = true; }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"再生を停止")) { stopMotionPreview(); motionTime = 0; }
                    ImGui::SameLine();
                    if (ImGui::Button(u8"1フレーム進む"))
                    {
                        motionPlaying = false;
                        motionTime = std::min(duration, motionTime + 1.0f / 60.0f);
                        motionFirstFrame = false;
                        sample();
                    }
                    ImGui::SameLine();
                    ImGui::Checkbox(u8"ループ再生", &motionLoop);
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
                else ImGui::TextUnformatted(u8"このヒロインのモーションは再生できません。");

                if (ImGui::Button(u8"WAVを再検索")) scanAudio();
                ImGui::SameLine();
                ImGui::BeginDisabled(duration <= 0 || track.sounds.size() >= 24);
                if (ImGui::Button(u8"現在の位置にSEを追加"))
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
                        std::max(duration, marker.time), u8"%.3f 秒", ImGuiSliderFlags_AlwaysClamp), entry.placement))
                    {
                        marker.time = time;
                        changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(240);
                    if (const auto file = chooseAsset("##file", marker.file.empty() ? u8"WAVを選択" : marker.file.c_str(), audioAssets);
                        file && *file != marker.file)
                    {
                        if (SceneAudio::prepareMarker(*file))
                        {
                            rememberMotion();
                            marker.file = *file;
                            changed = true;
                        }
                        else motionStatus = u8"このWAVを読み込めませんでした。";
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
                    if (ImGui::SmallButton(u8"試聴")) SceneAudio::playMarker(marker, true);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(u8"削除"))
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

    void drawAmbientPanel()
    {
        if (!ambientPanel) return;
        ImGui::SetNextWindowPos({ 382, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 390, 380 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"環境音ゾーン", &ambientPanel))
        {
            ImGui::BeginDisabled(ambientZones.size() >= maxAmbientZones || placingAmbient);
            if (ImGui::Button(u8"地面をクリックして追加")) placingAmbient = true;
            ImGui::EndDisabled();
            if (placingAmbient)
            {
                ImGui::SameLine();
                if (ImGui::Button(u8"追加を取り消す")) placingAmbient = false;
            }
            ImGui::TextWrapped(u8"青い円の内側で再生し、端に向かって音量が小さくなります。");
            const auto label = [&](int index)
            {
                const auto& file = ambientZones[index].file;
                return std::to_string(index + 1) + "  " + (file.empty() ? std::string(u8"音声未設定")
                    : fs::u8path(file).stem().u8string());
            };
            if (ImGui::BeginCombo(u8"ゾーン", selectedAmbient < 0 ? u8"選択なし" : label(selectedAmbient).c_str()))
            {
                for (int i = 0; i < static_cast<int>(ambientZones.size()); ++i)
                    if (ImGui::Selectable(label(i).c_str(), i == selectedAmbient))
                    {
                        selectedAmbient = i;
                        SceneAudio::stopAmbientPreview();
                        ambientStatus.clear();
                    }
                ImGui::EndCombo();
            }
            if (selectedAmbient >= 0)
            {
                auto& zone = ambientZones[selectedAmbient];
                if (ImGui::Button(u8"WAVを再検索")) scanAudio();
                if (const auto file = chooseAsset(u8"環境音ファイル", zone.file.empty() ? u8"なし" : zone.file.c_str(),
                        audioAssets, true); file && *file != zone.file)
                {
                    rememberAmbient();
                    zone.file = *file;
                    SceneAudio::stopAmbientPreview();
                    ambientStatus.clear();
                }
                const auto dragValue = [&](const char* name, float& value, float speed, float low, float high)
                {
                    float next = value;
                    const bool changed = ImGui::DragFloat(name, &next, speed, low, high, "%.2f",
                        ImGuiSliderFlags_AlwaysClamp);
                    if (ImGui::IsItemActivated()) ambientDragRecorded = false;
                    if (changed)
                    {
                        if (!ambientDragRecorded) rememberAmbient();
                        ambientDragRecorded = true;
                        value = next;
                    }
                    if (ImGui::IsItemDeactivated()) ambientDragRecorded = false;
                };
                dragValue(u8"中心 X", zone.x, .25f, -100.0f, 100.0f);
                dragValue(u8"中心 Z", zone.z, .25f, -100.0f, 100.0f);
                dragValue(u8"聞こえる半径", zone.radius, .25f, 1.0f, 100.0f);
                dragValue(u8"最大音量 (0–1)", zone.volume, .01f, 0.0f, 1.0f);
                ImGui::BeginDisabled(zone.file.empty());
                if (ImGui::Button(u8"試聴"))
                    ambientStatus = SceneAudio::previewAmbient(zone) ? u8"最大音量で試聴中です。"
                        : u8"このWAVを再生できませんでした。";
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(u8"試聴停止"))
                {
                    SceneAudio::stopAmbientPreview();
                    ambientStatus.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button(u8"ゾーンを削除"))
                {
                    rememberAmbient();
                    ambientZones.erase(ambientZones.begin() + selectedAmbient);
                    selectedAmbient = -1;
                    SceneAudio::stopAmbientPreview();
                    ambientStatus.clear();
                }
            }
            if (!ambientStatus.empty()) ImGui::TextWrapped("%s", ambientStatus.c_str());
            ImGui::TextWrapped(u8"「配置を保存」でシーンに保存します。");
        }
        ImGui::End();
        if (!ambientPanel)
        {
            placingAmbient = false;
            SceneAudio::stopAmbientPreview();
            ambientStatus.clear();
        }
    }

    void drawAudioPanel()
    {
        if (!audioPanel) return;
        ImGui::SetNextWindowPos({ 382, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 390, 320 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"シーン音声", &audioPanel))
        {
            constexpr const char* names[] = { "BGM", u8"プレイヤーのジャンプ", u8"プレイヤーの着地", u8"敵のパンチ",
                u8"中ボスの叩きつけ", u8"中ボスの突進" };
            static_assert(IM_ARRAYSIZE(names) == static_cast<int>(SceneAudio::Cue::Count), "Audio cue labels");
            if (ImGui::Combo(u8"再生する場面", &audioCue, names, IM_ARRAYSIZE(names))) SceneAudio::stopPreview();
            const auto cue = static_cast<SceneAudio::Cue>(audioCue);
            const auto rememberAudio = [&]()
            {
                remember({ UndoKind::Audio, static_cast<size_t>(audioCue), {}, {}, SceneAudio::setting(cue) });
            };
            const auto& setting = SceneAudio::setting(cue);
            if (ImGui::Button(u8"WAVを再検索")) scanAudio();
            if (const auto file = chooseAsset(u8"音声ファイル", setting.file.empty() ? u8"なし" : setting.file.c_str(),
                    audioAssets, true); file && *file != setting.file)
            {
                const auto before = setting;
                if (SceneAudio::assign(cue, *file))
                {
                    remember({ UndoKind::Audio, static_cast<size_t>(audioCue), {}, {}, before });
                    audioStatus = file->empty() ? u8"音声の割り当てを解除しました。" : u8"音声を割り当てました。";
                }
                else audioStatus = u8"このWAVを読み込めません。PCMまたは対応する浮動小数点形式のWAVを使ってください。";
            }
            float volume = setting.volume * 100.0f;
            const bool volumeChanged = ImGui::SliderFloat(u8"音量", &volume, 0.0f, 100.0f, "%.0f%%",
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
            if (ImGui::Checkbox(cue == SceneAudio::Cue::Bgm ? u8"ゲーム中にループ再生" : u8"ゲーム中に再生", &enabled))
            {
                rememberAudio();
                SceneAudio::setEnabled(cue, enabled);
            }
            if (ImGui::Button(u8"試聴"))
                audioStatus = SceneAudio::preview(cue) ? u8"試聴中です。" : u8"このWAVを再生できませんでした。";
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(u8"停止")) { SceneAudio::stopPreview(); audioStatus.clear(); }
            if (ImGui::Button(u8"音声設定を保存"))
                audioStatus = SceneAudio::save() ? u8"音声設定を asset/scene-audio.txt に保存しました。" : u8"音声設定を保存できませんでした。";
            if (!audioStatus.empty()) ImGui::TextWrapped("%s", audioStatus.c_str());
            ImGui::TextWrapped(u8"WAVを追加するには asset/Audio/ に入れ、「WAVを再検索」を押してください。");
        }
        ImGui::End();
        if (!audioPanel) SceneAudio::stopPreview();
    }

    void drawPrefabPanel()
    {
        if (!prefabPanel) return;
        ImGui::SetNextWindowPos({ 390, 25 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 370, 265 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"敵プレハブ", &prefabPanel))
        {
            ImGui::SetNextItemWidth(190);
            if (ImGui::BeginCombo("##prefab", prefabChoice.empty() ? u8"プレハブを選択" : prefabChoice.c_str()))
            {
                for (const auto& prefab : prefabs)
                    if (ImGui::Selectable(prefab.first.c_str(), prefabChoice == prefab.first)) prefabChoice = prefab.first;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(prefabChoice.empty());
            if (ImGui::Button(u8"プレハブを配置"))
            {
                prefabToPlace = prefabChoice;
                terrainMode = false;
                waterMode = false;
                stopWaterEdit();
                drag.axis = -1;
            }
            ImGui::EndDisabled();
            if (!prefabToPlace.empty())
            {
                ImGui::TextWrapped(u8"地面をクリックして「%s」を配置します。", prefabToPlace.c_str());
                if (ImGui::Button(u8"配置をキャンセル")) prefabToPlace.clear();
            }

            ImGui::Separator();
            if (selected >= 0 && isEnemy(entries[selected].placement.kind))
            {
                const auto& instance = entries[selected].placement;
                if (!instance.prefab.empty())
                {
                    ImGui::TextWrapped(u8"リンク先  %s", instance.prefab.c_str());
                    if (ImGui::Button(u8"リンク済みの敵に一括反映")) updatePrefab(static_cast<size_t>(selected));
                    ImGui::SetItemTooltip(u8"選択中の敵の共通設定を、同じプレハブにリンクしている敵すべてに反映します。");
                }
                ImGui::SetNextItemWidth(190);
                ImGui::InputTextWithHint("##prefabName", u8"新しいプレハブ名", newPrefabName, IM_ARRAYSIZE(newPrefabName));
                ImGui::SameLine();
                ImGui::BeginDisabled(newPrefabName[0] == '\0');
                if (ImGui::Button(u8"プレハブを作成")) createPrefab(newPrefabName, static_cast<size_t>(selected));
                ImGui::EndDisabled();
            }
            else ImGui::TextUnformatted(u8"プレハブを作成・更新するには敵を選択してください。");
            if (ImGui::Button(u8"配置を保存"))
                status = saveLayout() ? u8"配置と敵プレハブを保存しました。" : u8"シーンの配置を保存できませんでした。";
        }
        ImGui::End();
        if (!prefabPanel) prefabToPlace.clear();
    }

    void waterValue(const char* label, float& value, float step, float min, float max,
        bool record = true, bool rebuild = true)
    {
        float next = value;
        const bool changed = ImGui::DragFloat(label, &next, step, min, max, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemActivated()) numericRecorded = false;
        if (changed && std::isfinite(next))
        {
            if (record && !numericRecorded)
            {
                rememberWater();
                numericRecorded = true;
            }
            value = next;
            if (rebuild) waterRenderer.invalidate();
        }
        if (ImGui::IsItemDeactivated()) numericRecorded = false;
    }

    void finishRiver()
    {
        auto river = riverDefaults;
        river.points = riverDraft;
        if (water.rivers.size() >= Wave::MaxRivers)
        {
            status = u8"これ以上は川を追加できません。";
            return;
        }
        if (!river.valid())
        {
            status = u8"位置が離れた点を2つ以上指定してください。";
            return;
        }
        rememberWater();
        water.rivers.push_back(std::move(river));
        selectedRiver = static_cast<int>(water.rivers.size()) - 1;
        stopWaterEdit();
        waterRenderer.invalidate();
        status = u8"川を配置しました。";
    }

    void editWater()
    {
        if (ImGui::Combo(u8"水域ツール", &waterTool, u8"池を塗る\0池を消す\0川を配置・編集\0くぼみを満たす\0"))
            stopWaterEdit();
        bool blend = water.alphaBlend;
        if (ImGui::Checkbox(u8"アルファブレンド", &blend))
        {
            rememberWater();
            water.alphaBlend = blend;
        }
        ImGui::BeginDisabled(!water.alphaBlend);
        waterValue(u8"不透明度", water.opacity, 0.01f, 0.0f, 1.0f, true, false);
        ImGui::EndDisabled();
        waterValue(u8"反射の強さ", water.reflection, 0.01f, 0.0f, 1.0f, true, false);
        ImGui::TextWrapped(u8"Q で下降  E で上昇");
        if (waterTool != 2)
        {
            waterValue(u8"池の水位", water.pondHeight, 0.05f, -30.0f, 30.0f);
            if (waterTool == 3)
            {
                ImGui::TextWrapped(u8"水位を底と岸の間に設定し、くぼみの底を左クリックします。Shift＋左クリックでその池を取り除けます。");
                if (!water.basins.empty() && ImGui::Button(u8"くぼみに入れた水をすべて除去"))
                {
                    rememberWater();
                    water.basins.clear();
                    basinMessage = nullptr;
                    waterRenderer.invalidate();
                }
                if (basinMessage) ImGui::TextWrapped("%s", basinMessage);
            }
            else
            {
                waterValue(u8"ブラシ半径", waterBrushRadius, 0.1f, 1.0f, 25.0f, false, false);
                ImGui::TextWrapped(u8"地面をドラッグして範囲を指定します。水面は水位より低い地形に沿います。");
            }
            return;
        }

        const auto riverName = [](int index)
        {
            return index < 0 ? std::string(u8"新しい川") : u8"川 " + std::to_string(index + 1);
        };
        if (ImGui::BeginCombo(u8"川を選択", riverName(selectedRiver).c_str()))
        {
            for (int i = -1; i < static_cast<int>(water.rivers.size()); ++i)
                if (ImGui::Selectable(riverName(i).c_str(), selectedRiver == i))
                {
                    selectedRiver = i;
                    stopWaterEdit();
                }
            ImGui::EndCombo();
        }
        const bool existing = selectedRiver >= 0;
        auto& river = existing ? water.rivers[selectedRiver] : riverDefaults;
        waterValue(u8"川幅", river.width, 0.1f, 0.5f, 40.0f, existing, existing);
        waterValue(u8"川の水位", river.height, 0.05f, -30.0f, 30.0f, existing, existing);
        waterValue(u8"流速", river.speed, 0.05f, 0.0f, 12.0f, existing, existing);
        if (existing)
        {
            if (ImGui::Button(u8"流れを反転"))
            {
                rememberWater();
                std::reverse(river.points.begin(), river.points.end());
                if (riverPoint >= 0) riverPoint = static_cast<int>(river.points.size()) - 1 - riverPoint;
                waterRenderer.invalidate();
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"川を削除"))
            {
                rememberWater();
                water.rivers.erase(water.rivers.begin() + selectedRiver);
                selectedRiver = -1;
                stopWaterEdit();
                waterRenderer.invalidate();
                return;
            }
            ImGui::BeginDisabled(riverPoint < 0 || river.points.size() <= 2);
            if (ImGui::Button(u8"選択点を削除"))
            {
                auto next = river;
                next.points.erase(next.points.begin() + riverPoint);
                if (next.valid())
                {
                    rememberWater();
                    river = std::move(next);
                    riverPoint = -1;
                    waterRenderer.invalidate();
                }
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped(u8"点をドラッグして形を変更します。Shift＋左クリックで下流に点を追加します。");
        }
        else
        {
            ImGui::TextWrapped(u8"上流から順に点をクリックし、Enterで配置を確定します。");
            ImGui::BeginDisabled(riverDraft.size() < 2);
            if (ImGui::Button(u8"川を確定")) finishRiver();
            ImGui::EndDisabled();
            if (!riverDraft.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button(u8"最後の点を取り消す")) riverDraft.pop_back();
            }
        }
    }

    void inspector()
    {
        ImGui::SetNextWindowPos({ 14, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 350, 620 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(u8"シーンエディター"))
        {
            ImGui::TextWrapped(u8"Escで編集を終了  Deleteで選択を削除\nF2で試遊  WASDで移動  Shiftで加速");
            ImGui::TextWrapped(u8"左クリックで選択  右ドラッグで回転\n中ドラッグで平行移動  ホイールでズーム");
            if (ImGui::Button(u8"音声"))
            {
                audioPanel = !audioPanel;
                if (audioPanel) scanAudio();
                else SceneAudio::stopPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"モーションとSE"))
            {
                motionPanel = !motionPanel;
                if (motionPanel) scanAudio();
                else stopMotionPreview();
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"プレハブ"))
            {
                prefabPanel = !prefabPanel;
                if (!prefabPanel) prefabToPlace.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button(u8"環境音"))
            {
                ambientPanel = !ambientPanel;
                if (ambientPanel)
                {
                    scanAudio();
                    if (selectedAmbient < 0 && !ambientZones.empty()) selectedAmbient = 0;
                }
                else
                {
                    placingAmbient = false;
                    SceneAudio::stopAmbientPreview();
                    ambientStatus.clear();
                }
            }
            if (ImGui::Button(u8"配置を保存"))
                status = saveLayout() ? std::string(u8"配置を保存しました。\n") + layoutPath
                    : u8"シーンの配置を保存できませんでした。";
            ImGui::SameLine();
            ImGui::BeginDisabled(history.empty());
            if (ImGui::Button(u8"元に戻す (Ctrl+Z)")) undo();
            ImGui::EndDisabled();
            if (ImGui::Button(u8"試遊を開始 (F2)")) playModeRequested = true;
            ImGui::SameLine();
            ImGui::Checkbox(u8"グリッドスナップ", &gridSnap);
            if (gridSnap)
            {
                float step = gridStep;
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::DragFloat(u8"グリッド間隔", &step, 0.1f, 0.1f, 50.0f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp) && std::isfinite(step))
                    gridStep = step;
            }
            if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
            ImGui::Separator();
            if (ImGui::Checkbox(u8"地形ブラシ", &terrainMode))
            {
                painting = false;
                previousMaterialHit.reset();
                if (terrainMode)
                {
                    waterMode = false;
                    stopWaterEdit();
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox(u8"ウォーターブラシ", &waterMode))
            {
                stopWaterEdit();
                if (waterMode)
                {
                    terrainMode = false;
                    painting = false;
                    prefabToPlace.clear();
                }
            }
            if (waterMode) editWater();
            else if (terrainMode)
            {
                if (ImGui::Combo(u8"ブラシ", &brushMode, u8"盛り上げる\0掘り下げる\0平らにする\0土を塗る\0草を塗る\0"))
                {
                    painting = false;
                    previousMaterialHit.reset();
                }
                ImGui::SliderFloat(u8"半径", &radius, 2.5f, 25.0f);
                if (brushMode >= 3)
                {
                    ImGui::SliderFloat(u8"塗りの強さ", &materialOpacity, 0.0f, 100.0f, "%.0f%%",
                        ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TextWrapped(u8"地面を左ドラッグして塗ります。ブラシの端は周囲になじみます。");
                }
                else
                {
                    ImGui::SliderFloat(u8"強さ", &strength, 0.5f, 15.0f);
                    if (brushMode == 2) ImGui::SliderFloat(u8"高さ", &flattenHeight, -30.0f, 30.0f);
                    ImGui::TextWrapped(u8"地面を左クリックしたままドラッグして地形を編集します。");
                }
            }
            else
            {
                ImGui::TextUnformatted(u8"オブジェクト一覧");
                hierarchySearch.Draw(u8"名前を検索##hierarchySearch", 125.0f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::BeginCombo(u8"種類", hierarchyKind == 0 ? u8"すべて" : labels[hierarchyKind - 1]))
                {
                    if (ImGui::Selectable(u8"すべて", hierarchyKind == 0)) hierarchyKind = 0;
                    for (int i = 0; i < IM_ARRAYSIZE(labels); ++i)
                        if (ImGui::Selectable(labels[i], hierarchyKind == i + 1)) hierarchyKind = i + 1;
                    ImGui::EndCombo();
                }
                if (ImGui::BeginChild("Hierarchy", { 0, 165 }, true))
                {
                    if (!hierarchySearch.IsActive() && hierarchyKind == 0)
                        ImGui::TextDisabled(u8"シーン機能  カメラ／地形／空");
                    bool found = false;
                    for (size_t i = 0; i < entries.size(); ++i)
                    {
                        const auto& p = entries[i].placement;
                        const auto kind = static_cast<int>(p.kind);
                        if (hierarchyKind != 0 && hierarchyKind != kind + 1) continue;
                        auto name = p.kind == Kind::Model ? fs::u8path(p.asset).filename().u8string()
                            : std::string(labels[kind]);
                        if (p.kind != Kind::Player) name += " (" + std::to_string(i) + ")";
                        if (!p.prefab.empty()) name += " [" + p.prefab + "]";
                        if (!hierarchySearch.PassFilter((name + " " + p.asset + " " + labels[kind]).c_str())) continue;
                        found = true;
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(name.c_str(), selected == static_cast<int>(i))) select(static_cast<int>(i));
                        ImGui::PopID();
                    }
                    if (!found) ImGui::TextDisabled(u8"該当するオブジェクトがありません。");
                }
                ImGui::EndChild();
                if (selected >= 0)
                {
                    auto& entry = entries[selected];
                    auto& t = entry.placement.transform;
                    float position[] = { t.position.x, t.position.y, t.position.z };
                    if (recordNumeric(ImGui::DragFloat3(u8"位置", position, 0.1f), entry.placement))
                        t.position = { position[0], position[1], position[2] };
                    if (entry.placement.kind != Kind::Player)
                    {
                        float angle[] = { XMConvertToDegrees(t.rotation.x), XMConvertToDegrees(t.rotation.y), XMConvertToDegrees(t.rotation.z) };
                        float scale[] = { t.scale.x, t.scale.y, t.scale.z };
                        if (recordNumeric(ImGui::DragFloat3(u8"回転 (度)", angle, 0.5f), entry.placement))
                            t.rotation = { XMConvertToRadians(angle[0]), XMConvertToRadians(angle[1]), XMConvertToRadians(angle[2]) };
                        if (recordNumeric(ImGui::DragFloat3(u8"スケール", scale, 0.02f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp), entry.placement))
                            t.scale = { scale[0], scale[1], scale[2] };
                    }
                    const bool enemy = isEnemy(entry.placement.kind);
                    bool alphaBlend = entry.placement.alphaBlend;
                    if (ImGui::Checkbox(u8"アルファブレンド", &alphaBlend))
                    {
                        remember({ UndoKind::Appearance, static_cast<size_t>(selected), entry.placement });
                        entry.placement.alphaBlend = alphaBlend;
                    }
                    ImGui::BeginDisabled(!entry.placement.alphaBlend);
                    float opacity = entry.placement.opacity * 100.0f;
                    const bool opacityChanged = ImGui::SliderFloat(u8"不透明度", &opacity,
                        0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
                    if (recordNumeric(opacityChanged && std::isfinite(opacity), entry.placement, UndoKind::Appearance))
                        entry.placement.opacity = opacity / 100.0f;
                    ImGui::EndDisabled();
                    if (supportsPhysics(entry.placement.kind) || enemy)
                    {
                        const auto before = entry.placement;
                        if (ImGui::Checkbox(enemy ? u8"メッシュコライダー (自動)" : u8"コライダー (形状に合わせる)", &entry.placement.collider))
                            remember({ UndoKind::Modify, static_cast<size_t>(selected), before });
                    }
                    if (supportsPhysics(entry.placement.kind))
                    {
                        const auto before = entry.placement;
                        if (ImGui::Checkbox(u8"重力を使う", &entry.placement.gravity))
                            remember({ UndoKind::Modify, static_cast<size_t>(selected), before });
                    }
                    if (enemy)
                    {
                        editEnemyCharacter(entry);
                    }
                    apply(entry);
                    if (ImGui::Button(u8"対象に注目")) focus = t.position + Vector3{ 0, 1, 0 };
                    if (entry.placement.kind != Kind::Player && entry.placement.kind != Kind::Heroine)
                    {
                        ImGui::SameLine();
                        const bool duplicate = ImGui::Button(u8"複製");
                        ImGui::SameLine();
                        const bool remove = ImGui::Button(u8"削除");
                        if (duplicate)
                        {
                            Placement copy = entry.placement;
                            copy.transform.position.x += 1.0f;
                            if (spawn(copy))
                            {
                                remember({ UndoKind::Add, entries.size() - 1 });
                                select(static_cast<int>(entries.size() - 1));
                            }
                            else status = u8"オブジェクトを複製できませんでした。";
                        }
                        else if (remove) deleteSelected();
                    }
                }
                ImGui::Separator();
                static char path[1024]{};
                if (ImGui::Button(u8"モデルを検索")) refreshModels();
                ImGui::SameLine();
                if (const auto file = chooseAsset("##models", u8"モデルを選択", models))
                    strncpy_s(path, file->c_str(), _TRUNCATE);
                ImGui::InputTextWithHint("##path", "asset/model/example.fbx", path, IM_ARRAYSIZE(path));
                if (ImGui::Button(u8"モデルを配置"))
                {
                    Placement item{ Kind::Model, {}, {}, true };
                    item.transform.position = focus;
                    const bool onTerrain = snapPlacementToGround(item.transform.position);
                    if (gridSnap && !onTerrain)
                        status = u8"グリッド位置が地形の外にあります。";
                    else if (modelPath(path, item.asset) && spawn(item))
                    {
                        status = u8"モデルを配置しました。";
                        remember({ UndoKind::Add, entries.size() - 1 });
                        select(static_cast<int>(entries.size() - 1));
                    }
                    else status = u8"モデルを読み込めません。asset/ 内のFBX、OBJ、glTFを指定してください。";
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

    void drawRangeRing(const Vector3& origin, float radius, ImU32 color, float thickness,
        FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay, bool followTerrain = false)
    {
        if (radius <= 0.0f) return;
        ImVec2 previous{};
        bool wasVisible = false;
        for (int i = 0; i <= 48; ++i)
        {
            const float angle = i * XM_2PI / 48;
            auto point = origin + Vector3{ radius * cosf(angle), 0, radius * sinf(angle) };
            const bool onTerrain = !followTerrain || terrain->tryGetHeight(point.x, point.z, point.y);
            if (followTerrain) point.y += .12f;
            ImVec2 pixel;
            const bool visible = onTerrain && project(point, vp, size, pixel);
            if (wasVisible && visible) overlay.AddLine(previous, pixel, color, thickness);
            if (visible) previous = pixel;
            wasVisible = visible;
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

    void drawPlacementGrid(FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        constexpr int halfCells = 12;
        const float span = halfCells * gridStep;
        Vector3 center{ snapCoordinate(focus.x), 0, snapCoordinate(focus.z) };
        terrain->tryGetHeight(center.x, center.z, center.y);
        center.y += 0.03f;
        const auto line = [&](const Vector3& from, const Vector3& to, ImU32 color)
        {
            ImVec2 start, end;
            if (project(center + from, vp, size, start) && project(center + to, vp, size, end))
                overlay.AddLine(start, end, color);
        };
        for (int i = -halfCells; i <= halfCells; ++i)
        {
            const float offset = i * gridStep;
            const ImU32 color = i == 0 ? IM_COL32(177, 200, 225, 140) : IM_COL32(145, 173, 201, 80);
            line({ offset, 0, -span }, { offset, 0, span }, color);
            line({ -span, 0, offset }, { span, 0, offset }, color);
        }
    }

    void paintWaterStroke(const Vector3& hit)
    {
        auto before = waterUndoRecorded ? nullptr : std::make_unique<Wave::Layout>(water);
        const auto start = previousWaterHit.value_or(hit);
        const auto delta = hit - start;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(delta.x, delta.z)
            / (waterBrushRadius * 0.5f))));
        bool changed = false;
        for (int i = 1; i <= steps; ++i)
        {
            const auto point = start + delta * (static_cast<float>(i) / steps);
            changed = water.paintPond(point.x, point.z, waterBrushRadius, waterTool == 0) || changed;
        }
        previousWaterHit = hit;
        if (!changed) return;
        if (before)
        {
            EditRecord edit{ UndoKind::Water };
            edit.water = std::move(before);
            remember(std::move(edit));
            waterUndoRecorded = true;
        }
        waterRenderer.invalidate();
    }

    void drawRiverGuide(const Wave::River& river, FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        const auto sections = river.sections();
        if (sections.empty()) return;
        const float half = river.width * 0.5f;
        const auto projectSection = [&](const XMFLOAT2& point, ImVec2& screen)
        {
            return project({ point.x, river.height + .07f, point.y }, vp, size, screen);
        };
        const auto color = IM_COL32(88, 229, 255, 245);
        const auto fill = IM_COL32(49, 201, 237, 42);
        float nextArrow = std::min(4.0f, sections.back().distance * 0.45f);
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto& section = sections[i];
            ImVec2 left, right;
            if (!projectSection(section.across(-half), left) || !projectSection(section.across(half), right))
                continue;
            if (i > 0)
            {
                ImVec2 previousLeft, previousRight;
                if (projectSection(sections[i - 1].across(-half), previousLeft)
                    && projectSection(sections[i - 1].across(half), previousRight))
                {
                    // ImGui's filled shapes use clockwise screen winding for antialiasing.
                    const float area = (right.x - left.x) * (previousLeft.y - left.y)
                        - (right.y - left.y) * (previousLeft.x - left.x);
                    const auto flags = overlay.Flags;
                    overlay.Flags &= ~ImDrawListFlags_AntiAliasedFill;
                    if (area >= 0) overlay.AddQuadFilled(left, right, previousRight, previousLeft, fill);
                    else overlay.AddQuadFilled(left, previousLeft, previousRight, right, fill);
                    overlay.Flags = flags;
                    overlay.AddLine(previousLeft, left, color, 1.6f);
                    overlay.AddLine(previousRight, right, color, 1.6f);
                }
            }
            if (i == 0 || i + 1 == sections.size()) overlay.AddLine(left, right, color, 1.6f);
            if (section.distance < nextArrow) continue;
            ImVec2 center, ahead;
            if (!projectSection(section.center, center)
                || !projectSection({ section.center.x + section.tangent.x, section.center.y + section.tangent.y }, ahead))
                continue;
            const float dx = ahead.x - center.x, dy = ahead.y - center.y;
            const float length = std::hypot(dx, dy);
            if (length > 1.0f)
            {
                const float ux = dx / length, uy = dy / length;
                overlay.AddTriangleFilled({ center.x + ux * 6, center.y + uy * 6 },
                    { center.x - ux * 5 - uy * 4, center.y - uy * 5 + ux * 4 },
                    { center.x - ux * 5 + uy * 4, center.y - uy * 5 - ux * 4 }, color);
            }
            nextArrow += 7.0f;
        }
    }

    int drawRiverPoints(const std::vector<XMFLOAT2>& points, FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int hovered = -1;
        float nearest = 12.0f * 12.0f;
        for (size_t i = 0; i < points.size(); ++i)
        {
            Vector3 point{ points[i].x, 0, points[i].y };
            if (!terrain->tryGetHeight(point.x, point.z, point.y)) continue;
            point.y += 0.10f;
            ImVec2 screen;
            if (!project(point, vp, size, screen)) continue;
            const float dx = mouse.x - screen.x, dy = mouse.y - screen.y;
            const float distance = dx * dx + dy * dy;
            if (distance < nearest)
            {
                nearest = distance;
                hovered = static_cast<int>(i);
            }
            overlay.AddCircleFilled(screen, 6.0f, IM_COL32(12, 30, 37, 230));
            overlay.AddCircleFilled(screen, 4.0f, static_cast<int>(i) == riverPoint
                ? IM_COL32(255, 179, 90, 255) : IM_COL32(88, 229, 255, 255));
        }
        return hovered;
    }

    bool appendRiverPoint(std::vector<XMFLOAT2>& points, const XMFLOAT2& point)
    {
        if (points.size() >= Wave::MaxRiverPoints || (!points.empty()
            && std::hypot(point.x - points.back().x, point.y - points.back().y) < 0.5f)) return false;
        points.push_back(point);
        return true;
    }

    void waterViewport(bool overScene, FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        Vector3 hit{};
        bool hasHit = overScene && terrainHit(viewRay(ImGui::GetIO().MousePos, size, vp), hit);
        if (waterTool == 3)
        {
            if (!hasHit) return;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const bool fill = !ImGui::GetIO().KeyShift;
                auto before = std::make_unique<Wave::Layout>(water);
                if (water.editBasin(*terrain, hit.x, hit.z, fill))
                {
                    EditRecord edit{ UndoKind::Water };
                    edit.water = std::move(before);
                    remember(std::move(edit));
                    basinMessage = nullptr;
                    waterRenderer.invalidate();
                }
                else
                {
                    basinMessage = fill
                        ? u8"現在の水位で満たせる新しいくぼみがありません。水位と岸の高さを確認してください。"
                        : u8"この位置に除去できるくぼみの水はありません。";
                }
            }
            drawRangeRing(hit + Vector3{ 0, .06f, 0 }, .35f, hit.y < water.pondHeight
                ? IM_COL32(88, 229, 255, 245) : IM_COL32(255, 160, 90, 245), 2.0f, vp, size, overlay);
            return;
        }
        if (waterTool != 2)
        {
            if (click)
            {
                waterStroke = hasHit;
                waterUndoRecorded = false;
                previousWaterHit.reset();
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) waterStroke = false;
            if (!hasHit) { previousWaterHit.reset(); return; }
            if (waterStroke) paintWaterStroke(hit);
            drawRangeRing(hit + Vector3{ 0, .06f, 0 }, waterBrushRadius,
                waterTool == 0 ? IM_COL32(88, 229, 255, 245) : IM_COL32(255, 160, 90, 245),
                2.0f, vp, size, overlay);
            return;
        }
        if (hasHit && gridSnap) hasHit = snapPlacementToGround(hit);
        const XMFLOAT2 point{ hit.x, hit.z };
        if (selectedRiver >= 0)
        {
            auto& river = water.rivers[selectedRiver];
            drawRiverGuide(river, vp, size, overlay);
            const int hovered = drawRiverPoints(river.points, vp, size, overlay);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                riverDragging = false;
                riverMovementRecorded = false;
            }
            if (overScene && click)
            {
                if (hasHit && ImGui::GetIO().KeyShift)
                {
                    auto next = river;
                    if (appendRiverPoint(next.points, point) && next.valid())
                    {
                        rememberWater();
                        river = std::move(next);
                        riverPoint = static_cast<int>(river.points.size()) - 1;
                        waterRenderer.invalidate();
                    }
                }
                else
                {
                    riverPoint = hovered;
                    riverDragging = hovered >= 0;
                    riverMovementRecorded = false;
                }
            }
            if (riverDragging && hasHit && !click)
            {
                const auto previous = river.points[riverPoint];
                if (std::hypot(previous.x - point.x, previous.y - point.y) >= 0.01f)
                {
                    auto next = river;
                    next.points[riverPoint] = point;
                    if (next.valid())
                    {
                        if (!riverMovementRecorded)
                        {
                            rememberWater();
                            riverMovementRecorded = true;
                        }
                        river = std::move(next);
                        waterRenderer.invalidate();
                    }
                }
            }
            return;
        }

        if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && riverDraft.size() >= 2
            && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        {
            finishRiver();
            return;
        }

        auto preview = riverDefaults;
        preview.points = riverDraft;
        if (hasHit)
        {
            ImVec2 cursor;
            if (project(hit, vp, size, cursor))
                overlay.AddCircle(cursor, 5.0f, IM_COL32(88, 229, 255, 245), 16, 2.0f);
            const bool appended = appendRiverPoint(preview.points, point);
            if (click && appended) riverDraft.push_back(point);
            else if (click && riverDraft.size() >= Wave::MaxRiverPoints)
                status = u8"川に配置できる点は32個までです。";
            if (preview.points.size() == 1)
                drawRangeRing({ point.x, riverDefaults.height + .07f, point.y }, riverDefaults.width * 0.5f,
                    IM_COL32(88, 229, 255, 210), 1.5f, vp, size, overlay);
        }
        drawRiverGuide(preview, vp, size, overlay);
        drawRiverPoints(riverDraft, vp, size, overlay);
    }

    bool ambientViewport(bool overScene, FXMMATRIX vp, const ImVec2& size, ImDrawList& overlay)
    {
        if (!ambientPanel) return false;
        const auto guide = [&](const SceneAudio::AmbientZone& zone, bool active)
        {
            Vector3 center{ zone.x, 0, zone.z };
            terrain->tryGetHeight(center.x, center.z, center.y);
            center.y += .12f;
            const ImU32 color = active ? IM_COL32(88, 215, 255, 250) : IM_COL32(91, 166, 207, 135);
            drawRangeRing(center, zone.radius, color, active ? 2.2f : 1.2f, vp, size, overlay, true);
            ImVec2 pixel;
            if (project(center, vp, size, pixel)) overlay.AddCircleFilled(pixel, active ? 5.0f : 3.0f, color);
        };
        for (int i = 0; i < static_cast<int>(ambientZones.size()); ++i)
            guide(ambientZones[i], i == selectedAmbient);
        if (!placingAmbient) return false;

        Vector3 hit;
        if (overScene && terrainHit(viewRay(ImGui::GetIO().MousePos, size, vp), hit)
            && snapPlacementToGround(hit))
        {
            SceneAudio::AmbientZone zone;
            zone.x = hit.x;
            zone.z = hit.z;
            guide(zone, true);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ambientZones.size() < maxAmbientZones)
            {
                rememberAmbient();
                ambientZones.push_back(std::move(zone));
                selectedAmbient = static_cast<int>(ambientZones.size()) - 1;
                placingAmbient = false;
                SceneAudio::stopAmbientPreview();
                ambientStatus = u8"ゾーンに環境音ファイルを割り当ててください。";
            }
        }
        return true;
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
            pitch = std::max(waterMode ? -1.48f : -0.15f, std::min(1.48f, pitch + io.MouseDelta.y * 0.008f));
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
            const float lift = waterMode ? float(ImGui::IsKeyDown(ImGuiKey_E)) - float(ImGui::IsKeyDown(ImGuiKey_Q)) : 0.0f;
            const float magnitude = sqrtf(forward * forward + right * right + lift * lift);
            if (magnitude > 0.0f)
            {
                const float speed = io.KeyShift ? 24.0f : 8.0f;
                focus += (Vector3{ 0, lift, 0 } + Vector3{ sinf(yaw), 0, cosf(yaw) } * forward
                    + Vector3{ cosf(yaw), 0, -sinf(yaw) } * right)
                    * (speed * std::min(io.DeltaTime, 0.05f) / magnitude);
            }
        }

        Vector3 eye;
        XMMATRIX view, projection;
        SceneEditor::cameraMatrices(eye, view, projection);
        const XMMATRIX vp = view * projection;
        auto* overlay = ImGui::GetBackgroundDrawList();
        if (gridSnap && !terrainMode) drawPlacementGrid(vp, size, *overlay);

        if (ambientViewport(overScene, vp, size, *overlay))
        {
            drag.axis = -1;
            stopWaterEdit();
            painting = false;
            return;
        }

        if (waterMode)
        {
            drag.axis = -1;
            waterViewport(overScene, vp, size, *overlay);
            return;
        }

        if (!prefabToPlace.empty())
        {
            if (terrainMode)
                prefabToPlace.clear();
            else if (overScene)
            {
                Vector3 hit;
                if (terrainHit(viewRay(io.MousePos, size, vp), hit) && snapPlacementToGround(hit))
                {
                    ImVec2 cursor;
                    if (project(hit, vp, size, cursor))
                        overlay->AddCircle(cursor, 7.0f, IM_COL32(255, 210, 80, 255), 16, 2.0f);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        const bool placed = placePrefab(prefabToPlace, hit);
                        status = placed ? u8"プレハブを配置しました。" : u8"プレハブを配置できません。素材とオブジェクト数の上限を確認してください。";
                        if (placed) prefabToPlace.clear();
                        return;
                    }
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
            }
        }

        if (terrainMode)
        {
            drag.axis = -1;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                painting = overScene;
                brushRecorded = false;
                previousMaterialHit.reset();
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) painting = false;
            Vector3 hit;
            if (!overScene || !terrainHit(viewRay(io.MousePos, size, vp), hit))
            {
                previousMaterialHit.reset();
                return;
            }
            if (painting && brushMode >= 3)
            {
                auto before = brushRecorded ? nullptr
                    : std::make_unique<MeshField::MaterialPaint>(terrain->materialPaint());
                const float dt = std::max(0.01f, std::min(0.05f, io.DeltaTime));
                const float opacity = 1.0f - std::pow(1.0f - materialOpacity * 0.01f, dt * 30.0f);
                const bool changed = terrain->paintMaterial(previousMaterialHit.value_or(hit),
                    hit, radius, opacity, brushMode == 4);
                previousMaterialHit = hit;
                if (before && changed)
                {
                    EditRecord edit{ UndoKind::TerrainPaint };
                    edit.materialPaint = std::move(before);
                    remember(std::move(edit));
                    brushRecorded = true;
                }
            }
            else if (painting)
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
                if (visible && hasPrevious)
                    overlay->AddLine(previous, pixel, brushMode == 4
                        ? IM_COL32(115, 235, 111, 255) : IM_COL32(255, 206, 77, 255), 2.0f);
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
            const float movement = (io.MousePos.x - drag.mouse.x) * drag.unit.x
                + (io.MousePos.y - drag.mouse.y) * drag.unit.y;
            auto& coordinate = axisValue(next, drag.axis);
            if (!gridSnap || drag.recorded || fabsf(movement) >= 2.0f)
            {
                coordinate += movement / drag.screenUnits;
                if (gridSnap) coordinate = snapCoordinate(coordinate);
            }
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

void SceneEditor::useRuralMap()
{
    layoutPath = "asset/maps/rural-town/scene-layout.txt";
    temporaryPath = "asset/maps/rural-town/scene-layout.txt.tmp";
}

void SceneEditor::init(Player& scenePlayer, MeshField& sceneTerrain)
{
    player = &scenePlayer;
    heroine = Manager::GetGameObj<Heroine>();
    terrain = &sceneTerrain;
    const bool exists = GetFileAttributesA(layoutPath) != INVALID_FILE_ATTRIBUTES;
    if (!loadLayout())
    {
        defaults();
        if (exists) status = u8"配置を読み込めなかったため、初期配置を使います。";
    }
    if (heroine && std::none_of(entries.begin(), entries.end(), [](const Entry& e) { return e.placement.kind == Kind::Heroine; }))
    {
        Placement placement{ Kind::Heroine, { heroine->GetPosition(), heroine->GetRotation(), heroine->GetScale() }, {} };
        const auto previous = std::find_if(entries.begin(), entries.end(), [](const Entry& e) { return e.placement.kind == Kind::Player; });
        if (previous != entries.end())
            for (size_t i = 0; i < heroineMotionCount; ++i) placement.motions[i] = previous->placement.motions[i];
        spawn(placement);
    }
    focus = player->GetPosition() + Vector3{ 0, 1, 6 };
}

void SceneEditor::shutdown()
{
    stopMotionPreview();
    history.clear();
    prefabs.clear();
    prefabPanel = false;
    prefabChoice.clear();
    prefabToPlace.clear();
    newPrefabName[0] = '\0';
    audioAssets.clear();
    audioStatus.clear();
    audioPanel = false;
    audioCue = 0;
    audioVolumeRecorded = false;
    ambientZones.clear();
    selectedAmbient = -1;
    ambientPanel = false;
    placingAmbient = false;
    ambientDragRecorded = false;
    ambientStatus.clear();
    motionPanel = false;
    motionStatus.clear();
    motionLoop = false;
    entries.clear();
    editWorld.reset();
    playModeRequested = false;
    models.clear();
    modelsScanned = false;
    hierarchySearch.Clear();
    hierarchyKind = 0;
    assetSearch.Clear();
    gridSnap = false;
    gridStep = 1.0f;
    status.clear();
    player = nullptr;
    heroine = nullptr;
    terrain = nullptr;
    open = false;
    terrainMode = false;
    water = {};
    waterRenderer.reset();
    riverDefaults = {};
    waterMode = false;
    waterTool = 0;
    selectedRiver = -1;
    waterBrushRadius = 4.0f;
    stopWaterEdit();
    showHurtboxes = false;
    activeHurtbox = 0;
    select(-1);
}

bool SceneEditor::isAvailable() { return player && terrain; }
bool SceneEditor::isOpen() { return open; }
bool SceneEditor::isPlaying() { return editWorld.has_value(); }

void SceneEditor::toggle()
{
    if (!isAvailable()) return;
    if (open || editWorld)
    {
        playModeRequested = true;
        return;
    }
    open = true;
    SceneAudio::setEditing(true);
    if (!modelsScanned) refreshModels();
    for (auto& entry : entries) apply(entry);
}

void SceneEditor::close()
{
    if (!open) return;
    stopMotionPreview();
    stopWaterEdit();
    prefabToPlace.clear();
    playModeRequested = false;
    drag.axis = -1;
    painting = false;
    placingAmbient = false;
    ambientStatus.clear();
    open = false;
    SceneAudio::setEditing(false);
}

void SceneEditor::processPlayMode()
{
    if (!playModeRequested) return;
    playModeRequested = false;
    const auto restore = []()
    {
        Manager::endEditorPlay();
        player = editWorld->player;
        heroine = editWorld->heroine;
        terrain = editWorld->terrain;
        entries.swap(editWorld->entries);
        editWorld.reset();
        open = true;
        SceneAudio::setEditing(true);
        Input::SetMouseCapture(false);
    };
    if (editWorld)
    {
        restore();
        status = u8"試遊を終了し、開始前の編集状態に戻りました。";
        return;
    }
    if (!open) return;

    const auto orbit = Manager::GetGameObj<Camera>()->orbitSettings();
    stopMotionPreview();
    stopWaterEdit();
    if (!Manager::beginEditorPlay()) return;
    editWorld.emplace(EditWorld{ player, heroine, terrain, {} });
    entries.swap(editWorld->entries);
    const auto [playPlayer, playTerrain] = Game::createWorld();
    player = playPlayer;
    terrain = playTerrain;
    heroine = Manager::GetGameObj<Heroine>();
    heroine->toonSettings() = editWorld->heroine->toonSettings();
    Manager::GetGameObj<Camera>()->orbitSettings() = orbit;
    terrain->setHeights(editWorld->terrain->heights());
    terrain->setMaterialPaint(editWorld->terrain->materialPaint());
    for (const auto& entry : editWorld->entries)
    {
        if (spawn(entry.placement)) continue;
        restore();
        status = u8"試遊を開始できません。シーンが参照しているモデルを確認してください。";
        return;
    }
    prefabToPlace.clear();
    drag.axis = -1;
    painting = false;
    placingAmbient = false;
    ambientStatus.clear();
    open = false;
    SceneAudio::setEditing(false);
    Input::SetMouseCapture(GetFocus() == GetWindow());
}

void SceneEditor::drawPlayMode()
{
    if (!editWorld) return;
    constexpr char label[] = u8"試遊中  F2／Escで編集に戻る";
    const float x = std::max(12.0f, ImGui::GetIO().DisplaySize.x - ImGui::CalcTextSize(label).x - 24.0f);
    ImGui::SetNextWindowPos({ x, 12 });
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##editorPlayMode", nullptr, flags)) ImGui::TextUnformatted(label);
    ImGui::End();
}

void SceneEditor::drawWater(const Vector3& eye, FXMMATRIX viewProjection, float dt)
{
    if (!terrain) return;
    waterRenderer.draw(water, *terrain, eye, viewProjection, dt);
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
    if (!io.WantTextInput && !ImGui::IsAnyItemActive()
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup))
    {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected();
    }
    inspector();
    drawPrefabPanel();
    drawAudioPanel();
    drawAmbientPanel();
    drawMotionPanel();
    viewport();
}

void SceneEditor::updatePhysics(float dt)
{
    if (!terrain || open || dt <= 0.0f) return;
    const auto listener = player->GetPosition();
    SceneAudio::updateAmbient(ambientZones, listener.x, listener.z, dt);
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
