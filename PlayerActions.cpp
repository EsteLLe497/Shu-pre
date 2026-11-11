// =========================================================
// ファイルの情報[PlayerActions.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "Player.h"
#include "PlayerAnimationAssets.h"
#include "ActionFile.h"
#include "Explosion.h"
#include "manager.h"
#include <memory>

namespace
{
    using Kind = PlayerActionKind;

    struct ActionDefinition
    {
        Kind kind;
        const char* path;
        const char* fallbackClip;
        float motionStart = 0.0f;
        float motionEnd = 0.0f;
    };

    // 状態とアセットの対応はここで管理する。
    constexpr ActionDefinition definitions[] = {
        { Kind::idle, "asset/actions/Idle.json", "Idle" },
        { Kind::walk, "asset/actions/Walk.json", "Walk" },
        { Kind::run, "asset/actions/Run.json", "Run" },
        { Kind::yawn, "asset/actions/akubi.json", nullptr },
        { Kind::attack, ActionFile::attackPath, nullptr },
        // FBXで踏み切り・空中、接地・踏ん張りに相当する区間。
        { Kind::jump, "asset/actions/Jump.json", nullptr, 0.59f, 1.17f },
        { Kind::runJump, "asset/actions/RunJump.json", nullptr, 0.04f, 0.64f },
        { Kind::land, "asset/actions/Land.json", nullptr, 0.36f }
    };
    static_assert(_countof(definitions) == static_cast<std::size_t>(Kind::count),
        "Player action definitions and states must match.");

    const ActionDefinition& definition(Kind kind)
    {
        return definitions[static_cast<std::size_t>(kind)];
    }

    const char* locomotionClip(const PlayerActionSet::Slot& slot, Kind kind)
    {
        return slot.motionReady ? slot.sequence.clip.c_str() : definition(kind).fallbackClip;
    }
}

// =========================================================
// 本編で使うアクションをまとめて読み込む
// =========================================================
void PlayerActionSet::load()
{
    for (const auto& entry : definitions)
    {
        auto& slot = get(entry.kind);
        slot.loaded = ActionFile::load(entry.path, slot.sequence);
    }
}

void Player::loadPlayerActions()
{
    using namespace PlayerAnimationAssets;

    m_actions.load();
    const bool needsMorphs = std::any_of(std::begin(definitions), std::end(definitions), [&](const auto& entry) {
        const auto& slot = m_actions.get(entry.kind);
        return slot.loaded && slot.sequence.faceEnabled;
    });

    auto result = LoadResult::modelFailed;
    if (needsMorphs)
        result = loadCharacter(*m_AnimationModel, previewModel);
    if (result != LoadResult::loaded)
        result = loadCharacter(*m_AnimationModel, model);

    assert(result == LoadResult::loaded);
    if (result == LoadResult::loaded)
        bindPlayerActions(*m_AnimationModel);
}

// =========================================================
// 見つからない素材だけを無効にし、移動の標準モーションを残す
// =========================================================
void Player::bindPlayerActions(AnimationModel& model)
{
    for (const auto& entry : definitions)
    {
        auto& slot = m_actions.get(entry.kind);
        if (!slot.loaded)
            continue;

        const auto& action = slot.sequence;
        slot.motionReady = action.motionEnabled && PlayerAnimationAssets::loadMotion(model, action.clip);
        slot.faceReady = action.faceEnabled && hasExpressionMorphs(action, model.morphNames());
    }
}

// 読み込めたレイヤーで再生し、元の設定は保持する。
bool Player::playAction(PlayerActionKind kind)
{
    if (kind < Kind::yawn || kind >= Kind::count)
        return false;

    const auto& slot = m_actions.get(kind);
    if (!slot.loaded ||
        (!slot.motionReady && !slot.faceReady && !slot.sequence.effectEnabled))
        return false;

    auto action = slot.sequence;
    action.motionEnabled = slot.motionReady;
    action.faceEnabled = slot.faceReady;
    if (!playAction(action))
        return false;

    m_playingActionKind = kind;
    if (definition(kind).motionStart > 0.0f)
    {
        m_actionPlayback.stop();
        m_actionPlayback.start(definition(kind).motionStart);
    }
    return true;
}

// =========================================================
// 入力に応じた移動状態へ切り替え、表情と体を共通の時刻で再生する
// =========================================================
void Player::updateLocomotion(float dt, PlayerActionKind kind)
{
    const auto& action = m_actions.get(kind).sequence;
    if (m_locomotionKind != kind)
    {
        m_locomotionKind = kind;
        m_locomotionMotionTime = 0.0;
        m_locomotionTime = 0.0f;
        m_locomotionEffectFired = false;
        return;
    }

    // 体はFBX本来の周期で回し、表情とエフェクトにはJSONの周期を使う。
    m_locomotionMotionTime += dt;
    if (m_locomotionTime >= action.duration)
    {
        m_locomotionTime = 0.0f;
        m_locomotionEffectFired = false;
    }
    else
    {
        m_locomotionTime = fminf(action.duration, m_locomotionTime + dt);
    }
}

// =========================================================
// 任意のアクションを先読みし、イベントから再生する
// =========================================================
bool Player::loadAction(const char* path, ActionSequence& action)
{
    ActionSequence candidate;
    if (m_actionPlayback.active() || !ActionFile::load(path, candidate) || !prepareAction(candidate))
        return false;

    action = std::move(candidate);
    return true;
}

bool Player::playAction(const ActionSequence& action)
{
    if (!ActionFile::validate(action) || !prepareAction(action))
        return false;

    m_playingAction = action;
    m_playingActionKind = Kind::count;
    m_actionMotionRestart = action.motionEnabled;
    m_playingAction.clip = PlayerAnimationAssets::canonicalMotionName(action.clip);
    // 呼び出し側が選んだ時点で、前のアクションを切り替えて先頭から再生する。
    m_yawning = false;
    m_actionPlayback.stop();
    m_actionPlayback.start();
    return true;
}

// =========================================================
// 必要な素材を検証し、成功したときだけ表情付きモデルへ切り替える
// =========================================================
bool Player::prepareAction(const ActionSequence& action)
{
    if (!m_AnimationModel)
        return false;

    using namespace PlayerAnimationAssets;

    if (!action.faceEnabled || hasExpressionMorphs(action, m_AnimationModel->morphNames()))
        return !action.motionEnabled || loadMotion(*m_AnimationModel, action.clip);

    const auto component = std::find(m_Components.begin(), m_Components.end(), m_AnimationModel);
    if (component == m_Components.end())
        return false;

    auto candidate = std::make_unique<AnimationModel>(this);
    if (loadCharacter(*candidate, previewModel) != LoadResult::loaded)
        return false;

    if (!hasExpressionMorphs(action, candidate->morphNames())
        || (action.motionEnabled && !loadMotion(*candidate, action.clip)))
    {
        candidate->Uninit();
        return false;
    }

    bindPlayerActions(*candidate);
    m_AnimationModel->Uninit();
    delete m_AnimationModel;
    m_AnimationModel = candidate.release();
    *component = m_AnimationModel;
    return true;
}

// =========================================================
// 単発アクションを優先し、終わったら移動状態の姿勢へ戻す
// =========================================================
void Player::updateActionPose(float dt, float physicsDt)
{
    const auto& slot = m_actions.get(m_locomotionKind);
    const auto& gait = slot.sequence;
    const auto& action = m_playingAction;
    const bool active = m_actionPlayback.active();
    const bool jumping = m_playingActionKind == Kind::jump || m_playingActionKind == Kind::runJump;
    const bool landing = m_playingActionKind == Kind::land;
    const bool useActionFace = active && action.faceEnabled;
    const auto& expression = useActionFace ? action : gait;
    const float faceTime = useActionFace ? m_actionPlayback.time() : m_locomotionTime;
    if (useActionFace || slot.faceReady)
    {
        const float weight = sampleExpression(faceTime, expression.faceStart, expression.facePeak,
            expression.faceEnd, expression.faceStrength, expression.smoothFaceBlend);
        m_AnimationModel->setMorphBlend(expression.baseMorph, expression.targetMorph, weight,
            expression.extraMorphs, faceTime);
        m_faceApplied = true;
    }
    else if (m_faceApplied)
    {
        m_AnimationModel->setMorph("", 0.0f);
        m_faceApplied = false;
    }

    const bool useActionMotion = active && action.motionEnabled;
    const bool actionChanged = useActionMotion != m_actionMotionApplied;
    if (useActionMotion)
    {
        using Travel = AnimationModel::RootTravel;
        const auto rootTravel = jumping ? Travel::lockTranslation : (landing ? Travel::anchorLandingFeet : Travel::authored);
        m_AnimationModel->sampleBlendedClip(action.clip.c_str(), m_actionPlayback.time(), dt,
            rootTravel, actionChanged || m_actionMotionRestart, !jumping && !landing);
    }
    else
    {
        // 歩き・走りの移動は入力側が担当するため、描画では周回ごとの前進量を差し引く。
        const bool isGait = m_locomotionKind == Kind::walk || m_locomotionKind == Kind::run;
        const auto rootTravel = isGait ? AnimationModel::RootTravel::removeHorizontalCycle
            : AnimationModel::RootTravel::authored;
        m_AnimationModel->sampleBlendedClip(locomotionClip(slot, m_locomotionKind), m_locomotionMotionTime,
            dt, rootTravel, actionChanged);
    }
    m_actionMotionApplied = useActionMotion;
    m_actionMotionRestart = false;

    if (m_actionPlayback.takeEffect(action))
        spawnActionEffect(action);

    if (slot.loaded && gait.effectEnabled && !m_locomotionEffectFired && m_locomotionTime >= gait.effectStart)
    {
        m_locomotionEffectFired = true;
        if (!active)
            spawnActionEffect(gait);
    }

    if (jumping && !m_Ground)
    {
        // 空中姿勢をジャンプの物理時間に合わせ、長い落下では最後の姿勢を保つ。
        const auto& window = definition(m_playingActionKind);
        constexpr float flightTime = 2.0f * jumpSpeed / gravity;
        m_actionPlayback.advanceTo(window.motionEnd,
            physicsDt * (window.motionEnd - window.motionStart) / flightTime, true);
    }
    else
    {
        m_actionPlayback.advance(action, dt);
    }
}

void Player::spawnActionEffect(const ActionSequence& action)
{
    const Vector3 offset = GetRight() * action.effectX
        + GetFoward() * ActionSequence::effectForward + Vector3(0.0f, action.effectHeight, 0.0f);
    auto* effect = Manager::AddGameObj<Explosion>();
    effect->SetPosition(m_Position + offset);
    effect->SetScale({ ActionSequence::effectScale, ActionSequence::effectScale, ActionSequence::effectScale });
}
