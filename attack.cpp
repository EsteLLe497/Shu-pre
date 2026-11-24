#include "main.h"
#include "renderer.h"
#include "attack.h"
#include "GameObject.h"
#include "manager.h"
#include "Enemy.h"
#include "Player.h"
#include "Camera.h"
#include "MeshField.h"
#include "ToonPanel.h"
#include "imgui/imgui.h"
#include <cmath>

void Attack::Init()
{
    m_parameters = DamageParameters{};
    m_hitbox = HitboxParameters{};
    m_targeting = TargetParameters{};
    m_feedback = HitFeedbackParameters{};
    begin();
    finish();
}

void Attack::Uninit()
{
    m_hitEnemies.clear();
    ToonPanel::closeAttackDebug();
}

void Attack::Update()
{
    // 命中時の表示だけを更新。攻撃時間はsamplePlaybackで受け取る。
    m_hitFlash = std::max(0.0f, m_hitFlash - Manager::animationDeltaSeconds());
}

float Attack::damage() const
{
    const auto& p = m_parameters;
    if (!std::isfinite(p.weaponDamage) || !std::isfinite(p.damageMultiplier) || !std::isfinite(p.bonusDamage))
        return 0.0f;
    const float result = std::max(0.0f, p.weaponDamage) * std::max(0.0f, p.damageMultiplier) + p.bonusDamage;
    return std::isfinite(result) ? std::max(0.0f, result) : 0.0f;
}

void Attack::begin()
{
    m_hitEnemies.clear();
    m_active = false;
    m_hitFlash = 0.0f;
}

void Attack::finish()
{
    m_active = false;
}

Vector3 Attack::hitboxCenter() const
{
    Vector3 forward = m_GameObject->GetFoward();
    forward.y = 0.0f;
    forward.normalize();
    return m_GameObject->GetPosition() + forward * m_hitbox.forwardOffset
        + Vector3{ 0.0f, m_hitbox.height, 0.0f };
}

void Attack::samplePlayback(float previousTime, float currentTime)
{
    // 低FPSで開始・終了を一度にまたいでも、重なった時間を見落とさない。
    m_active = std::isfinite(m_hitbox.startTime) && std::isfinite(m_hitbox.endTime)
        && m_hitbox.startTime >= 0.0f && m_hitbox.endTime > m_hitbox.startTime
        && previousTime < m_hitbox.endTime && currentTime >= m_hitbox.startTime;
    if (m_active) checkHits();
}

void Attack::checkHits()
{
    if (!std::isfinite(m_hitbox.radius) || m_hitbox.radius <= 0.0f || damage() <= 0.0f) return;
    const auto center = hitboxCenter();
    const BoundingSphere sphere({ center.x, center.y, center.z }, m_hitbox.radius);
    bool hitAny = false;
    for (auto* enemy : Manager::GetGameObjs<Enemy>())
    {
        if (enemy->isDead() || std::find(m_hitEnemies.begin(), m_hitEnemies.end(), enemy) != m_hitEnemies.end()) continue;
        // 読込モデルは骨ごとのカプセル、試作モデルは既存の身体判定を使う。
        size_t part = 0;
        const bool hit = enemy->hasCharacter()
            ? enemy->queryHurtbox(center, m_hitbox.radius, part) : enemy->IntersectsMesh(sphere);
        if (!hit) continue;
        m_hitEnemies.push_back(enemy);
        enemy->AddDamage(damage(), m_feedback.flashSeconds);
        m_hitFlash = 0.15f;
        hitAny = true;
    }
    // 同時に何体へ当たっても、カメラと停止はこの判定で一度だけ発生させる。
    if (hitAny)
    {
        Manager::requestHitStop(m_feedback.hitStopSeconds);
        if (std::isfinite(m_feedback.shakeStrength) && m_feedback.shakeStrength > 0.0f)
            if (auto* camera = Manager::GetGameObj<Camera>())
                camera->Shake({ m_feedback.shakeStrength, m_feedback.shakeStrength * 0.5f, 0.0f });
    }
}

void Attack::Draw()
{
    if (!ImGui::GetCurrentContext()) return;
    drawTargetCircles();
    drawHitbox();
    if (ToonPanel::isAttackDebugOpen()) drawDebugPanel();
}

// 判定の中心間距離で選ぶ。毎回生存中の敵を探すので削除済みの参照を残さない。
Enemy* Attack::findTarget() const
{
    if (!m_targeting.autoAim || !std::isfinite(m_targeting.range) || m_targeting.range <= 0.0f) return nullptr;
    Enemy* nearest = nullptr;
    float nearestDistance = m_targeting.range * m_targeting.range;
    const auto position = m_GameObject->GetPosition();
    for (auto* enemy : Manager::GetGameObjs<Enemy>())
    {
        if (!enemy->HasMeshCollider()) continue;
        const auto offset = enemy->GetPosition() - position;
        if (fabsf(offset.y) > m_targeting.maxHeightDifference) continue;
        const float distance = offset.x * offset.x + offset.z * offset.z;
        if (distance > nearestDistance) continue;
        nearest = enemy;
        nearestDistance = distance;
    }
    return nearest;
}

void Attack::faceTarget()
{
    auto* enemy = findTarget();
    if (!enemy) return;
    const auto direction = enemy->GetPosition() - m_GameObject->GetPosition();
    if (direction.x * direction.x + direction.z * direction.z < 0.000001f) return;
    auto rotation = m_GameObject->GetRotation();
    rotation.y = atan2f(direction.x, direction.z);
    m_GameObject->SetRotation(rotation);
}

void Attack::drawTargetCircles()
{
    if (!m_targeting.showCircle || !std::isfinite(m_targeting.range) || m_targeting.range <= 0.0f) return;
    auto* camera = Manager::GetGameObj<Camera>();
    if (!camera) return;
    const auto* terrain = Manager::GetGameObj<MeshField>();
    const auto vp = camera->GetViewMatrix() * camera->projectionMatrix();
    const auto size = ImGui::GetIO().DisplaySize;
    auto* overlay = ImGui::GetBackgroundDrawList();
    const auto* target = findTarget();
    for (auto* enemy : Manager::GetGameObjs<Enemy>())
    {
        if (!enemy->HasMeshCollider()) continue;
        const auto center = enemy->GetPosition();
        const bool selected = enemy == target;
        const ImU32 color = selected ? IM_COL32(255, 215, 65, 240) : IM_COL32(95, 195, 230, 110);
        ImVec2 previous;
        bool visible = false;
        for (int i = 0; i <= 96; ++i)
        {
            const float angle = XM_2PI * i / 96;
            auto point = center + Vector3{ cosf(angle) * m_targeting.range, 0.0f, sinf(angle) * m_targeting.range };
            // 凹凸のある地面にもサークルを沿わせる。地形の外には描かない。
            const bool onGround = !terrain || terrain->tryGetHeight(point.x, point.z, point.y);
            point.y += 0.04f;
            const auto clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1), vp);
            const float w = XMVectorGetW(clip);
            const bool projected = onGround && w > 0.01f && XMVectorGetZ(clip) >= 0.0f && XMVectorGetZ(clip) <= w;
            if (projected)
            {
                const ImVec2 pixel((XMVectorGetX(clip) / w + 1.0f) * size.x * 0.5f,
                    (1.0f - XMVectorGetY(clip) / w) * size.y * 0.5f);
                if (visible) overlay->AddLine(previous, pixel, color, selected ? 2.5f : 1.5f);
                previous = pixel;
            }
            visible = projected;
        }
    }
}

void Attack::drawHitbox()
{
    if (!m_showHitbox || (!m_active && !m_showInactive && m_hitFlash <= 0.0f)) return;
    auto* camera = Manager::GetGameObj<Camera>();
    if (!camera) return;
    const auto center = hitboxCenter();
    const auto vp = camera->GetViewMatrix() * camera->projectionMatrix();
    const auto size = ImGui::GetIO().DisplaySize;
    auto* overlay = ImGui::GetBackgroundDrawList();
    const ImU32 color = m_hitFlash > 0.0f ? IM_COL32(255, 80, 65, 255)
        : m_active ? IM_COL32(255, 220, 65, 255) : IM_COL32(150, 170, 190, 200);
    const auto project = [&](const Vector3& point, ImVec2& pixel)
    {
        const auto clip = XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 0.01f || XMVectorGetZ(clip) < 0.0f || XMVectorGetZ(clip) > w) return false;
        pixel = { (XMVectorGetX(clip) / w + 1.0f) * size.x * 0.5f,
            (1.0f - XMVectorGetY(clip) / w) * size.y * 0.5f };
        return true;
    };
    // 実際の判定と同じ中心・半径で、3方向の円を描く。
    for (int axis = 0; axis < 3; ++axis)
    {
        ImVec2 previous;
        bool visible = false;
        for (int i = 0; i <= 48; ++i)
        {
            const float angle = XM_2PI * i / 48;
            const float a = cosf(angle) * m_hitbox.radius, b = sinf(angle) * m_hitbox.radius;
            const Vector3 offset = axis == 0 ? Vector3{ 0, a, b }
                : axis == 1 ? Vector3{ a, 0, b } : Vector3{ a, b, 0 };
            ImVec2 pixel;
            const bool projected = project(center + offset, pixel);
            if (visible && projected) overlay->AddLine(previous, pixel, color, 2.0f);
            if (projected) previous = pixel;
            visible = projected;
        }
    }
}

void Attack::drawDebugPanel()
{
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Attack / Damage (F4)"))
    {
        constexpr auto flags = ImGuiSliderFlags_AlwaysClamp;
        ImGui::PushItemWidth(150.0f);
        ImGui::TextUnformatted("Short Sword / Normal attack");
        ImGui::DragFloat("Weapon damage", &m_parameters.weaponDamage, 1.0f, 0.0f, 1000000.0f, "%.1f", flags);
        ImGui::DragFloat("Damage multiplier", &m_parameters.damageMultiplier, 0.05f, 0.0f, 100.0f, "%.2f", flags);
        ImGui::DragFloat("Bonus damage", &m_parameters.bonusDamage, 1.0f, -1000000.0f, 1000000.0f, "%.1f", flags);
        ImGui::Text("Final damage: %.1f", damage());
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Hit feedback"))
        {
            ImGui::DragFloat("Shake strength", &m_feedback.shakeStrength, 0.005f, 0.0f, 0.5f, "%.3f", flags);
            ImGui::DragFloat("Hit stop (sec)", &m_feedback.hitStopSeconds, 0.01f, 0.0f, 1.0f, "%.2f", flags);
            ImGui::DragFloat("White flash (sec)", &m_feedback.flashSeconds, 0.01f, 0.0f, 2.0f, "%.2f", flags);
        }
        ImGui::Separator();
        ImGui::Checkbox("Auto aim at attack start", &m_targeting.autoAim);
        ImGui::Checkbox("Show target circles", &m_targeting.showCircle);
        ImGui::DragFloat("Target range", &m_targeting.range, 0.05f, 0.1f, 20.0f, "%.2f", flags);
        ImGui::DragFloat("Target height limit", &m_targeting.maxHeightDifference, 0.05f, 0.0f, 10.0f, "%.2f", flags);
        ImGui::TextDisabled("Gold circle: nearest enemy in range");
        ImGui::TextDisabled("Target range and damage hitbox are separate.");
        ImGui::Separator();
        ImGui::Checkbox("Show hitbox", &m_showHitbox);
        ImGui::Checkbox("Show while inactive", &m_showInactive);
        ImGui::TextDisabled("Gray: inactive / Yellow: active / Red: hit");
        ImGui::DragFloat("Forward offset", &m_hitbox.forwardOffset, 0.05f, 0.0f, 10.0f, "%.2f", flags);
        ImGui::DragFloat("Height", &m_hitbox.height, 0.05f, 0.0f, 10.0f, "%.2f", flags);
        ImGui::DragFloat("Radius", &m_hitbox.radius, 0.05f, 0.05f, 10.0f, "%.2f", flags);
        ImGui::DragFloat("Hit start (sec)", &m_hitbox.startTime, 0.01f, 0.0f, 10.0f, "%.2f", flags);
        ImGui::DragFloat("Hit end (sec)", &m_hitbox.endTime, 0.01f, m_hitbox.startTime + 0.01f, 20.0f, "%.2f", flags);
        ImGui::PopItemWidth();
        ImGui::Separator();
        ImGui::Checkbox("Freeze enemies", &m_freezeEnemies);
        if (ImGui::Button("Test attack"))
            if (auto* player = dynamic_cast<Player*>(m_GameObject)) player->playAction(PlayerActionKind::attack);
        ImGui::SameLine();
        if (ImGui::Button("Reset enemy HP"))
            for (auto* enemy : Manager::GetGameObjs<Enemy>()) enemy->resetHealth();
        if (ImGui::Button("Reset attack settings"))
        {
            m_parameters = DamageParameters{};
            m_hitbox = HitboxParameters{};
            m_targeting = TargetParameters{};
            m_feedback = HitFeedbackParameters{};
        }
        ImGui::TextDisabled("Session only. F4 closes this panel.");
    }
    ImGui::End();
}
