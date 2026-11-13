// =========================================================
// ファイルの情報[PreviewScene.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "PreviewScene.h"
#include "manager.h"
#include "ActionSequence.h"
#include "PlayerAnimationAssets.h"
#include "ToonPanel.h"
#include "ToonPreset.h"
#include "imgui/imgui.h"

namespace
{
    struct Axis
    {
        XMFLOAT3 end;
        ImU32 color;
        const char* label;
    };

    constexpr Axis axes[] = {
            { { 1.0f, 0.0f, 0.0f }, IM_COL32(255, 94, 96, 255), "X" },
            { { 0.0f, 1.0f, 0.0f }, IM_COL32(109, 229, 137, 255), "Y" },
            { { 0.0f, 0.0f, 1.0f }, IM_COL32(103, 172, 255, 255), "Z" }
    };

    ImVec2 toScreen(FXMVECTOR point)
    {
        const auto* viewport = ImGui::GetMainViewport();
        const float w = XMVectorGetW(point);
        return { viewport->Pos.x + (0.5f + 0.5f * XMVectorGetX(point) / w) * viewport->Size.x,
            viewport->Pos.y + (0.5f - 0.5f * XMVectorGetY(point) / w) * viewport->Size.y };
    }

    void drawWorldAxes(FXMMATRIX view)
    {
        const auto transform = view * PreviewCamera::projection();
        const auto origin = XMVector4Transform(XMVectorSet(0, 0, 0, 1), transform);
        auto* draw = ImGui::GetBackgroundDrawList();
        for (const auto& axis : axes)
        {
            auto start = origin;
            auto end = XMVector4Transform(XMVectorSetW(XMLoadFloat3(&axis.end), 1.0f), transform);
            const float startZ = XMVectorGetZ(start);
            const float endZ = XMVectorGetZ(end);
            if (startZ < 0.0f && endZ < 0.0f)
                continue;
            if (startZ < 0.0f)
                start = XMVectorLerp(start, end, startZ / (startZ - endZ));
            if (endZ < 0.0f)
                end = XMVectorLerp(end, start, endZ / (endZ - startZ));

            const ImVec2 from = toScreen(start);
            const ImVec2 tip = toScreen(end);
            draw->AddLine(from, tip, IM_COL32(12, 15, 22, 230), 4.0f);
            draw->AddLine(from, tip, axis.color, 2.0f);
            if (endZ >= 0.0f)
            {
                draw->AddCircleFilled(tip, 3.0f, axis.color);
                draw->AddText(ImVec2(tip.x + 6, tip.y - 9), axis.color, axis.label);
            }
        }

        if (XMVectorGetZ(origin) >= 0.0f)
            draw->AddCircleFilled(toScreen(origin), 3.0f, IM_COL32_WHITE);
    }
}

// =========================================================
// ゲームから独立したステージに、同じ描画リソースを用意する
// =========================================================
void PreviewScene::Init()
{
    m_controls.motionNames = PlayerAnimationAssets::listMotions();
    m_toonShader.init();

    // 本編と同じ外観設定を使い、プレビューのカメラは独立させる。
    ToonPreset::loadAppearance(m_toonShader.settings());

    m_effect.Init();
    m_effect.SetScale({ ActionSequence::effectScale, ActionSequence::effectScale, ActionSequence::effectScale });

    const bool exportExists = GetFileAttributesA(PlayerAnimationAssets::previewModel) != INVALID_FILE_ATTRIBUTES;
    if (exportExists && loadModel(PlayerAnimationAssets::previewModel))
        return;

    if (loadModel(PlayerAnimationAssets::model) && exportExists)
        m_controls.assetStatus = "Blender export failed to load. Showing the built-in player.";
}

void PreviewScene::Uninit()
{
    if (m_model)
    {
        m_model->Uninit();
        m_model.reset();
    }

    m_weapon.clear();
    m_effect.Uninit();
    m_toonShader.uninit();
}

void PreviewScene::Update()
{
    m_timeline.advance(Manager::animationDeltaSeconds());
}

// =========================================================
// 読み込みに成功したモデルだけを差し替える
// =========================================================
bool PreviewScene::loadModel(const char* path)
{
    auto candidate = std::make_unique<AnimationModel>(&m_modelAnchor);
    using PlayerAnimationAssets::LoadResult;
    const auto result = PlayerAnimationAssets::loadCharacter(*candidate, path);
    if (result != LoadResult::loaded)
    {
        if (result == LoadResult::motionMismatch)
            m_controls.assetStatus = "Could not match the FBX rig to the player motions.";
        else if (result == LoadResult::missingTextures)
            m_controls.assetStatus = "Could not resolve embedded textures in the candidate FBX.";
        else
            m_controls.assetStatus = std::string("Could not load: ") + path;

        return false;
    }

    if (!PlayerAnimationAssets::loadMotion(*candidate, m_controls.clipName))
        m_controls.clipName = "Walk";

    // FBX の再読込後も、同じ名前のシェイプキーがあれば選択を引き継ぐ。
    const auto& names = candidate->morphNames();
    const auto findName = [&](int index, int fallback) {
        if (!m_model || index < 0 || static_cast<std::size_t>(index) >= m_model->morphNames().size())
            return fallback;

        const auto found = std::find(names.begin(), names.end(), m_model->morphNames()[index]);
        return found == names.end() ? fallback : static_cast<int>(found - names.begin());
    };
    m_controls.morphIndex = findName(m_controls.morphIndex, 0);
    m_controls.baseMorphIndex = findName(m_controls.baseMorphIndex, -1);
    auto& extras = m_controls.extraMorphs;
    extras.erase(std::remove_if(extras.begin(), extras.end(), [&](const MorphTarget& morph) {
        return names.empty() || morph.name == names[m_controls.morphIndex]
            || std::find(names.begin(), names.end(), morph.name) == names.end();
    }), extras.end());

    if (m_model)
        m_model->Uninit();
    m_model = std::move(candidate);
    m_controls.motionNames = PlayerAnimationAssets::listMotions();
    m_controls.motionStatus = nullptr;
    m_sampledClip.clear();
    const std::string loadedPath = path;
    strcpy_s(m_controls.characterPath, loadedPath.c_str());
    m_controls.assetStatus = "Loaded: " + loadedPath;
    return true;
}

// =========================================================
// 選択した FBX を検証し、成功したときだけプレビューを切り替える
// =========================================================
bool PreviewScene::selectMotion(const std::string& name)
{
    if (!m_model || !PlayerAnimationAssets::loadMotion(*m_model, name))
        return false;

    const auto canonical = PlayerAnimationAssets::canonicalMotionName(name);
    if (canonical != m_controls.clipName && !PlayerAnimationAssets::isBuiltInMotion(m_controls.clipName))
        m_model->unloadAnimation(m_controls.clipName.c_str());

    m_controls.clipName = canonical;
    return true;
}

// =========================================================
// 現在時刻から姿勢と表情を求め、停止中の同じ姿勢は再計算しない
// =========================================================
void PreviewScene::sampleModel()
{
    const float motionTime = m_timeline.trackEnabled(0) ? m_timeline.time : 0.0f;
    const auto& clip = m_controls.clipName;
    const float weight = m_timeline.faceWeight();
    const auto& morphs = m_model->morphNames();
    const bool faceEnabled = m_timeline.trackEnabled(1) && !morphs.empty();
    const int morph = faceEnabled ? m_controls.morphIndex : -1;
    const int baseMorph = faceEnabled ? m_controls.baseMorphIndex : -1;
    const float extraTime = faceEnabled && !m_controls.extraMorphs.empty() ? m_timeline.time : -1.0f;
    if (m_sampledClip == m_controls.clipName && m_sampledWeapon == m_controls.weapon.enabled
        && m_sampledMorph == morph &&
        m_sampledBaseMorph == baseMorph && m_sampledTime == motionTime && m_sampledWeight == weight
        && m_sampledExtraTime == extraTime && m_sampledExtraMorphs == m_controls.extraMorphs)
        return;

    static const std::string neutral;
    if (faceEnabled)
        m_model->setMorphBlend(baseMorph < 0 ? neutral : morphs[baseMorph], morphs[morph], weight,
            m_controls.extraMorphs, m_timeline.time);
    else
        m_model->setMorph(neutral, 0.0f);

    m_model->sampleBlendedClip(clip.c_str(), motionTime, 0.0f,
        AnimationModel::RootTravel::authored, false, !m_controls.weapon.enabled);
    m_sampledClip = m_controls.clipName;
    m_sampledWeapon = m_controls.weapon.enabled;
    m_sampledMorph = morph;
    m_sampledBaseMorph = baseMorph;
    if (m_sampledExtraMorphs != m_controls.extraMorphs)
        m_sampledExtraMorphs = m_controls.extraMorphs;
    m_sampledTime = motionTime;
    m_sampledWeight = weight;
    m_sampledExtraTime = extraTime;
}

void PreviewScene::editWeaponInViewport(FXMMATRIX view)
{
    auto& weapon = m_controls.weapon;
    const bool releasing = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !releasing)
        m_dragAxis = -1;
    if (m_selectedWeapon != weapon.model || !weapon.enabled)
    {
        m_selectedWeapon.clear();
        m_dragAxis = -1;
    }
    if (!m_weapon.ready() || !weapon.visible(m_timeline.time)
        || (m_timeline.soloTrack >= 0 && m_timeline.soloTrack != 3))
        return;

    const float times[] = { weapon.appear, weapon.middle, weapon.disappear };
    const int nearest = static_cast<int>(std::min_element(std::begin(times), std::end(times), [this](float a, float b) {
        return fabsf(a - m_timeline.time) < fabsf(b - m_timeline.time);
    }) - times);

    const auto& io = ImGui::GetIO();
    const auto* viewport = ImGui::GetMainViewport();
    const auto projection = PreviewCamera::projection();
    const auto nearPoint = XMVector3Unproject(XMVectorSet(io.MousePos.x, io.MousePos.y, 0, 1),
        viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y, 0, 1,
        projection, view, XMMatrixIdentity());
    const auto farPoint = XMVector3Unproject(XMVectorSet(io.MousePos.x, io.MousePos.y, 1, 1),
        viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y, 0, 1,
        projection, view, XMMatrixIdentity());
    const auto ray = XMVector3Normalize(farPoint - nearPoint);
    const auto axisParameter = [&](FXMVECTOR origin, int axis) {
        const auto direction = XMLoadFloat3(&axes[axis].end);
        const float dot = XMVectorGetX(XMVector3Dot(direction, ray));
        const auto delta = nearPoint - origin;
        const auto perpendicular = direction - ray * dot;
        return XMVectorGetX(XMVector3Dot(delta, perpendicular)) / std::max(0.0001f, 1.0f - dot * dot);
    };

    if (m_dragAxis >= 0 && ImGui::IsMousePosValid())
    {
        const float delta = axisParameter(XMLoadFloat3(&m_dragOrigin), m_dragAxis) - m_dragStart;
        float minimum = -20.0f, maximum = 20.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (m_dragKey >= 0 && i != m_dragKey)
                continue;
            minimum = std::max(minimum, -10.0f - m_dragPositions[i]);
            maximum = std::min(maximum, 10.0f - m_dragPositions[i]);
        }
        const float movement = std::max(minimum, std::min(maximum, delta));
        for (int i = 0; i < 3; ++i)
            if (m_dragKey < 0 || i == m_dragKey)
                weapon.keys[i].position[m_dragAxis] = m_dragPositions[i] + movement;
    }

    if (releasing)
        m_dragAxis = -1;

    const auto pose = weapon.sample(m_timeline.time);
    const auto origin = XMVectorSet(pose.position[0], pose.position[1], pose.position[2], 1.0f);
    const auto transform = view * projection;
    const auto clip = XMVector4Transform(origin, transform);
    const bool canClick = PreviewCamera::mouseOverViewport() && !m_camera.dragging() && !io.KeyAlt
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    int hovered = -1;
    float bestDistance = 9.0f * 9.0f;
    ImVec2 screenOrigin{};
    ImVec2 tips[3]{};
    bool drawn[3]{};
    if (!m_selectedWeapon.empty() && XMVectorGetZ(clip) >= 0.0f)
    {
        screenOrigin = toScreen(clip);
        const float length = 140.0f * XMVectorGetW(clip) / (viewport->Size.y * XMVectorGetY(projection.r[1]));
        for (int i = 0; i < 3; ++i)
        {
            const auto tip = XMVector4Transform(origin + XMLoadFloat3(&axes[i].end) * length, transform);
            if (XMVectorGetZ(tip) < 0.0f)
                continue;
            tips[i] = toScreen(tip);
            const float x = tips[i].x - screenOrigin.x, y = tips[i].y - screenOrigin.y;
            const float squared = x * x + y * y;
            if (squared < 12.0f * 12.0f)
                continue;

            drawn[i] = true;
            const float mx = io.MousePos.x - screenOrigin.x, my = io.MousePos.y - screenOrigin.y;
            const float t = std::max(0.18f, std::min(1.0f, (mx * x + my * y) / squared));
            const float dx = mx - t * x, dy = my - t * y;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance)
            {
                hovered = i;
                bestDistance = distance;
            }
        }
    }

    if (canClick && hovered >= 0)
    {
        m_timeline.playing = false;
        if (!m_controls.moveWholePath && m_timeline.time != times[nearest])
        {
            m_timeline.seek(times[nearest]);
            return;
        }
        m_dragAxis = hovered;
        m_dragKey = -1;
        XMStoreFloat3(&m_dragOrigin, origin);
        m_dragStart = axisParameter(origin, hovered);
        for (int i = 0; i < 3; ++i)
            m_dragPositions[i] = weapon.keys[i].position[hovered];
        if (!m_controls.moveWholePath)
            m_dragKey = nearest;
    }
    else if (canClick)
    {
        m_selectedWeapon = m_weapon.hitTest(weapon, m_timeline.time, nearPoint, ray) ? weapon.model : "";
        if (!m_selectedWeapon.empty())
        {
            m_timeline.playing = false;
            if (!m_controls.moveWholePath)
                m_timeline.seek(times[nearest]);
        }
    }

    if (m_selectedWeapon.empty())
        return;

    auto* draw = ImGui::GetBackgroundDrawList();
    for (int i = 0; i < 3; ++i)
    {
        if (!drawn[i])
            continue;
        const auto color = i == m_dragAxis || (i == hovered && PreviewCamera::mouseOverViewport())
            ? IM_COL32(255, 226, 122, 255) : axes[i].color;
        const auto tip = tips[i];
        const float x = tip.x - screenOrigin.x, y = tip.y - screenOrigin.y;
        const float size = sqrtf(x * x + y * y);
        const float dx = x / size, dy = y / size;
        draw->AddLine(screenOrigin, tip, IM_COL32(12, 15, 22, 230), 6.0f);
        draw->AddLine(screenOrigin, tip, color, 3.0f);
        draw->AddTriangleFilled(tip, { tip.x - dx * 12 - dy * 5, tip.y - dy * 12 + dx * 5 },
            { tip.x - dx * 12 + dy * 5, tip.y - dy * 12 - dx * 5 }, color);
        draw->AddText({ tip.x + 6, tip.y - 9 }, color, axes[i].label);
    }
}

void PreviewScene::Draw()
{
    const bool showUi = ImGui::GetCurrentContext() != nullptr;
    if (showUi)
    {
        static const std::vector<std::string> emptyMorphs;
        ToonPanel::beginFrame();
        if (PreviewPanel::draw(m_timeline, m_controls, m_camera, m_toonShader.settings(),
            m_model ? m_model->morphNames() : emptyMorphs,
            [this](const std::string& name) { return selectMotion(name); },
            [this](const std::string& path) {
                const bool loaded = m_weapon.load(path);
                m_controls.weaponStatus = !loaded ? "Could not load weapon FBX."
                    : (m_weapon.missingTextures() ? "Loaded weapon with missing textures." : "Weapon loaded.");
                return loaded;
            },
            [this]() { return m_model ? m_model->clipDuration(m_controls.clipName.c_str()) : 0.0; }))
        {
            loadModel(m_controls.characterPath);
        }
        if (m_dragAxis < 0 || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            m_camera.handleMouse();
    }

    const auto& weapon = m_controls.weapon;
    const auto& target = weapon.cameraTarget;
    const float cameraWeight = weapon.cameraWeight(m_timeline.time, m_timeline.duration);
    const auto view = m_camera.bind({ target[0] * cameraWeight, target[1] * cameraWeight,
        target[2] * cameraWeight });
    if (showUi)
    {
        if (m_controls.showWorldAxes)
            drawWorldAxes(view);
        editWeaponInViewport(view);
    }

    if (m_model)
    {
        sampleModel();
        m_model->setModelYawOffset(XM_PI + m_toonShader.settings().modelYaw);
        m_toonShader.bindSurface();
        m_model->Draw();
        m_toonShader.drawOutline(*m_model);
    }

    if (m_timeline.soloTrack < 0 || m_timeline.soloTrack == 3)
    {
        m_toonShader.bindSurface();
        m_weapon.draw(weapon, m_timeline.time, XMMatrixIdentity());
    }

    const float age = m_timeline.effectAge();
    if (age >= 0.0f && age < Explosion::frameCount * Explosion::frameDuration)
    {
        const auto position = weapon.enabled && weapon.effectOnWeapon && m_weapon.ready()
            ? AttackWeapon::effectPosition(weapon, m_timeline.effectStart, XMMatrixIdentity())
            : Vector3{ m_controls.effectX, m_controls.effectHeight, ActionSequence::effectForward };
        m_effect.SetPosition(position);
        Renderer::SetDepthEnable(false);
        m_effect.drawFrame(static_cast<int>(age / Explosion::frameDuration), view);
        Renderer::SetDepthEnable(true);
    }

    if (showUi)
        ToonPanel::draw();
}
