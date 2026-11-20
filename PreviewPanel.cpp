// =========================================================
// ファイルの情報[PreviewPanel.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "Explosion.h"
#include "PreviewPanel.h"
#include "ActionFile.h"
#include "ActionSequence.h"
#include "PreviewCamera.h"
#include "ToonPanel.h"
#include "ToonPreset.h"
#include "PreviewTimeline.h"
#include "PlayerAnimationAssets.h"
#include "imgui/imgui.h"

namespace
{
    constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

    constexpr float trackLabelWidth = 68.0f;
    constexpr char defaultWeapon[] = "asset/model/Weapon/Short_Sword/mfx025_viking-sword-A.FBX";
    using LoadWeapon = std::function<bool(const std::string&)>;

    void clampWeaponTiming(WeaponTrack& weapon, float duration)
    {
        weapon.disappear = std::min(weapon.disappear, duration);
        weapon.middle = std::min(weapon.middle, weapon.disappear);
        weapon.appear = std::min(weapon.appear, weapon.middle);
        for (std::size_t i = 0; i < weapon.keys.size(); ++i)
            weapon.keys[i].time = std::max(weapon.appear, std::min(weapon.disappear, weapon.keyTime(i)));

    }

    float timelineHeight(const PreviewControls& controls)
    {
        const auto extraTracks = controls.extraMorphs.size() + (controls.weapon.model.empty() ? 0 : 1);
        const float addedHeight = 23.0f * static_cast<float>(extraTracks);
        return fminf(196.0f + addedHeight, fmaxf(196.0f, ImGui::GetIO().DisplaySize.y * 0.48f));
    }

    // ピークを金色のひし形と秒数で表示し、クリックするとその時刻へ移動する。
    void resizeTimeline(PreviewTimeline& timeline, PreviewControls& controls)
    {
        timeline.seek(timeline.time);
        if (controls.category == 2) clampWeaponTiming(controls.weapon, timeline.duration);
        timeline.effectStart = std::min(timeline.effectStart, timeline.duration);
        if (controls.category != 0) return;
        timeline.faceEnd = std::min(timeline.faceEnd, timeline.duration);
        timeline.facePeak = std::min(timeline.facePeak, timeline.faceEnd);
        timeline.faceStart = std::min(timeline.faceStart, timeline.facePeak);
        timeline.effectStart = std::min(timeline.effectStart, timeline.duration);
        for (auto& morph : controls.extraMorphs)
        {
            morph.end = std::min(morph.end, timeline.duration);
            morph.peak = std::min(morph.peak, morph.end);
            morph.start = std::min(morph.start, morph.peak);
        }
    }

    void drawTrackBar(float start, float end, float peak, ImU32 color, bool enabled, PreviewTimeline& timeline)
    {
        const float width = fmaxf(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("Track", ImVec2(width, 19.0f));
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 corner = ImGui::GetItemRectMax();
        const auto timeToX = [&](float time) {
            return origin.x + width * fmaxf(0.0f, fminf(1.0f, time / timeline.playbackEnd()));
        };

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, corner, IM_COL32(38, 46, 61, 255), 3.0f);
        for (int i = 1; i < 20; ++i)
        {
            const float x = origin.x + width * i / 20;
            drawList->AddLine({x, origin.y}, {x, corner.y}, IM_COL32(70, 80, 95, 130));
        }
        drawList->AddRectFilled(ImVec2(timeToX(start), origin.y + 2.0f),
            ImVec2(timeToX(end), corner.y - 2.0f),
            enabled ? color : IM_COL32(85, 90, 101, 255), 2.0f);

        if (ImGui::IsItemActive())
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const bool nearPeak = peak >= 0.0f && fabsf(mouseX - timeToX(peak)) <= 7.0f;
            timeline.playing = false;
            if (ImGui::IsItemActivated() || fabsf(ImGui::GetIO().MouseDelta.x) > 0.0f)
                timeline.seek(nearPeak ? peak : (mouseX - origin.x) / width * timeline.playbackEnd());
        }

        const float playhead = timeToX(timeline.time);
        drawList->AddLine(ImVec2(playhead, origin.y), ImVec2(playhead, corner.y), IM_COL32_WHITE, 2.0f);

        if (peak >= 0.0f)
        {
            const float x = timeToX(peak);
            const float y = (origin.y + corner.y) * 0.5f;
            constexpr ImU32 markerColor = IM_COL32(255, 221, 121, 255);
            drawList->PushClipRect(origin, corner, true);
            drawList->AddQuadFilled(ImVec2(x, y - 7), ImVec2(x + 6, y), ImVec2(x, y + 7),
                ImVec2(x - 6, y), markerColor);
            drawList->AddQuad(ImVec2(x, y - 7), ImVec2(x + 6, y), ImVec2(x, y + 7),
                ImVec2(x - 6, y), IM_COL32(26, 24, 30, 255));

            drawList->PopClipRect();
            if (ImGui::IsItemHovered() && fabsf(ImGui::GetIO().MousePos.x - x) <= 7.0f)
                ImGui::SetTooltip("Peak %.3f s | Click to seek", peak);
        }
    }

    void drawTrack(const char* label, bool& enabled, float start, float end,
        ImU32 color, PreviewTimeline& timeline, int track, float peak = -1.0f)
    {
        ImGui::PushID(track);
        ImGui::Checkbox("##enabled", &enabled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
        ImGui::SameLine();
        if (ImGui::ColorButton("##solo", ImGui::ColorConvertU32ToFloat4(color),
            ImGuiColorEditFlags_NoTooltip, ImVec2(19, 19)))
            timeline.soloTrack = timeline.soloTrack == track ? -1 : track;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: solo", label);
        ImGui::SameLine(trackLabelWidth);
        drawTrackBar(start, end, peak, color, (track != 3 || enabled) && timeline.trackEnabled(track), timeline);
        ImGui::PopID();
    }

    void drawMorphTrack(const MorphTarget& morph, PreviewTimeline& timeline)
    {
        ImGui::PushID(morph.name.c_str());
        if (ImGui::ColorButton("##morph", ImVec4(0.57f, 0.45f, 0.8f, 1),
            ImGuiColorEditFlags_NoTooltip, ImVec2(19, 19)))
        {
            timeline.playing = false;
            timeline.seek(morph.peak);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", morph.name.c_str());
        ImGui::SameLine(trackLabelWidth);
        drawTrackBar(morph.start, morph.end, morph.peak, IM_COL32(145, 115, 204, 255),
            timeline.trackEnabled(1) && morph.weight > 0.0f, timeline);
        ImGui::PopID();
    }

    // ソロと再生速度は試聴用。書き出す値は各レイヤーの設定から取得する。
    ActionSequence captureAction(const PreviewTimeline& timeline, const PreviewControls& controls,
        const std::vector<std::string>& morphs)
    {
        ActionSequence action;
        action.duration = timeline.duration;
        action.speed = timeline.speed;
        if (controls.category == 2) action.weapon = controls.weapon;
        action.motionEnabled = controls.category == 0 && timeline.motionEnabled;
        action.clip = controls.category == 2 ? "Idle" : controls.clipName;
        action.faceEnabled = controls.category == 0 && timeline.faceEnabled && !morphs.empty();
        if (controls.category == 0 && !morphs.empty())
        {
            action.targetMorph = morphs[controls.morphIndex];
            action.extraMorphs = controls.extraMorphs;
            if (controls.baseMorphIndex >= 0)
                action.baseMorph = morphs[controls.baseMorphIndex];
        }

        action.faceStart = timeline.faceStart;
        action.facePeak = timeline.facePeak;
        action.faceEnd = timeline.faceEnd;
        action.faceStrength = timeline.faceStrength;
        action.smoothFaceBlend = timeline.smoothFaceBlend;
        if (controls.category == 2) action.faceStart = action.facePeak = action.faceEnd = 0.0f;
        action.effectEnabled = timeline.effectEnabled;
        action.effectStart = timeline.effectStart;
        action.effectX = controls.effectX;
        action.effectHeight = controls.effectHeight;
        return action;
    }

    const char* applyAction(const ActionSequence& action, PreviewTimeline& timeline, PreviewControls& controls,
        const std::vector<std::string>& morphs, const std::function<bool(const std::string&)>& selectMotion,
        const LoadWeapon& loadWeapon)
    {
        if (controls.category == 2)
        {
            if (action.weapon.model.empty() || !loadWeapon(action.weapon.model)) return "Could not load weapon.";
            controls.weapon = action.weapon;
            strcpy_s(controls.weaponPath, controls.weapon.model.c_str());
            controls.selectedKey = 0;
            controls.effectX = action.effectX;
            controls.effectHeight = action.effectHeight;
            timeline.duration = action.duration;
            timeline.speed = action.speed;
            timeline.motionEnabled = timeline.faceEnabled = false;
            timeline.effectEnabled = action.effectEnabled;
            timeline.effectStart = action.effectStart;
            timeline.weaponVisualEnd = action.weapon.visualEnd();
            timeline.playing = false;
            timeline.soloTrack = -1;
            timeline.seek(0);
            return nullptr;
        }
        const auto base = std::find(morphs.begin(), morphs.end(), action.baseMorph);
        const auto target = std::find(morphs.begin(), morphs.end(), action.targetMorph);
        if (action.faceEnabled && !hasExpressionMorphs(action, morphs))
            return "This model is missing a shape key used by the action.";

        if (!selectMotion(action.clip) && action.motionEnabled)
            return "Could not load the motion used by the action.";
        timeline.speed = action.speed;
        controls.baseMorphIndex = base == morphs.end() ? -1 : static_cast<int>(base - morphs.begin());
        controls.morphIndex = target == morphs.end() ? 0 : static_cast<int>(target - morphs.begin());
        controls.extraMorphs.clear();
        for (const auto& morph : action.extraMorphs)
        {
            const auto found = std::find(morphs.begin(), morphs.end(), morph.name);
            if (found != morphs.end() && morph.name != morphs[controls.morphIndex])
                controls.extraMorphs.push_back(morph);
        }
        controls.effectX = action.effectX;
        controls.effectHeight = action.effectHeight;
        timeline.duration = action.duration;
        timeline.motionEnabled = action.motionEnabled;
        timeline.faceEnabled = action.faceEnabled;
        timeline.faceStart = action.faceStart;
        timeline.facePeak = action.facePeak;
        timeline.faceEnd = action.faceEnd;
        timeline.faceStrength = action.faceStrength;
        timeline.smoothFaceBlend = action.smoothFaceBlend;
        timeline.effectEnabled = action.effectEnabled;
        timeline.effectStart = action.effectStart;
        timeline.playing = false;
        timeline.soloTrack = -1;
        controls.selectedKey = 0;
        timeline.seek(0.0f);
        return nullptr;
    }

    void drawWeaponKeys(PreviewTimeline& timeline, PreviewControls& controls)
    {
        auto& weapon = controls.weapon;
        for (std::size_t i = 0; i < weapon.keys.size(); ++i) weapon.keys[i].time = weapon.keyTime(i);
        ImGui::PushID("WeaponKeys");
        ImGui::Dummy(ImVec2(trackLabelWidth - 8, 24));
        ImGui::SameLine(trackLabelWidth);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("##keys", ImVec2(width, 24));
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + 24), IM_COL32(28, 36, 49, 255), 3);
        const auto toX = [&](float t) { return origin.x + width * t / timeline.playbackEnd(); };
        const float mouseTime = std::max(weapon.appear, std::min(weapon.disappear,
            (ImGui::GetIO().MousePos.x - origin.x) / width * timeline.playbackEnd()));
        int nearest = -1;
        float distance = 9;
        for (int i = 0; i < static_cast<int>(weapon.keys.size()); ++i)
        {
            const float x = toX(weapon.keyTime(i)), y = origin.y + 12;
            const float d = fabsf(ImGui::GetIO().MousePos.x - x);
            if (d < distance) { distance = d; nearest = i; }
            const ImU32 color = i == controls.selectedKey ? IM_COL32(255, 220, 110, 255) : IM_COL32(80, 210, 210, 255);
            draw->AddQuadFilled({x, y-7}, {x+6, y}, {x, y+7}, {x-6, y}, color);
        }
        const float head = toX(timeline.time);
        draw->AddLine({head, origin.y}, {head, origin.y+24}, IM_COL32_WHITE, 2);
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(0))
            {
                controls.selectedKey = nearest;
                timeline.playing = false;
                timeline.seek(nearest >= 0 ? weapon.keyTime(nearest) : mouseTime);
            }
            if (ImGui::IsMouseDoubleClicked(0) && nearest < 0 && weapon.keys.size() < 512)
            {
                auto pose = weapon.sample(mouseTime); pose.time = mouseTime;
                auto it = std::upper_bound(weapon.keys.begin(), weapon.keys.end(), mouseTime,
                    [](float t, const WeaponPose& p) { return t < p.time; });
                controls.selectedKey = static_cast<int>(it - weapon.keys.begin());
                weapon.keys.insert(it, pose);
            }
            if (ImGui::IsMouseClicked(1) && nearest >= 0 && weapon.keys.size() > 2)
            {
                weapon.keys.erase(weapon.keys.begin() + nearest);
                controls.selectedKey = -1;
            }
        }
        const int selected = controls.selectedKey;
        if ((ImGui::IsItemActive() || ImGui::IsItemDeactivated())
            && fabsf(ImGui::GetMouseDragDelta(0).x) > ImGui::GetIO().MouseDragThreshold && selected >= 0
            && selected < static_cast<int>(weapon.keys.size()))
        {
            weapon.keys[selected].time = std::max(selected ? weapon.keyTime(selected - 1) : weapon.appear,
                std::min(selected + 1 < static_cast<int>(weapon.keys.size()) ? weapon.keyTime(selected + 1)
                    : weapon.disappear, mouseTime));
            timeline.seek(weapon.keys[selected].time);
        }
        ImGui::PopID();
    }

    void drawTimeline(PreviewTimeline& timeline, PreviewControls& controls, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion, const LoadWeapon& loadWeapon)
    {
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        const float height = timelineHeight(controls);
        ImGui::SetNextWindowPos(ImVec2(16.0f, fmaxf(16.0f, size.y - height - 16.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(fmaxf(1.0f, size.x - 32.0f), height), ImGuiCond_Always);
        if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            if (ImGui::Button(timeline.playing ? "Pause" : "Play", ImVec2(65.0f, 0.0f)))
            {
                if (!timeline.playing && timeline.time >= timeline.playbackEnd())
                    timeline.seek(0.0f);
                timeline.playing = !timeline.playing;
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop"))
            {
                timeline.playing = false;
                timeline.seek(0.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("-1 frame"))
            {
                timeline.playing = false;
                timeline.seek(timeline.time - PreviewTimeline::frameStep);
            }
            ImGui::SameLine();
            if (ImGui::Button("+1 frame"))
            {
                timeline.playing = false;
                timeline.seek(timeline.time + PreviewTimeline::frameStep);
            }
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &timeline.loop);
            ImGui::SameLine();
            ImGui::Text("%.3f / %.3f s", timeline.time, timeline.playbackEnd());

            const float controlsHeight = 2.0f * ImGui::GetFrameHeightWithSpacing();
            const float trackHeight = fmaxf(40.0f, ImGui::GetContentRegionAvail().y - controlsHeight);
            if (ImGui::BeginChild("Tracks", ImVec2(0.0f, trackHeight)))
            {
                if (controls.category == 1)
                {
                    ImGui::PushID("EnemyTrack");
                    ImGui::Dummy(ImVec2(trackLabelWidth - 8, 19));
                    ImGui::SameLine(trackLabelWidth);
                    drawTrackBar(0, timeline.duration, -1, IM_COL32(218, 95, 80, 255), true, timeline);
                    ImGui::PopID();
                }
                if (controls.category == 0)
                {
                drawTrack("Motion", timeline.motionEnabled, 0.0f, timeline.duration,
                    IM_COL32(71, 130, 204, 255), timeline, 0);
                drawTrack("Expression", timeline.faceEnabled, timeline.faceStart, timeline.faceEnd,
                    IM_COL32(179, 109, 181, 255), timeline, 1, timeline.facePeak);
                for (const auto& morph : controls.extraMorphs)
                    drawMorphTrack(morph, timeline);
                }
                if (controls.category == 2 && !controls.weapon.model.empty())
                {
                    drawWeaponKeys(timeline, controls);
                    drawTrack("Weapon", controls.weapon.enabled, controls.weapon.appear, controls.weapon.visualEnd(),
                        IM_COL32(70, 180, 180, 255), timeline, 3);
                }
                if (controls.category != 1)
                drawTrack("Effect", timeline.effectEnabled, timeline.effectStart,
                    timeline.effectStart + Explosion::frameCount * Explosion::frameDuration,
                    IM_COL32(218, 151, 72, 255), timeline, 2);
            }
            ImGui::EndChild();

            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::SliderFloat("Duration", &timeline.duration, 0.5f, 10.0f, "%.2f s", sliderFlags))
            {
                resizeTimeline(timeline, controls);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::SliderFloat("Speed", &timeline.speed, 0.1f, 2.0f, "%.2fx", sliderFlags);

            // 保存と再読込で同じファイル名欄を使う。
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("Action name", controls.actionName,
                sizeof(controls.actionName)))
            {
                controls.actionStatus.clear();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(controls.category == 1);
            if (ImGui::Button("Export Action"))
            {
                const auto actionPath = ActionFile::pathForName(controls.actionName);
                if (actionPath.empty())
                    controls.actionStatus = "Enter a name using letters, numbers, spaces, _ or -.";
                else if (!ActionFile::saveNamed(controls.actionName, captureAction(timeline, controls, morphs)))
                    controls.actionStatus = "Could not export " + actionPath;
                else
                    controls.actionStatus = "Saved " + actionPath;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Action"))
            {
                controls.savedActionNames = ActionFile::listNames();
                ImGui::OpenPopup("Saved actions");
            }

            ImGui::EndDisabled();
            ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f), ImVec2(520.0f, 280.0f));
            if (ImGui::BeginPopup("Saved actions"))
            {
                if (controls.savedActionNames.empty())
                    ImGui::TextDisabled("No actions saved in asset/actions.");

                for (const auto& name : controls.savedActionNames)
                {
                    ActionSequence candidate;
                    if (!ActionFile::loadNamed(name, candidate)
                        || candidate.weapon.enabled != (controls.category == 2)) continue;
                    if (!ImGui::Selectable(name.c_str(), _stricmp(name.c_str(), controls.actionName) == 0))
                        continue;

                    const auto actionPath = ActionFile::pathForName(name);
                    ActionSequence action;
                    if (!ActionFile::loadNamed(name, action))
                        controls.actionStatus = "No valid action at " + actionPath;
                    else if (const char* error = applyAction(action, timeline, controls, morphs, selectMotion, loadWeapon))
                        controls.actionStatus = error;
                    else
                    {
                        strcpy_s(controls.actionName, name.c_str());
                        controls.actionStatus = "Loaded " + actionPath;
                    }
                }
                ImGui::EndPopup();
            }
            if (!controls.actionStatus.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(controls.actionStatus.c_str());
            }
        }
        ImGui::End();
    }

    // -1 はモーフを加えないニュートラル姿勢として扱う。
    bool hasExtraMorph(const std::string& name, const std::vector<MorphTarget>& extras,
        const MorphTarget* except = nullptr)
    {
        return std::any_of(extras.begin(), extras.end(), [&](const MorphTarget& morph) {
            return &morph != except && morph.name == name;
        });
    }

    void selectMorph(const char* label, int& index, const std::vector<std::string>& morphs, bool allowNeutral,
        const std::vector<MorphTarget>* extras = nullptr)
    {
        if (!ImGui::BeginCombo(label, index < 0 ? "Neutral" : morphs[index].c_str()))
            return;

        if (allowNeutral && ImGui::Selectable("Neutral", index < 0))
            index = -1;

        for (int i = 0; i < static_cast<int>(morphs.size()); ++i)
        {
            if (extras && hasExtraMorph(morphs[i], *extras))
                continue;

            ImGui::PushID(i);
            if (ImGui::Selectable(morphs[i].c_str(), i == index))
                index = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // 同じキーが二重に加算されないよう、使用中のキーを候補から外す。
    void editExpressionTiming(float& start, float& peak, float& end, float duration)
    {
        if (ImGui::SliderFloat("Start", &start, 0.0f, duration, "%.3f s", sliderFlags))
        {
            peak = fmaxf(peak, start);
            end = fmaxf(end, peak);
        }
        if (ImGui::SliderFloat("Peak", &peak, 0.0f, duration, "%.3f s", sliderFlags))
        {
            start = fminf(start, peak);
            end = fmaxf(end, peak);
        }
        if (ImGui::SliderFloat("End", &end, 0.0f, duration, "%.3f s", sliderFlags))
        {
            peak = fminf(peak, end);
            start = fminf(start, peak);
        }
    }

    void editExtraMorphs(PreviewControls& controls, const std::vector<std::string>& morphs,
        const PreviewTimeline& timeline)
    {
        auto& extras = controls.extraMorphs;
        const auto& main = morphs[controls.morphIndex];
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(extras.size()); ++i)
        {
            auto& extra = extras[i];
            ImGui::PushID(i);
            if (ImGui::BeginCombo("Extra key", extra.name.c_str()))
            {
                for (const auto& name : morphs)
                {
                    if (name != main && !hasExtraMorph(name, extras, &extra)
                        && ImGui::Selectable(name.c_str(), name == extra.name))
                        extra.name = name;
                }
                ImGui::EndCombo();
            }

            editExpressionTiming(extra.start, extra.peak, extra.end, timeline.duration);
            ImGui::Checkbox("Smooth blend", &extra.smooth);
            ImGui::SetNextItemWidth(105.0f);
            ImGui::SliderFloat("Key weight", &extra.weight, 0.0f, 1.0f, "%.2f", sliderFlags);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove"))
                removeIndex = i;
            ImGui::PopID();
        }
        if (removeIndex >= 0)
            extras.erase(extras.begin() + removeIndex);

        const auto available = std::find_if(morphs.begin(), morphs.end(), [&](const std::string& name) {
            return name != main && !hasExtraMorph(name, extras);
        });
        const bool canAdd = available != morphs.end() && extras.size() < MorphTarget::maxExtraTargets;
        ImGui::BeginDisabled(!canAdd);
        if (ImGui::Button("Add Shape Key") && canAdd)
            extras.push_back({ *available, 1.0f, timeline.faceStart, timeline.facePeak,
                timeline.faceEnd, timeline.smoothFaceBlend });
        ImGui::EndDisabled();
    }

    void editWeapon(PreviewTimeline& timeline, PreviewControls& controls, const LoadWeapon& loadWeapon)
    {
        auto& weapon = controls.weapon;
        ImGui::InputText("Weapon FBX", controls.weaponPath, sizeof(controls.weaponPath));
        const bool load = ImGui::Button("Load weapon");
        ImGui::SameLine();
        const bool shortSword = ImGui::Button("Short Sword");
        if (shortSword)
            strcpy_s(controls.weaponPath, defaultWeapon);
        if ((load || shortSword) && loadWeapon(controls.weaponPath))
        {
            weapon.model = controls.weaponPath;
            weapon.enabled = true;
            clampWeaponTiming(weapon, timeline.duration);
        }
        if (!controls.weaponStatus.empty())
            ImGui::TextWrapped("%s", controls.weaponStatus.c_str());
        if (weapon.model.empty())
            return;

        ImGui::Checkbox("Show during attack", &weapon.enabled);
        ImGui::Checkbox("Move whole path", &controls.moveWholePath);
        ImGui::TextDisabled("Off: move nearest pose key");
        ImGui::SliderFloat("Appear", &weapon.appear, 0.0f, weapon.middle, "%.3f s", sliderFlags);
        ImGui::SliderFloat("End pose time", &weapon.disappear, weapon.appear, timeline.duration, "%.3f s", sliderFlags);
        clampWeaponTiming(weapon, timeline.duration);
        ImGui::SliderFloat("Scale", &weapon.scale, 0.001f, 20.0f, "%.3f",
            sliderFlags | ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat3("Pivot XYZ", weapon.pivot.data(), 0.01f, -10.0f, 10.0f, "%.2f", sliderFlags);
        if (ImGui::Button("Add pose at playhead") && weapon.keys.size() < 512)
        {
            const float time = std::max(weapon.appear, std::min(weapon.disappear, timeline.time));
            auto pose = weapon.sample(time); pose.time = time;
            auto it = std::upper_bound(weapon.keys.begin(), weapon.keys.end(), time,
                [](float t, const WeaponPose& p) { return t < p.time; });
            controls.selectedKey = static_cast<int>(it - weapon.keys.begin());
            weapon.keys.insert(it, pose);
        }
        int remove = -1;
        for (int i = 0; i < static_cast<int>(weapon.keys.size()); ++i)
        {
            ImGui::PushID(i);
            if (ImGui::TreeNode("Pose", "Pose %d", i + 1))
            {
                if (ImGui::Button("Seek")) { timeline.playing = false; timeline.seek(weapon.keyTime(i)); controls.selectedKey = i; }
                ImGui::SliderFloat("Time", &weapon.keys[i].time, i ? weapon.keyTime(i-1) : weapon.appear,
                    i+1 < static_cast<int>(weapon.keys.size()) ? weapon.keyTime(i+1) : weapon.disappear, "%.3f s", sliderFlags);
                ImGui::DragFloat3("Position XYZ", weapon.keys[i].position.data(), 0.01f, -10, 10, "%.2f", sliderFlags);
                ImGui::DragFloat3("Pitch/Yaw/Roll", weapon.keys[i].angles.data(), 1, -1440, 1440, "%.0f deg", sliderFlags);
                if (weapon.keys.size() > 2 && ImGui::Button("Remove pose")) remove = i;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove >= 0) { weapon.keys.erase(weapon.keys.begin()+remove); controls.selectedKey = -1; }

        ImGui::Checkbox("Trail", &weapon.trail);
        if (weapon.trail)
        {
            ImGui::ColorEdit3("Trail color", weapon.trailColor.data());
            ImGui::SliderFloat("Trail length", &weapon.trailLength, 0.01f, 1.0f, "%.2f s", sliderFlags);
            ImGui::DragFloat3("Blade base", weapon.trailBase.data(), 0.01f, -10.0f, 10.0f, "%.2f", sliderFlags);
        }
        ImGui::Checkbox("End dissolve", &weapon.dissolve);
        if (weapon.dissolve)
        {
            if (ImGui::Button("Preview dissolve"))
            {
                timeline.playing = false;
                timeline.seek(weapon.disappear + weapon.dissolveDuration * 0.45f);
            }
            ImGui::SliderFloat("Dissolve time", &weapon.dissolveDuration, 0.08f, 1.5f, "%.2f s", sliderFlags);
            ImGui::SliderFloat("Edge glow", &weapon.dissolveGlow, 0.1f, 3.0f, "%.2f", sliderFlags);
            ImGui::ColorEdit3("Edge color", weapon.dissolveColor.data());
            ImGui::Checkbox("End particles", &weapon.dissolveParticles);
            if (weapon.dissolveParticles)
            {
                ImGui::SliderInt("Particle count", &weapon.particleCount, 8, 128, "%d", sliderFlags);
                ImGui::ColorEdit3("Particle color", weapon.particleColor.data());
            }
        }
        ImGui::Checkbox("Effect at blade tip", &weapon.effectOnWeapon);
        if (weapon.trail || weapon.effectOnWeapon)
            ImGui::DragFloat3("Blade tip", weapon.trailTip.data(), 0.01f, -10.0f, 10.0f, "%.2f", sliderFlags);
        ImGui::Checkbox("Attack camera target", &weapon.cameraEnabled);
        if (weapon.cameraEnabled)
            ImGui::DragFloat3("Target offset XYZ", weapon.cameraTarget.data(), 0.01f, -10.0f, 10.0f, "%.2f", sliderFlags);
    }

    void drawInspector(PreviewTimeline& timeline, PreviewControls& controls, PreviewCamera& camera,
        ToonSettings& shader, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion, const LoadWeapon& loadWeapon,
        const std::function<double()>& clipDuration)
    {
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(fmaxf(16.0f, size.x - 336.0f), 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320.0f, fmaxf(160.0f, size.y - timelineHeight(controls) - 48.0f)), ImGuiCond_Always);
        if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            ImGui::PushItemWidth(-102.0f);
            if (controls.category == 0 && ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginCombo("Clip", controls.clipName.c_str()))
                {
                    for (const auto& name : controls.motionNames)
                    {
                        if (ImGui::Selectable(name.c_str(), name == controls.clipName))
                        {
                            controls.motionStatus = selectMotion(name)
                                ? nullptr : "Could not load this FBX or match its rig.";
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Button("Refresh motions"))
                {
                    controls.motionNames = PlayerAnimationAssets::listMotions();
                    controls.motionStatus = "Motion list refreshed.";
                }
                if (ImGui::Button("Use clip duration") && clipDuration() > 0.0)
                {
                    timeline.duration = std::max(0.5f, std::min(10.0f, static_cast<float>(clipDuration())));
                    resizeTimeline(timeline, controls);
                }
                if (controls.motionStatus)
                    ImGui::TextWrapped("%s", controls.motionStatus);
                ImGui::TextDisabled("Muted motion holds frame 0.");
            }

            if (controls.category == 2 && ImGui::CollapsingHeader("Attack weapon", controls.weapon.enabled ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                editWeapon(timeline, controls, loadWeapon);

            if (controls.category == 0 && ImGui::CollapsingHeader("Expression", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (morphs.empty())
                {
                    ImGui::TextWrapped("No morph targets. Export a Blender shape key and reload.");
                    ImGui::BeginDisabled();
                    ImGui::Button("Add Shape Key");
                    ImGui::EndDisabled();
                }
                else
                {
                    selectMorph("Base expression", controls.baseMorphIndex, morphs, true);
                    selectMorph("Shape key", controls.morphIndex, morphs, false, &controls.extraMorphs);
                    ImGui::Checkbox("Smooth blend", &timeline.smoothFaceBlend);
                    editExpressionTiming(timeline.faceStart, timeline.facePeak, timeline.faceEnd, timeline.duration);
                    ImGui::SliderFloat("Weight", &timeline.faceStrength, 0.0f, 1.0f, "%.3f", sliderFlags);
                    ImGui::Separator();
                    editExtraMorphs(controls, morphs, timeline);
                }
            }

            if (controls.category != 1 && ImGui::CollapsingHeader("Effect", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat("Trigger", &timeline.effectStart, 0.0f, timeline.duration, "%.3f s", sliderFlags);
                ImGui::SliderFloat("Offset X", &controls.effectX, -2.0f, 2.0f, "%.2f", sliderFlags);
                ImGui::SliderFloat("Height", &controls.effectHeight, 0.0f, 2.0f, "%.2f", sliderFlags);
            }

            if (ImGui::CollapsingHeader("Camera"))
            {
                ImGui::SliderAngle("Yaw", &camera.yaw);
                ImGui::SliderAngle("Pitch", &camera.pitch, -25.0f, 65.0f);
                ImGui::SliderFloat("Distance", &camera.distance, PreviewCamera::minDistance,
                    PreviewCamera::maxDistance, "%.2f", sliderFlags);
                ImGui::SliderFloat("Look height", &camera.lookHeight, 0.0f, 2.5f, "%.2f", sliderFlags);
                if (ImGui::Button("Reset view"))
                    camera.reset();
            }

            if (ImGui::CollapsingHeader("Toon shader"))
            {
                if (ImGui::Button("Load game preset"))
                    controls.shaderStatus = ToonPreset::loadAppearance(shader) ? "Game preset loaded." : "No valid game preset.";
                ImGui::SameLine();
                if (ImGui::Button("Save to game"))
                    controls.shaderStatus = ToonPreset::saveAppearance(shader) ? "Game preset saved." : "Could not save game preset.";

                if (controls.shaderStatus)
                    ImGui::TextWrapped("%s", controls.shaderStatus);

                ImGui::Indent();
                ToonPanel::editAppearance(shader);
                ImGui::Unindent();
            }
            ImGui::PopItemWidth();
        }
        ImGui::End();
    }
}

// =========================================================
// 描画用の状態を編集し、モデル再読み込みの要求を返す
// =========================================================
bool PreviewPanel::draw(PreviewTimeline& timeline, PreviewControls& controls, PreviewCamera& camera,
    ToonSettings& shader, const std::vector<std::string>& morphs,
    const std::function<bool(const std::string&)>& selectMotion,
    const LoadWeapon& loadWeapon, const std::function<double()>& clipDuration)
{
    bool reload = false;
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(298.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("Asset Preview", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        const int previousCategory = controls.category;
        if (ImGui::Combo("Category", &controls.category, "Player\0Enemy\0Weapon\0"))
        {
            controls.categoryTimelines[previousCategory] = timeline;
            controls.categoryNames[previousCategory] = controls.actionName;
            controls.categoryEffectX[previousCategory] = controls.effectX;
            controls.categoryEffectHeight[previousCategory] = controls.effectHeight;
            timeline = controls.categoryTimelines[controls.category];
            controls.effectX = controls.categoryEffectX[controls.category];
            controls.effectHeight = controls.categoryEffectHeight[controls.category];
            timeline.soloTrack = -1;
            timeline.playing = false;
            timeline.seek(0.0f);
            strcpy_s(controls.actionName, controls.categoryNames[controls.category].c_str());
            camera.reset();
            if (controls.category == 1) { camera.distance = 6.0f; camera.lookHeight = 1.1f; }
            if (controls.category == 2) { camera.yaw = 0.8f; camera.distance = 5.5f; camera.lookHeight = 0.9f; }
        }
        if (controls.category == 1)
            ImGui::Combo("Enemy action", &controls.enemyAction, "Idle\0Punch\0Slam\0Charge\0");
        if (controls.category == 2)
            ImGui::Checkbox("Player context", &controls.showPlayerContext);
        ImGui::Checkbox("Show XYZ axes", &controls.showWorldAxes);
        if (controls.category == 0)
        {
        ImGui::TextUnformatted("Character FBX");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##CharacterFBX", controls.characterPath, sizeof(controls.characterPath));
        reload = ImGui::Button("Load character");
        ImGui::SameLine();
        if (ImGui::Button("Reload Blender export"))
        {
            strcpy_s(controls.characterPath, PlayerAnimationAssets::previewModel);
            reload = true;
        }
        }
        if (controls.category == 2 && ImGui::Button("Open Attack01"))
        {
            ActionSequence action;
            if (!ActionFile::loadAttack(action))
                controls.actionStatus = "Could not read Attack01.";
            else if (const char* error = applyAction(action, timeline, controls, morphs, selectMotion, loadWeapon))
                controls.actionStatus = error;
            else
            {
                strcpy_s(controls.actionName, "Attack01");
                controls.actionStatus = "Loaded Attack01.";
                timeline.seek(action.weapon.middle);
            }
        }
        if (controls.category == 0)
        {
            ImGui::TextWrapped("%s", controls.assetStatus.c_str());
            ImGui::Text("%zu morph targets", morphs.size());
        }
        if (controls.category == 1) ImGui::TextDisabled("Procedural enemy");
        ImGui::TextDisabled("RMB: orbit | MMB: pan");
        ImGui::TextDisabled("Wheel / Alt + RMB: zoom | F: frame");
        ImGui::TextDisabled("Click weapon to move");
    }
    ImGui::End();

    drawInspector(timeline, controls, camera, shader, morphs, selectMotion, loadWeapon, clipDuration);
    timeline.weaponVisualEnd = controls.category == 2 && controls.weapon.enabled ? controls.weapon.visualEnd() : 0.0f;
    drawTimeline(timeline, controls, morphs, selectMotion, loadWeapon);
    return reload;
}
