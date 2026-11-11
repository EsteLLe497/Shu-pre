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
    m_controls.assetStatus = std::string("Loaded: ") + path;
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
    const float weight = m_timeline.faceWeight();
    const auto& morphs = m_model->morphNames();
    const bool faceEnabled = m_timeline.trackEnabled(1) && !morphs.empty();
    const int morph = faceEnabled ? m_controls.morphIndex : -1;
    const int baseMorph = faceEnabled ? m_controls.baseMorphIndex : -1;
    const float extraTime = faceEnabled && !m_controls.extraMorphs.empty() ? m_timeline.time : -1.0f;
    if (m_sampledClip == m_controls.clipName && m_sampledMorph == morph &&
        m_sampledBaseMorph == baseMorph && m_sampledTime == motionTime && m_sampledWeight == weight
        && m_sampledExtraTime == extraTime && m_sampledExtraMorphs == m_controls.extraMorphs)
        return;

    static const std::string neutral;
    if (faceEnabled)
        m_model->setMorphBlend(baseMorph < 0 ? neutral : morphs[baseMorph], morphs[morph], weight,
            m_controls.extraMorphs, m_timeline.time);
    else
        m_model->setMorph(neutral, 0.0f);

    m_model->sampleClip(m_controls.clipName.c_str(), motionTime);
    m_sampledClip = m_controls.clipName;
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
            [this](const std::string& name) { return selectMotion(name); }))
        {
            loadModel(PlayerAnimationAssets::previewModel);
        }
        m_camera.handleMouse();
    }

    const auto view = m_camera.bind();
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
        m_effect.SetPosition({ m_controls.effectX, m_controls.effectHeight, ActionSequence::effectForward });
        Renderer::SetDepthEnable(false);
        m_effect.drawFrame(static_cast<int>(age / Explosion::frameDuration), view);
        Renderer::SetDepthEnable(true);
    }

    if (showUi)
        ToonPanel::draw();
}
