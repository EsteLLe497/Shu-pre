#include "main.h"
#include "renderer.h"
#include "DebugPanel.h"
#include "Heroine.h"
#include "HeroineExpressionPanel.h"
#include "HeroineConversation.h"
#include "AI/DialoguePanel.h"
#include "Player.h"
#include "Camera.h"
#include "MeshField.h"
#include "manager.h"
#include "imgui/imgui.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace
{
    constexpr char settingsPath[] = "asset/heroine-move-test.txt";
    bool open = false;
    bool initialized = false;
    bool showMarkers = true;
    bool showLookMarkers = true;
    const char* lookStatus = nullptr;
    const char* status = nullptr;

    struct MoveTest
    {
        Vector3 start{ 0, 0, 3 };
        Vector3 target{ 0, 0, 7 };
        float speed = 1.2f;
        float arrivalRadius = 0.05f;
        bool repeat = false;
    } test;

    bool valid(const MoveTest& value)
    {
        HeroineMovement movement;
        return movement.begin(value.start, value.target, value.speed, value.arrivalRadius, value.repeat);
    }

    bool load()
    {
        std::ifstream file(settingsPath);
        file.imbue(std::locale::classic());
        std::string format;
        int version = 0, repeat = 0;
        MoveTest candidate;
        if (!(file >> format >> version) || format != "heroineMoveTest" || version != 1
            || !(file >> candidate.start.x >> candidate.start.y >> candidate.start.z
                >> candidate.target.x >> candidate.target.y >> candidate.target.z
                >> candidate.speed >> candidate.arrivalRadius >> repeat)
            || (repeat != 0 && repeat != 1)) return false;
        candidate.repeat = repeat != 0;
        if (!valid(candidate) || !(file >> std::ws).eof()) return false;
        test = candidate;
        return true;
    }

    bool saveText(const char* path, const std::function<void(std::ostream&)>& write)
    {
        char temporary[MAX_PATH]{};
        if (!GetTempFileNameA("asset", "hmt", 0, temporary)) return false;
        std::ofstream file(temporary);
        file.imbue(std::locale::classic());
        write(file);
        file.close();
        const bool saved = file && MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved) DeleteFileA(temporary);
        return saved;
    }

    bool save()
    {
        if (!valid(test)) return false;
        return saveText(settingsPath, [](std::ostream& file)
        {
            file << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "heroineMoveTest 1\n"
            << test.start.x << ' ' << test.start.y << ' ' << test.start.z << '\n'
            << test.target.x << ' ' << test.target.y << ' ' << test.target.z << '\n'
            << test.speed << ' ' << test.arrivalRadius << ' ' << test.repeat << '\n';
        });
    }

    void ground(Vector3& point)
    {
        if (const auto* terrain = Manager::GetGameObj<MeshField>())
        {
            float height;
            if (terrain->tryGetHeight(point.x, point.z, height)) point.y = height;
        }
    }

    const char* stateName(HeroineMovement::State state)
    {
        switch (state)
        {
        case HeroineMovement::State::moving: return u8"移動中";
        case HeroineMovement::State::paused: return u8"一時停止";
        case HeroineMovement::State::arrived: return u8"到着";
        case HeroineMovement::State::blocked: return u8"障害物・段差・地形外で停止";
        default: return u8"待機";
        }
    }

    void drawMovement(Heroine& heroine)
    {
        if (!initialized)
        {
            test.start = heroine.GetPosition();
            test.target = test.start + Vector3{ 0, 0, 4 };
            load();
            initialized = true;
        }
        const auto& movement = heroine.movement();
        const auto state = movement.state();
        const bool active = state == HeroineMovement::State::moving || state == HeroineMovement::State::paused;
        ImGui::TextWrapped(u8"始点と終点を指定して、歩行と停止を繰り返し確認します。F3 / Escで閉じます。");
        ImGui::Separator();
        ImGui::BeginDisabled(active);
        ImGui::DragFloat3(u8"始点 XYZ", &test.start.x, 0.05f, -100000.0f, 100000.0f, "%.2f");
        if (ImGui::Button(u8"現在のヒロインを始点に")) test.start = heroine.GetPosition();
        ImGui::SameLine();
        if (ImGui::Button(u8"プレイヤーを始点に"))
            if (const auto* player = Manager::GetGameObj<Player>()) test.start = player->GetPosition();
        ImGui::DragFloat3(u8"終点 XYZ", &test.target.x, 0.05f, -100000.0f, 100000.0f, "%.2f");
        if (ImGui::Button(u8"現在のヒロインを終点に")) test.target = heroine.GetPosition();
        ImGui::SameLine();
        if (ImGui::Button(u8"プレイヤーを終点に"))
            if (const auto* player = Manager::GetGameObj<Player>()) test.target = player->GetPosition();
        if (ImGui::Button(u8"始点と終点を交換")) std::swap(test.start, test.target);
        ImGui::SameLine();
        if (ImGui::Button(u8"両端を地面に合わせる")) { ground(test.start); ground(test.target); }
        ImGui::SliderFloat(u8"移動速度", &test.speed, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat(u8"到着半径", &test.arrivalRadius, 0.01f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox(u8"往復を繰り返す", &test.repeat);
        ImGui::EndDisabled();
        ImGui::Checkbox(u8"始点・終点と経路を表示", &showMarkers);
        ImGui::TextDisabled(u8"Yは地形に接地。経路は直線、障害物は迂回しません。");
        if (active) ImGui::TextDisabled(u8"座標・速度の編集は停止してから行ってください。");
        ImGui::Separator();

        ImGui::BeginDisabled(!heroine.movementReady() || !valid(test));
        if (ImGui::Button(u8"始点から再生"))
        {
            if (!heroine.resetMovement(test.start)) status = u8"始点を配置できません。地形とコライダーを確認してください。";
            else status = heroine.moveTo(test.target, test.speed, test.arrivalRadius, test.repeat)
                ? u8"移動テストを開始しました。" : u8"終点を配置できません。地形とコライダーを確認してください。";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!active);
        if (ImGui::Button(state == HeroineMovement::State::paused ? u8"再開" : u8"一時停止"))
            heroine.pauseMovement(state != HeroineMovement::State::paused);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(u8"停止")) heroine.stopMovement();
        if (ImGui::Button(u8"停止して始点へ戻す"))
            status = heroine.resetMovement(test.start) ? u8"始点に戻しました。" : u8"始点を配置できません。";
        ImGui::Separator();
        ImGui::BeginDisabled(active);
        if (ImGui::Button(u8"設定を保存")) status = save() ? u8"移動テスト設定を保存しました。" : u8"保存できませんでした。";
        ImGui::SameLine();
        if (ImGui::Button(u8"設定を読込")) status = load() ? u8"移動テスト設定を読み込みました。" : u8"有効な移動テスト設定がありません。";
        ImGui::EndDisabled();
        ImGui::TextDisabled("asset/heroine-move-test.txt");
        if (status) ImGui::TextWrapped("%s", status);
        if (!heroine.movementReady()) ImGui::TextWrapped(u8"Idle / Walkモーションの読み込みを確認してください。");

        ImGui::Separator();
        const auto position = heroine.GetPosition();
        ImGui::Text(u8"状態: %s", stateName(movement.state()));
        ImGui::Text(u8"現在位置: %.2f, %.2f, %.2f", position.x, position.y, position.z);
        if (movement.state() != HeroineMovement::State::idle)
        {
            ImGui::Text(u8"現在の目標まで: %.2f / 到着回数: %u", movement.remaining(position), movement.completedLegs());
            const float length = hypotf(test.target.x - test.start.x, test.target.z - test.start.z);
            const float progress = length > 0.001f ? 1.0f - movement.remaining(position) / length : 1.0f;
            ImGui::ProgressBar(std::max(0.0f, std::min(1.0f, progress)), { -1, 0 });
        }
    }

    void drawLookAt(Heroine& heroine)
    {
        auto& settings = heroine.lookAt().settings;
        ImGui::Checkbox(u8"Look Atを有効にする", &settings.enabled);
        ImGui::TextWrapped(u8"小さな角度は瞳だけ。首・頭の追加回転には上限があり、遠方や背後は追いません。");
        int target = heroine.usesFixedLookTarget() ? 1 : 0;
        if (ImGui::Combo(u8"視線対象", &target, u8"プレイヤーの目の高さ\0固定した座標\0"))
        {
            if (target == 0) heroine.lookAtPlayer();
            else heroine.setLookTarget(heroine.lookTarget());
        }
        auto point = heroine.lookTarget();
        if (heroine.usesFixedLookTarget())
            if (ImGui::DragFloat3(u8"対象 XYZ", &point.x, 0.02f, -100000, 100000, "%.2f")) heroine.setLookTarget(point);
        if (ImGui::Button(u8"プレイヤーの位置で固定"))
            if (const auto* player = Manager::GetGameObj<Player>())
            {
                auto* camera = Manager::GetGameObj<Camera>();
                heroine.setLookTarget(player->GetPosition() + Vector3{ 0, camera ? camera->orbitSettings().lookHeight : 1.6f, 0 });
            }
        ImGui::Checkbox(u8"視線対象と向きを表示", &showLookMarkers);

        if (ImGui::CollapsingHeader(u8"瞳の追従", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(!heroine.eyesReady());
            ImGui::Checkbox(u8"瞳を動かす", &settings.eyesEnabled);
            ImGui::SliderFloat(u8"瞳 左右上限", &settings.eyeYawDegrees, 0, HeroineLookAtSettings::maxEyeYawDegrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"瞳 上下上限", &settings.eyePitchDegrees, 0, HeroineLookAtSettings::maxEyePitchDegrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped(u8"白目を抑えるため狭い範囲に制限。斜め方向は楕円状に抑えます。");
            settings.eyeOnlyYawDegrees = std::min(settings.eyeOnlyYawDegrees, settings.eyeYawDegrees);
            settings.eyeOnlyPitchDegrees = std::min(settings.eyeOnlyPitchDegrees, settings.eyePitchDegrees);
            ImGui::SliderFloat(u8"瞳だけで見る左右角度", &settings.eyeOnlyYawDegrees, 0, settings.eyeYawDegrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"瞳だけで見る上下角度", &settings.eyeOnlyPitchDegrees, 0, settings.eyePitchDegrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"瞳の追従速度", &settings.eyeResponse, 2, 30, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (!heroine.eyesReady()) ImGui::TextWrapped(u8"左右の目ボーンがありません。瞳の追従は無効です。");
        }
        if (ImGui::CollapsingHeader(u8"首・頭の追従", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox(u8"首・頭も少し動かす", &settings.headEnabled);
            ImGui::SliderFloat(u8"首＋頭 左右上限", &settings.headYawDegrees, 0, 35, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"首＋頭 上下上限", &settings.headPitchDegrees, 0, 20, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"首・頭の追従速度", &settings.headResponse, 1, 12, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"首への配分", &settings.neckWeight, 0, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextDisabled(u8"首・頭をOFFにすると瞳だけが追従します。");
        }
        if (ImGui::CollapsingHeader(u8"近距離・閉眼", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginDisabled(!heroine.eyelidsReady());
            ImGui::Checkbox(u8"密着したら目を閉じる", &settings.closeEyesWhenNear);
            ImGui::SliderFloat(u8"閉じ始める距離", &settings.nearDistance, 0.1f, 2, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            settings.fullyClosedDistance = std::min(settings.fullyClosedDistance, settings.nearDistance - 0.01f);
            settings.maxDistance = std::max(settings.nearDistance + 0.5f, settings.maxDistance);
            ImGui::SliderFloat(u8"全閉になる距離", &settings.fullyClosedDistance, 0, settings.nearDistance - 0.01f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat(u8"まぶたの開閉速度", &settings.eyelidResponse, 2, 30, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            ImGui::TextWrapped(u8"近距離でも追従を維持し、密着時は直前の向きを保ちます。プレイヤーは水平距離、固定座標は3D距離で閉眼を判定します。");
            if (!heroine.eyelidsReady()) ImGui::TextWrapped(u8"閉眼モーフが見つかりません。閉眼表示は無効です。");
        }
        if (ImGui::CollapsingHeader(u8"追従をやめる範囲"))
        {
            ImGui::SliderFloat(u8"左右の追従範囲", &settings.viewYawDegrees, 20, 85, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped(u8"正面から左右それぞれの角度。首・瞳の回転上限は保ち、端の10度で追従を弱めます。");
            settings.maxDistance = std::max(settings.nearDistance + 0.5f, settings.maxDistance);
            ImGui::SliderFloat(u8"最大距離", &settings.maxDistance, settings.nearDistance + 0.5f, 30, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::TextWrapped(u8"左右の追従範囲外や遠方では元のアニメーションへ戻します。首・瞳の回転上限は維持します。遮蔽判定はまだ行いません。");
        }
        ImGui::Separator();
        if (ImGui::Button(u8"視線設定を保存"))
            lookStatus = settings.valid() && saveText("asset/heroine-look-at.txt", [&settings](std::ostream& output) { settings.write(output); })
                ? u8"視線設定を保存しました。" : u8"視線設定を保存できませんでした。";
        ImGui::SameLine();
        if (ImGui::Button(u8"視線設定を読込"))
        {
            std::ifstream input("asset/heroine-look-at.txt");
            input.imbue(std::locale::classic());
            lookStatus = settings.read(input) ? u8"視線設定を読み込みました。" : u8"有効な視線設定がありません。";
            if (!heroine.eyesReady()) settings.eyesEnabled = false;
        }
        if (ImGui::Button(u8"視線設定を初期値へ"))
        {
            settings = HeroineLookAtSettings{};
            if (!heroine.eyesReady()) settings.eyesEnabled = false;
        }
        ImGui::TextDisabled("asset/heroine-look-at.txt");
        if (lookStatus) ImGui::TextWrapped("%s", lookStatus);
        const auto& gaze = heroine.lookAt();
        ImGui::Text(u8"状態: %s", gaze.tracking() ? u8"追従中" : u8"正面へ戻る / 範囲外");
        ImGui::Text(u8"首＋頭: 左右 %.1f / 上下 %.1f deg", XMConvertToDegrees(gaze.headYaw()), XMConvertToDegrees(gaze.headPitch()));
        ImGui::Text(u8"瞳: 左右 %.1f / 上下 %.1f deg", XMConvertToDegrees(gaze.eyeYaw()), XMConvertToDegrees(gaze.eyePitch()));
        ImGui::Text(u8"閉眼: %.0f%%", gaze.eyeClosure() * 100);
        if (ImGui::CollapsingHeader(u8"検出したボーン"))
        {
            for (const auto& bone : heroine.lookBones()) ImGui::TextUnformatted(bone.empty() ? u8"未検出" : bone.c_str());
            if (heroine.lookBones()[1].empty()) ImGui::TextWrapped(u8"頭ボーンがないためLook Atは無効です。");
        }
    }

    bool project(const Vector3& point, FXMMATRIX vp, ImVec2& pixel)
    {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(point.x, point.y, point.z, 1), vp));
        if (clip.w <= 0.001f || clip.z < 0 || clip.z > clip.w) return false;
        const auto size = ImGui::GetIO().DisplaySize;
        pixel = { (clip.x / clip.w + 1) * size.x * 0.5f, (1 - clip.y / clip.w) * size.y * 0.5f };
        return std::isfinite(pixel.x) && std::isfinite(pixel.y);
    }

    void marker(Vector3 point, const char* label, ImU32 color, FXMMATRIX vp)
    {
        ground(point);
        auto* draw = ImGui::GetBackgroundDrawList();
        ImVec2 previous;
        bool visible = false;
        for (int i = 0; i <= 32; ++i)
        {
            const float angle = i * XM_2PI / 32;
            ImVec2 current;
            const bool projected = project(point + Vector3{ cosf(angle) * 0.25f, 0.04f, sinf(angle) * 0.25f }, vp, current);
            if (visible && projected) draw->AddLine(previous, current, color, 2.0f);
            if (projected) previous = current;
            visible = projected;
        }
        ImVec2 base, top;
        if (project(point, vp, base) && project(point + Vector3{ 0, 0.65f, 0 }, vp, top))
        {
            draw->AddLine(base, top, color, 2.0f);
            draw->AddCircleFilled(top, 4, color);
            draw->AddText({ top.x + 8, top.y - 8 }, color, label);
        }
    }
}

bool DebugPanel::isOpen() { return open; }
void DebugPanel::toggle() { open = !open; }
void DebugPanel::close() { open = false; }

void DebugPanel::draw()
{
    if (!open) return;
    ImGui::SetNextWindowPos({ 16, 16 }, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({ 465, 620 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"ゲームデバッグ (F3)"))
    {
        if (ImGui::BeginTabBar("Debug sections"))
        {
            if (ImGui::BeginTabItem(u8"ヒロイン移動"))
            {
                if (auto* heroine = Manager::GetGameObj<Heroine>()) drawMovement(*heroine);
                else ImGui::TextUnformatted(u8"ヒロインがいません。");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"視線・Look At"))
            {
                if (auto* heroine = Manager::GetGameObj<Heroine>()) drawLookAt(*heroine);
                else ImGui::TextUnformatted(u8"ヒロインがいません。");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"表情"))
            {
                if (auto* heroine = Manager::GetGameObj<Heroine>()) HeroineExpressionPanel::draw(*heroine);
                else ImGui::TextUnformatted(u8"ヒロインがいません。");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(u8"AI会話"))
            {
                if (auto* conversation = Manager::GetGameObj<HeroineConversation>()) AI::DialoguePanel::draw(*conversation);
                else ImGui::TextUnformatted(u8"会話システムがありません。");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void DebugPanel::drawWorldMarkers()
{
    if (!open) return;
    auto* camera = Manager::GetGameObj<Camera>();
    if (!camera) return;
    const auto vp = camera->GetViewMatrix() * camera->projectionMatrix();
    ImVec2 a, b;
    if (showMarkers && initialized)
    {
        marker(test.start, u8"始点", IM_COL32(86, 236, 150, 255), vp);
        marker(test.target, u8"終点", IM_COL32(255, 181, 76, 255), vp);
        auto start = test.start, target = test.target;
        ground(start); ground(target);
        if (project(start + Vector3{ 0, 0.04f, 0 }, vp, a) && project(target + Vector3{ 0, 0.04f, 0 }, vp, b))
            ImGui::GetBackgroundDrawList()->AddLine(a, b, IM_COL32(160, 204, 255, 190), 2.0f);
    }
    if (showLookMarkers)
        if (auto* heroine = Manager::GetGameObj<Heroine>())
        {
            auto* draw = ImGui::GetBackgroundDrawList();
            if (project(heroine->lookOrigin(), vp, a) && project(heroine->lookOrigin() + heroine->lookDirection() * 1.5f, vp, b))
                draw->AddLine(a, b, IM_COL32(108, 235, 245, 255), 2.0f);
            if (project(heroine->lookTarget(), vp, a))
            {
                draw->AddCircle(a, 8, IM_COL32(246, 211, 107, 255), 0, 2);
                draw->AddText({ a.x + 12, a.y }, IM_COL32(246, 211, 107, 255), u8"視線対象");
            }
        }
}
