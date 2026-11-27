#include "main.h"
#include "renderer.h"
#include "Heroine.h"
#include "HeroineAnimationAssets.h"
#include "ActionFile.h"
#include "MeshField.h"
#include "manager.h"
#include <algorithm>

namespace
{
    const char* const actionNames[] = { "Idle", "Walk", "Run", "akubi" };
    const char* const defaultClips[] = { "Idle", "Walk", "Run", "akubi" };
}

void HeroineActionSet::load()
{
    for (size_t i = 0; i < static_cast<size_t>(HeroineActionKind::count); ++i)
    {
        auto& slot = get(static_cast<HeroineActionKind>(i));
        slot.loaded = ActionFile::loadNamed(actionNames[i], slot.sequence);
        if (!slot.loaded)
        {
            slot.sequence.clip = defaultClips[i];
            slot.sequence.effectEnabled = false;
        }
    }
}

void Heroine::Init()
{
    m_Layer = 1;
    m_Position = { 0, 0, 3 };
    m_toonShader.init();
    m_AnimationModel = AddGameComponent<AnimationModel>(this);
    using namespace HeroineAnimationAssets;
    auto result = loadCharacter(*m_AnimationModel, previewModel);
    if (result != LoadResult::loaded) result = loadCharacter(*m_AnimationModel, model);
    m_ready = result == LoadResult::loaded;
    if (!m_ready) return;
    m_actions.load();
    for (size_t i = 0; i < static_cast<size_t>(HeroineActionKind::count); ++i)
    {
        auto& slot = m_actions.get(static_cast<HeroineActionKind>(i));
        slot.motionReady = slot.sequence.motionEnabled && loadMotion(*m_AnimationModel, slot.sequence.clip);
        slot.faceReady = slot.sequence.faceEnabled && hasExpressionMorphs(slot.sequence, m_AnimationModel->morphNames());
    }
    playAction(HeroineActionKind::idle);
    samplePose(0, 0, true);
}

void Heroine::Uninit()
{
    m_toonShader.uninit();
    GameObject::Uninit();
    m_AnimationModel = nullptr;
    m_ready = false;
}

bool Heroine::playAction(HeroineActionKind kind)
{
    if (!m_ready || kind >= HeroineActionKind::count) return false;
    const auto& slot = m_actions.get(kind);
    if (!slot.motionReady && !slot.faceReady) return false;
    m_action = slot.sequence;
    m_action.motionEnabled = slot.motionReady;
    m_action.faceEnabled = slot.faceReady;
    m_playingKind = kind;
    m_singleAction = kind == HeroineActionKind::yawn;
    m_time = m_motionTime = 0;
    m_restart = true;
    return true;
}

bool Heroine::playAction(const ActionSequence& action)
{
    if (!m_ready || !ActionFile::validate(action)) return false;
    if (action.motionEnabled && !HeroineAnimationAssets::loadMotion(*m_AnimationModel, action.clip)) return false;
    if (action.faceEnabled && !hasExpressionMorphs(action, m_AnimationModel->morphNames())) return false;
    m_action = action;
    m_playingKind = HeroineActionKind::count;
    m_singleAction = true;
    m_time = m_motionTime = 0;
    m_restart = true;
    return true;
}

void Heroine::samplePose(float time, float dt, bool loop)
{
    if (!m_ready) return;
    if (m_action.faceEnabled)
    {
        const float weight = sampleExpression(time, m_action.faceStart, m_action.facePeak,
            m_action.faceEnd, m_action.faceStrength, m_action.smoothFaceBlend);
        m_AnimationModel->setMorphBlend(m_action.baseMorph, m_action.targetMorph, weight, m_action.extraMorphs, time);
    }
    else m_AnimationModel->setMorph("", 0);
    if (m_action.motionEnabled)
        m_AnimationModel->sampleBlendedClip(m_action.clip.c_str(), m_singleAction ? time : m_motionTime, dt,
            AnimationModel::RootTravel::removeHorizontalCycle, m_restart, loop);
    m_restart = false;
}

void Heroine::Update()
{
    if (!m_ready) return;
    if (const auto* terrain = Manager::GetGameObj<MeshField>())
    {
        float height;
        if (terrain->tryGetHeight(m_Position.x, m_Position.z, height)) m_Position.y = height;
    }
    const float dt = Manager::animationDeltaSeconds();
    const float duration = m_action.duration;
    const float next = m_time + dt * m_action.speed;
    m_time = m_singleAction ? std::min(next, duration) : fmodf(next, duration);
    const float clipDuration = m_playingKind < HeroineActionKind::count ? motionDuration(m_playingKind) : duration;
    const float previousMotion = m_restart ? -1.0f : m_motionTime;
    const float nextMotion = m_motionTime + dt * m_action.speed;
    m_motionTime = clipDuration > 0 ? (m_singleAction ? std::min(nextMotion, clipDuration) : fmodf(nextMotion, clipDuration)) : 0;
    if (m_playingKind < HeroineActionKind::count)
        SceneAudio::playMarkers(m_motionSounds[static_cast<size_t>(m_playingKind)], previousMotion, m_motionTime,
            !m_singleAction && nextMotion >= clipDuration ? clipDuration : 0);
    samplePose(m_time, dt, !m_singleAction);
    if (m_singleAction && next >= duration) playAction(HeroineActionKind::idle);
}

void Heroine::Draw()
{
    if (!m_ready) return;
    m_AnimationModel->setModelYawOffset(XM_PI + m_toonShader.settings().modelYaw);
    m_toonShader.bindSurface();
    GameObject::Draw();
    if (!usesAlphaBlend() || opacity() >= 1)
        m_toonShader.drawOutline(*m_AnimationModel);
}

void Heroine::setMotionSounds(HeroineActionKind kind, const SceneAudio::Timeline& markers)
{
    if (kind < HeroineActionKind::count) m_motionSounds[static_cast<size_t>(kind)] = markers;
}

float Heroine::motionDuration(HeroineActionKind kind) const
{
    if (!m_ready || kind >= HeroineActionKind::count) return 0;
    const auto& slot = m_actions.get(kind);
    return kind == HeroineActionKind::yawn ? slot.sequence.duration
        : static_cast<float>(m_AnimationModel->clipDuration(slot.sequence.clip.c_str()));
}

void Heroine::previewMotion(HeroineActionKind kind, float time)
{
    if (!playAction(kind)) return;
    m_time = m_motionTime = time;
    samplePose(time, 0, false);
}

void Heroine::clearMotionPreview()
{
    playAction(HeroineActionKind::idle);
    samplePose(0, 0, true);
}
