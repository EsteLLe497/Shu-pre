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
#include "HeroineAnimationAssets.h"
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
    m_controls.motionNames = HeroineAnimationAssets::listMotions();
    m_toonShader.init();

    // 本編と同じ外観設定を使い、プレビューのカメラは独立させる。
    ToonPreset::loadAppearance(m_toonShader.settings());

    m_effect.Init();
    m_effect.SetScale({ ActionSequence::effectScale, ActionSequence::effectScale, ActionSequence::effectScale });

    const bool exportExists = GetFileAttributesA(HeroineAnimationAssets::previewModel) != INVALID_FILE_ATTRIBUTES;
    if (exportExists && loadModel(HeroineAnimationAssets::previewModel))
        return;

    if (loadModel(HeroineAnimationAssets::model) && exportExists)
        m_controls.assetStatus = "Blender export failed to load. Showing the built-in player.";
}

void PreviewScene::Uninit()
{
    if (m_model)
    {
        m_model->Uninit();
        m_model.reset();
    }

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
    using HeroineAnimationAssets::LoadResult;
    const auto result = HeroineAnimationAssets::loadCharacter(*candidate, path);
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

    if (!HeroineAnimationAssets::loadMotion(*candidate, m_controls.clipName))
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
    m_controls.motionNames = HeroineAnimationAssets::listMotions();
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
    if (!m_model || !HeroineAnimationAssets::loadMotion(*m_model, name))
        return false;

    const auto canonical = HeroineAnimationAssets::canonicalMotionName(name);
    if (canonical != m_controls.clipName && !HeroineAnimationAssets::isBuiltInMotion(m_controls.clipName))
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
    const std::string clip = m_controls.clipName;
    const float weight = m_timeline.faceWeight();
    const auto& morphs = m_model->morphNames();
    const bool faceEnabled = m_timeline.trackEnabled(1) && !morphs.empty();
    const int morph = faceEnabled ? m_controls.morphIndex : -1;
    const int baseMorph = faceEnabled ? m_controls.baseMorphIndex : -1;
    const float extraTime = faceEnabled && !m_controls.extraMorphs.empty() ? m_timeline.time : -1.0f;
    if (m_sampledClip == clip
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
        AnimationModel::RootTravel::authored, false, true);
    m_sampledClip = clip;
    m_sampledMorph = morph;
    m_sampledBaseMorph = baseMorph;
    if (m_sampledExtraMorphs != m_controls.extraMorphs)
        m_sampledExtraMorphs = m_controls.extraMorphs;
    m_sampledTime = motionTime;
    m_sampledWeight = weight;
    m_sampledExtraTime = extraTime;
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
            [this]() { return m_model ? m_model->clipDuration(m_controls.clipName.c_str()) : 0.0; }))
        {
            loadModel(m_controls.characterPath);
        }
        m_camera.handleMouse();
    }

    const auto view = m_camera.bind({ 0, 0, 0 });
    if (showUi)
    {
        if (m_controls.showWorldAxes)
            drawWorldAxes(view);
    }

    if (m_model)
    {
        sampleModel();
        m_model->setModelYawOffset(XM_PI + m_toonShader.settings().modelYaw);
        m_toonShader.bindSurface();
        m_model->Draw();
        m_toonShader.drawOutline(*m_model);
    }


    const float age = m_timeline.effectAge();
    if (age >= 0.0f && age < Explosion::frameCount * Explosion::frameDuration)
    {
        const Vector3 position{ m_controls.effectX, m_controls.effectHeight, ActionSequence::effectForward };
        m_effect.SetPosition(position);
        Renderer::SetDepthEnable(false);
        m_effect.drawFrame(static_cast<int>(age / Explosion::frameDuration), view);
        Renderer::SetDepthEnable(true);
    }

    if (showUi)
        ToonPanel::draw();
}
