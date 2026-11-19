#include "main.h"
#include "renderer.h"
#include "GameObject.h"
#include "manager.h"
#include "SceneEditor.h"
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
#include <wrl/client.h>

namespace
{
    namespace fs = std::filesystem;
    constexpr char layoutPath[] = "asset/scene-layout.txt";
    constexpr char temporaryPath[] = "asset/scene-layout.txt.tmp";
    constexpr size_t maxObjects = 512;

    enum class Kind { Player, Enemy, Boss, Box, Grass, Particle, Model };
    const char* const labels[] = { "Player spawn", "Enemy", "Mid boss", "Box", "Grass", "Particle", "Model" };

    struct Transform
    {
        Vector3 position{ 0, 0, 0 };
        Vector3 rotation{ 0, 0, 0 };
        Vector3 scale{ 1, 1, 1 };
    };
    struct Placement
    {
        Kind kind;
        Transform transform;
        std::string asset;
    };
    struct Entry
    {
        Placement placement;
        GameObject* object;
    };

    class StaticProp final : public GameObject
    {
    public:
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

    struct AxisDrag
    {
        int axis = -1;
        ImVec2 mouse;
        ImVec2 unit;
        float screenUnits = 1.0f;
        Vector3 start{ 0, 0, 0 };
    } drag;

    float& axisValue(Vector3& point, int axis)
    {
        return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
    }
    XMVECTOR vec(const Vector3& p) { return XMVectorSet(p.x, p.y, p.z, 0); }
    Vector3 vec(FXMVECTOR v) { return { XMVectorGetX(v), XMVectorGetY(v), XMVectorGetZ(v) }; }

    void apply(Entry& entry)
    {
        const auto& t = entry.placement.transform;
        entry.object->SetPosition(t.position);
        entry.object->SetRotation(t.rotation);
        entry.object->SetScale(t.scale);
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
        return true;
    }

    void defaults()
    {
        spawn({ Kind::Player, { player->GetPosition(), player->GetRotation(), player->GetScale() }, {} });
        spawn({ Kind::Enemy, { { -2.4f, 0, 5.4f }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {} });
        spawn({ Kind::Enemy, { { 0, 0, 5 }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {} });
        spawn({ Kind::Enemy, { { 2.4f, 0, 5.4f }, { 0, XM_PI, 0 }, { .75f, .75f, .75f } }, {} });
        spawn({ Kind::Boss, { { -1, 0, 15 }, { 0, XM_PI, 0 }, { 1.55f, 1.55f, 1.55f } }, {} });
        spawn({ Kind::Box, { { 5, 0, 5 }, { 0, 0, 0 }, { 2, 1, 2 } }, {} });
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

    bool loadLayout()
    {
        std::ifstream input(layoutPath);
        std::string signature;
        int version = 0, size = 0;
        size_t count = 0;
        if (!(input >> signature >> version >> count >> size)
            || signature != "SceneLayout" || version != 1 || count > maxObjects
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
            if (placement.kind == Kind::Player) ++playerCount;
            if (placement.kind == Kind::Model && !modelPath(placement.asset, placement.asset)) return false;
            placements.push_back(placement);
        }
        MeshField::Heights heights{};
        for (float& height : heights)
            if (!(input >> height) || !std::isfinite(height) || fabsf(height) > 30.0f) return false;
        if (playerCount != 1) return false;

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
        output << "SceneLayout 1 " << entries.size() << ' ' << MeshField::GridSize << '\n';
        output << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (const auto& entry : entries)
        {
            const auto& p = entry.placement;
            output << static_cast<int>(p.kind) << ' ';
            writeVector(output, p.transform.position);
            writeVector(output, p.transform.rotation);
            writeVector(output, p.transform.scale);
            output << std::quoted(p.asset) << '\n';
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
        selected = index;
        drag.axis = -1;
    }

    void inspector()
    {
        ImGui::SetNextWindowPos({ 14, 14 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 350, 620 }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene Editor"))
        {
            ImGui::TextWrapped("F2: edit/play   RMB: orbit   MMB: pan   Wheel: zoom");
            if (ImGui::Button("Save layout"))
                status = saveLayout() ? "Saved asset/scene-layout.txt." : "Could not save the scene layout.";
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
                    if (ImGui::DragFloat3("Position", position, 0.1f))
                        t.position = { position[0], position[1], position[2] };
                    if (entry.placement.kind != Kind::Player)
                    {
                        float angle[] = { XMConvertToDegrees(t.rotation.x), XMConvertToDegrees(t.rotation.y), XMConvertToDegrees(t.rotation.z) };
                        float scale[] = { t.scale.x, t.scale.y, t.scale.z };
                        if (ImGui::DragFloat3("Rotation (deg)", angle, 0.5f))
                            t.rotation = { XMConvertToRadians(angle[0]), XMConvertToRadians(angle[1]), XMConvertToRadians(angle[2]) };
                        if (ImGui::DragFloat3("Scale", scale, 0.02f, 0.01f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
                            t.scale = { scale[0], scale[1], scale[2] };
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
                            if (spawn(copy)) select(static_cast<int>(entries.size() - 1));
                            else status = "Could not duplicate this object.";
                        }
                        else if (remove)
                        {
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
                    Placement item{ Kind::Model, {}, {} };
                    item.transform.position = focus;
                    terrain->tryGetHeight(focus.x, focus.z, item.transform.position.y);
                    if (modelPath(path, item.asset) && spawn(item))
                    {
                        status = "Model added.";
                        select(static_cast<int>(entries.size() - 1));
                    }
                    else status = "Could not load the model. Use FBX, OBJ or glTF under asset/.";
                }
            }
        }
        ImGui::End();
    }

    bool project(const Vector3& point, FXMMATRIX vp, const ImVec2& size, ImVec2& pixel, float* depth = nullptr)
    {
        const XMVECTOR clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 0.01f) return false;
        pixel = { (XMVectorGetX(clip) / w + 1.0f) * size.x * 0.5f,
                  (1.0f - XMVectorGetY(clip) / w) * size.y * 0.5f };
        if (depth) *depth = w;
        return true;
    }

    bool terrainHit(const ImVec2& mouse, const ImVec2& size, FXMMATRIX vp, Vector3& point)
    {
        const XMMATRIX inverse = XMMatrixInverse(nullptr, vp);
        const float x = mouse.x * 2.0f / size.x - 1.0f;
        const float y = 1.0f - mouse.y * 2.0f / size.y;
        const XMVECTOR start = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverse);
        const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverse);
        const XMVECTOR direction = XMVector3Normalize(farPoint - start);
        const auto at = [&](float t) { return vec(start + direction * t); };
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

        Vector3 eye;
        XMMATRIX view, projection;
        SceneEditor::cameraMatrices(eye, view, projection);
        const XMMATRIX vp = view * projection;
        auto* overlay = ImGui::GetBackgroundDrawList();
        if (terrainMode)
        {
            drag.axis = -1;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) painting = overScene;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) painting = false;
            Vector3 hit;
            if (!overScene || !terrainHit(io.MousePos, size, vp, hit)) return;
            if (painting)
            {
                const float amount = strength * std::min(0.05f, io.DeltaTime);
                terrain->sculpt(hit.x, hit.z, radius, brushMode == 1 ? -amount : amount,
                    brushMode == 2 ? &flattenHeight : nullptr);
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
            position = drag.start;
            axisValue(position, drag.axis) += ((io.MousePos.x - drag.mouse.x) * drag.unit.x
                + (io.MousePos.y - drag.mouse.y) * drag.unit.y) / drag.screenUnits;
            apply(entry);
        }

        int pick = -1;
        float best = 14.0f;
        float nearestDepth = std::numeric_limits<float>::max();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            ImVec2 pixel;
            float depth = 0.0f;
            auto point = entries[i].placement.transform.position;
            point.y += 0.35f;
            if (!project(point, vp, size, pixel, &depth)) continue;
            const float gap = hypotf(io.MousePos.x - pixel.x, io.MousePos.y - pixel.y);
            if (gap < best || (gap == best && depth < nearestDepth))
            {
                best = gap;
                nearestDepth = depth;
                pick = static_cast<int>(i);
            }
            const bool active = selected == static_cast<int>(i);
            overlay->AddCircleFilled(pixel, active ? 6.0f : 4.0f,
                active ? IM_COL32(255, 211, 88, 255) : IM_COL32(232, 239, 250, 210));
        }

        if (selected >= 0)
        {
            const auto& point = entries[selected].placement.transform.position;
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
        if (click && drag.axis < 0 && pick >= 0) select(pick);
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
    entries.clear();
    models.clear();
    status.clear();
    player = nullptr;
    terrain = nullptr;
    open = false;
    terrainMode = false;
    select(-1);
}

bool SceneEditor::isAvailable() { return player && terrain; }
bool SceneEditor::isOpen() { return open; }

void SceneEditor::toggle()
{
    if (!isAvailable()) return;
    open = !open;
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
    inspector();
    viewport();
}
