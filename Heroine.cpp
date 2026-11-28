#include "main.h"
#include "renderer.h"
#include "Heroine.h"
#include "HeroineAnimationAssets.h"
#include "ActionFile.h"
#include "MeshField.h"
#include "manager.h"
#include "SceneEditor.h"
#include "Player.h"
#include "Camera.h"
#include <fstream>
#include <locale>
#include <algorithm>

namespace
{
    const char* const actionNames[] = { "Idle", "Walk", "Run", "akubi" };
    const char* const defaultClips[] = { "Idle", "Walk", "Run", "akubi" };

    aiMatrix4x4 lookRotation(const aiVector3D& up, const aiVector3D& right, float yaw, float pitch)
    {
        aiMatrix4x4 yawRotation, pitchRotation;
        aiMatrix4x4::Rotation(yaw, up, yawRotation);
        const aiVector3D pitchAxis = aiMatrix3x3(yawRotation) * right;
        aiMatrix4x4::Rotation(-pitch, pitchAxis, pitchRotation);
        return pitchRotation * yawRotation;
    }
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
    m_actions.load();
    std::ifstream expressions("asset/ai/heroine-expressions.cfg");
    expressions.imbue(std::locale::classic());
    m_expression.read(expressions);
    std::ifstream lookSettings("asset/heroine-look-at.txt");
    lookSettings.imbue(std::locale::classic());
    m_lookAt.settings.read(lookSettings);
    if (!setModel(HeroineAnimationAssets::previewModel)) setModel(HeroineAnimationAssets::model);
}

void Heroine::Uninit()
{
    m_toonShader.uninit();
    GameObject::Uninit();
    m_AnimationModel = nullptr;
    m_ready = false;
}


bool Heroine::setModel(const std::string& path)
{
    // Prepare the replacement separately; failed imports never touch the live model.
    const auto releaseModel = [](AnimationModel* model) { model->Uninit(); delete model; };
    std::unique_ptr<AnimationModel, decltype(releaseModel)> candidate(new AnimationModel(this), releaseModel);
    candidate->Init();
    const auto result = HeroineAnimationAssets::loadCharacter(*candidate, path.c_str());
    if (result != HeroineAnimationAssets::LoadResult::loaded)
    {
        m_modelStatus = result == HeroineAnimationAssets::LoadResult::motionMismatch ? u8"切替失敗: 標準モーションと骨格が一致しません。"
            : result == HeroineAnimationAssets::LoadResult::missingTextures ? u8"切替失敗: テクスチャを読み込めません。"
            : u8"切替失敗: モデルを読み込めません。";
        return false;
    }
    auto actions = m_actions;
    for (size_t i = 0; i < static_cast<size_t>(HeroineActionKind::count); ++i)
    {
        auto& slot = actions.get(static_cast<HeroineActionKind>(i));
        slot.motionReady = slot.sequence.motionEnabled && HeroineAnimationAssets::loadMotion(*candidate, slot.sequence.clip, path);
        slot.faceReady = slot.sequence.faceEnabled && hasExpressionMorphs(slot.sequence, candidate->morphNames());
    }
    if (!actions.get(HeroineActionKind::idle).motionReady || !actions.get(HeroineActionKind::walk).motionReady)
    {
        m_modelStatus = u8"切替失敗: 待機・歩行アクションに必要なモーションがありません。";
        return false;
    }
    auto* previous = m_AnimationModel;
    const auto component = std::find(m_Components.begin(), m_Components.end(), previous);
    if (component == m_Components.end()) m_Components.push_back(candidate.get());
    else *component = candidate.get();
    m_AnimationModel = candidate.release();
    m_actions = std::move(actions);
    m_modelPath = path;
    m_ready = true;
    if (previous) releaseModel(previous);
    m_expression.bind(m_AnimationModel->morphNames());
    initLookAt();
    playAction(HeroineActionKind::idle);
    samplePose(0, 0, true);
    m_modelStatus = m_AnimationModel->morphNames().empty() ? u8"切替完了。表情モーフがないため、表情・閉眼は利用できません。" : u8"モデルを切り替えました。";
    return true;
}

bool Heroine::playAction(HeroineActionKind kind)
{
    if (!startAction(kind)) return false;
    m_movement.stop();
    return true;
}

bool Heroine::startAction(HeroineActionKind kind)
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
    if (action.motionEnabled && !HeroineAnimationAssets::loadMotion(*m_AnimationModel, action.clip, m_modelPath)) return false;
    if (action.faceEnabled && !hasExpressionMorphs(action, m_AnimationModel->morphNames())) return false;
    m_movement.stop();
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
    m_poseDeltaSeconds = dt;
    m_AnimationModel->setExpressionMorphs(m_expression.weights(), m_expression.blendWeight());
    m_actionEyeClosure = 0;
    if (m_action.faceEnabled)
    {
        const float weight = sampleExpression(time, m_action.faceStart, m_action.facePeak,
            m_action.faceEnd, m_action.faceStrength, m_action.smoothFaceBlend);
        // Keep animation blinks absolute, after the conversation expression blend.
        auto base = m_action.baseMorph, target = m_action.targetMorph;
        auto extra = m_action.extraMorphs;
        if (!m_closeEyeMorph.empty())
        {
            if (base == m_closeEyeMorph) { m_actionEyeClosure += 1.0f - weight; base.clear(); }
            if (target == m_closeEyeMorph) { m_actionEyeClosure += weight; target.clear(); }
            for (auto& morph : extra)
                if (morph.name == m_closeEyeMorph)
                {
                    m_actionEyeClosure += sampleExpression(time, morph.start, morph.peak, morph.end, morph.weight, morph.smooth);
                    morph.name.clear();
                }
        }
        m_actionEyeClosure = std::min(1.0f, m_actionEyeClosure);
        m_AnimationModel->setMorphBlend(base, target, weight, extra, time);
    }
    else m_AnimationModel->setMorph("", 0);
    if (m_action.motionEnabled)
        m_AnimationModel->sampleBlendedClip(m_action.clip.c_str(), m_singleAction ? time : m_motionTime, dt,
            AnimationModel::RootTravel::removeHorizontalCycle, m_restart, loop);
    else m_AnimationModel->refreshPose();
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
    m_expression.update(dt);
    updateMovement(dt);
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

bool Heroine::movementReady() const
{
    return m_ready && m_actions.get(HeroineActionKind::idle).motionReady
        && m_actions.get(HeroineActionKind::walk).motionReady;
}

bool Heroine::resolveMovement(Vector3& position) const
{
    if (const auto* terrain = Manager::GetGameObj<MeshField>())
    {
        float height;
        if (!terrain->tryGetHeight(position.x, position.z, height)) return false;
        position.y = height;
    }
    constexpr float radius = 0.35f;
    return !SceneEditor::blocksEnemy(position, radius, this)
        && !SceneEditor::blocksEnemy(position + Vector3{ 0, 0.85f, 0 }, radius, this);
}

bool Heroine::resetMovement(const Vector3& position)
{
    if (!m_ready || !HeroineMovement::validPosition(position)) return false;
    Vector3 candidate = position;
    if (!resolveMovement(candidate)) return false;
    stopMovement();
    m_Position = candidate;
    return true;
}

bool Heroine::moveTo(const Vector3& target, float speed, float arrivalRadius, bool repeat)
{
    if (!movementReady()) return false;
    Vector3 groundedTarget = target;
    if (!HeroineMovement::validPosition(target) || !resolveMovement(groundedTarget)) return false;
    if (!m_movement.begin(m_Position, groundedTarget, speed, arrivalRadius, repeat)) return false;
    startAction(m_movement.state() == HeroineMovement::State::moving ? HeroineActionKind::walk : HeroineActionKind::idle);
    return true;
}

void Heroine::stopMovement()
{
    m_movement.stop();
    startAction(HeroineActionKind::idle);
}

void Heroine::pauseMovement(bool paused)
{
    const auto previous = m_movement.state();
    m_movement.setPaused(paused);
    if (previous != m_movement.state())
        startAction(paused ? HeroineActionKind::idle : HeroineActionKind::walk);
}

void Heroine::updateMovement(float dt)
{
    const Vector3 previous = m_Position;
    m_movement.update(m_Position, dt, [this](Vector3& position)
    {
        if (!resolveMovement(position)) return false;
        return position.y - m_Position.y <= 0.4f && m_Position.y - position.y <= 0.6f;
    });
    const Vector3 moved = m_Position - previous;
    if (hypotf(moved.x, moved.z) > 0.0001f)
    {
        const float yaw = atan2f(moved.x, moved.z);
        m_Rotation.y += remainderf(yaw - m_Rotation.y, XM_2PI) * (1.0f - expf(-12.0f * dt));
    }
    const auto state = m_movement.state();
    if (state == HeroineMovement::State::moving && m_playingKind != HeroineActionKind::walk)
        startAction(HeroineActionKind::walk);
    else if ((state == HeroineMovement::State::arrived || state == HeroineMovement::State::blocked)
        && m_playingKind == HeroineActionKind::walk)
        startAction(HeroineActionKind::idle);
}

void Heroine::initLookAt()
{
    m_lookBones.fill({});
    m_closeEyeMorph.clear();
    m_actionEyeClosure = 0;
    m_lookAt.reset();
    const std::initializer_list<const char*> candidates[] = {
        { "J_Bip_C_Neck", "Neck", "mixamorig:Neck" },
        { "J_Bip_C_Head", "Head", "mixamorig:Head" },
        { "J_Adj_L_FaceEye", "J_Bip_L_Eye", "LeftEye", "mixamorig:LeftEye" },
        { "J_Adj_R_FaceEye", "J_Bip_R_Eye", "RightEye", "mixamorig:RightEye" }
    };
    for (size_t i = 0; i < m_lookBones.size(); ++i)
        for (const auto* name : candidates[i])
        {
            aiMatrix4x4 transform;
            if (m_AnimationModel->boneModelTransform(name, transform)) { m_lookBones[i] = name; break; }
        }
    for (const char* name : { "Fcl_EYE_Close", "Blink", "EyesClosed" })
        if (std::find(m_AnimationModel->morphNames().begin(), m_AnimationModel->morphNames().end(), name)
            != m_AnimationModel->morphNames().end()) { m_closeEyeMorph = name; break; }
    m_AnimationModel->setPoseModifier([this](AnimationModel& model) { updateLookAt(model); });
}

bool Heroine::setLookTarget(const Vector3& target)
{
    if (!HeroineMovement::validPosition(target)) return false;
    m_fixedLookTarget = true;
    m_lookTarget = target;
    return true;
}

void Heroine::updateLookAt(AnimationModel& model)
{
    model.setOverlayMorph(m_closeEyeMorph, m_actionEyeClosure);
    if (m_lookBones[1].empty() || SceneEditor::isOpen() || m_poseDeltaSeconds <= 0)
    {
        m_lookAt.reset();
        return;
    }
    aiMatrix4x4 head;
    if (!model.boneModelTransform(m_lookBones[1].c_str(), head)) return;
    aiVector3D origin(head.a4, head.b4, head.c4);
    if (eyesReady())
    {
        aiMatrix4x4 left, right;
        model.boneModelTransform(m_lookBones[2].c_str(), left);
        model.boneModelTransform(m_lookBones[3].c_str(), right);
        origin = aiVector3D(left.a4 + right.a4, left.b4 + right.b4, left.c4 + right.c4) * 0.5f;
    }
    const auto world = XMMatrixRotationY(XM_PI + m_toonShader.settings().modelYaw) * GetMatrx();
    XMFLOAT3 worldOrigin;
    XMStoreFloat3(&worldOrigin, XMVector3TransformCoord(XMVectorSet(origin.x, origin.y, origin.z, 1), world));
    m_lookOrigin = { worldOrigin.x, worldOrigin.y, worldOrigin.z };
    bool available = m_fixedLookTarget;
    if (!m_fixedLookTarget)
    {
        if (const auto* player = Manager::GetGameObj<Player>())
        {
            auto* camera = Manager::GetGameObj<Camera>();
            const float height = camera ? camera->orbitSettings().lookHeight : 1.6f;
            m_lookTarget = player->GetPosition() + Vector3{ 0, height, 0 };
            available = true;
        }
    }
    XMFLOAT3 localTarget;
    XMStoreFloat3(&localTarget, XMVector3TransformCoord(
        XMVectorSet(m_lookTarget.x, m_lookTarget.y, m_lookTarget.z, 1), XMMatrixInverse(nullptr, world)));
    aiVector3D direction = aiVector3D(localTarget.x, localTarget.y, localTarget.z) - origin;
    aiVector3D forward(-head.a3, -head.b3, -head.c3), up(head.a2, head.b2, head.c2);
    forward.Normalize(); up.Normalize();
    aiVector3D right = up ^ forward;
    right.Normalize(); up = forward ^ right; up.Normalize();
    const float x = direction * right, y = direction * up, z = direction * forward;
    const float distance = (m_lookTarget - m_lookOrigin).lenght();
    // Player proximity uses horizontal body distance so differing eye heights do
    // not prevent closure at contact. Fixed debug targets use true 3D distance.
    const float proximity = m_fixedLookTarget ? distance : hypotf(m_lookTarget.x - m_Position.x, m_lookTarget.z - m_Position.z);
    m_lookAt.update(atan2f(x, z), atan2f(y, hypotf(x, z)), distance, available, m_poseDeltaSeconds, proximity);
    if (eyelidsReady()) model.setOverlayMorph(m_closeEyeMorph, std::max(m_actionEyeClosure, m_lookAt.eyeClosure()));

    const float neck = m_lookBones[0].empty() ? 0 : m_lookAt.settings.neckWeight;
    if (neck > 0) model.rotateBoneModelSpace(m_lookBones[0].c_str(),
        lookRotation(up, right, m_lookAt.headYaw() * neck, m_lookAt.headPitch() * neck));
    model.rotateBoneModelSpace(m_lookBones[1].c_str(),
        lookRotation(up, right, m_lookAt.headYaw() * (1 - neck), m_lookAt.headPitch() * (1 - neck)));
    // Recompute the face axes after the neck/head correction. Both eye bones use this
    // model-space frame, not their differently oriented local FBX axes.
    model.boneModelTransform(m_lookBones[1].c_str(), head);
    forward = aiVector3D(-head.a3, -head.b3, -head.c3).Normalize();
    up = aiVector3D(head.a2, head.b2, head.c2).Normalize();
    right = (up ^ forward).Normalize(); up = (forward ^ right).Normalize();
    const auto eyeRotation = lookRotation(up, right, m_lookAt.eyeYaw(), m_lookAt.eyePitch());
    if (eyesReady())
    {
        model.rotateBoneModelSpace(m_lookBones[2].c_str(), eyeRotation);
        model.rotateBoneModelSpace(m_lookBones[3].c_str(), eyeRotation);
    }
    const auto gaze = aiMatrix3x3(eyeRotation) * forward;
    XMFLOAT3 worldDirection;
    XMStoreFloat3(&worldDirection, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(gaze.x, gaze.y, gaze.z, 0), world)));
    m_lookDirection = { worldDirection.x, worldDirection.y, worldDirection.z };
}
