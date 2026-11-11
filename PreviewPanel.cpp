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

    constexpr float trackLabelWidth = 188.0f;

    float timelineHeight(const PreviewControls& controls)
    {
        const float addedHeight = 23.0f * static_cast<float>(controls.extraMorphs.size());
        return fminf(196.0f + addedHeight, fmaxf(196.0f, ImGui::GetIO().DisplaySize.y * 0.48f));
    }

    // ピークを金色のひし形と秒数で表示し、クリックするとその時刻へ移動する。
    void drawTrackBar(float start, float end, float peak, ImU32 color, bool enabled, PreviewTimeline& timeline)
    {
        const float width = fmaxf(1.0f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("Track", ImVec2(width, 19.0f));
        const ImVec2 origin = ImGui::GetItemRectMin();
        const ImVec2 corner = ImGui::GetItemRectMax();
        const auto timeToX = [&](float time) {
            return origin.x + width * fmaxf(0.0f, fminf(1.0f, time / timeline.duration));
        };

        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, corner, IM_COL32(38, 46, 61, 255), 3.0f);
        drawList->AddRectFilled(ImVec2(timeToX(start), origin.y + 2.0f),
            ImVec2(timeToX(end), corner.y - 2.0f),
            enabled ? color : IM_COL32(85, 90, 101, 255), 2.0f);

        if (ImGui::IsItemActive())
        {
            const float mouseX = ImGui::GetIO().MousePos.x;
            const bool nearPeak = peak >= 0.0f && fabsf(mouseX - timeToX(peak)) <= 7.0f;
            timeline.playing = false;
            if (ImGui::IsItemActivated() || fabsf(ImGui::GetIO().MouseDelta.x) > 0.0f)
                timeline.seek(nearPeak ? peak : (mouseX - origin.x) / width * timeline.duration);
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

            char label[32];
            ////snprintf(label, sizeof(label), "Peak %.3f s", peak);
            const float textWidth = ImGui::CalcTextSize(label).x;
            const float textX = x + 10.0f + textWidth < corner.x ? x + 10.0f : x - 10.0f - textWidth;
            const float textY = y - ImGui::GetTextLineHeight() * 0.5f;
            if (textX >= origin.x)
            {
                drawList->AddRectFilled(ImVec2(textX - 2, textY - 1),
                    ImVec2(textX + textWidth + 2, textY + ImGui::GetTextLineHeight()),
                    IM_COL32(26, 24, 30, 235), 2.0f);
                drawList->AddText(ImVec2(textX, textY), markerColor, label);
            }
            drawList->PopClipRect();
            if (ImGui::IsItemHovered() && fabsf(ImGui::GetIO().MousePos.x - x) <= 7.0f)
                ImGui::SetTooltip("Peak %.3f s | Click to seek", peak);
        }
    }

    void drawTrack(const char* label, bool& enabled, float start, float end,
        ImU32 color, PreviewTimeline& timeline, int track, float peak = -1.0f)
    {
        ImGui::PushID(track);
        ImGui::Checkbox(label, &enabled);
        ImGui::SameLine(112.0f);
        if (ImGui::SmallButton(timeline.soloTrack == track ? "Solo on" : "Solo"))
            timeline.soloTrack = timeline.soloTrack == track ? -1 : track;
        ImGui::SameLine(trackLabelWidth);
        drawTrackBar(start, end, peak, color, timeline.trackEnabled(track), timeline);
        ImGui::PopID();
    }

    void drawMorphTrack(const MorphTarget& morph, PreviewTimeline& timeline)
    {
        ImGui::PushID(morph.name.c_str());
        if (ImGui::Selectable(morph.name.c_str(), false, ImGuiSelectableFlags_None, ImVec2(172.0f, 19.0f)))
        {
            timeline.playing = false;
            timeline.seek(morph.peak);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", morph.name.c_str());
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
        action.motionEnabled = timeline.motionEnabled;
        action.clip = controls.clipName;
        action.faceEnabled = timeline.faceEnabled && !morphs.empty();
        if (!morphs.empty())
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
        action.effectEnabled = timeline.effectEnabled;
        action.effectStart = timeline.effectStart;
        action.effectX = controls.effectX;
        action.effectHeight = controls.effectHeight;
        return action;
    }

    const char* applyAction(const ActionSequence& action, PreviewTimeline& timeline, PreviewControls& controls,
        const std::vector<std::string>& morphs, const std::function<bool(const std::string&)>& selectMotion)
    {
        const auto base = std::find(morphs.begin(), morphs.end(), action.baseMorph);
        const auto target = std::find(morphs.begin(), morphs.end(), action.targetMorph);
        if (action.faceEnabled && !hasExpressionMorphs(action, morphs))
            return "This model is missing a shape key used by the action.";

        if (!selectMotion(action.clip) && action.motionEnabled)
            return "Could not load the motion used by the action.";
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
        timeline.seek(0.0f);
        return nullptr;
    }

    void drawTimeline(PreviewTimeline& timeline, PreviewControls& controls, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion)
    {
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        const float height = timelineHeight(controls);
        ImGui::SetNextWindowPos(ImVec2(16.0f, fmaxf(16.0f, size.y - height - 16.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(fmaxf(1.0f, size.x - 32.0f), height), ImGuiCond_Always);
        if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            if (ImGui::Button(timeline.playing ? "Pause" : "Play", ImVec2(65.0f, 0.0f)))
            {
                if (!timeline.playing && timeline.time >= timeline.duration)
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
            ImGui::Text("%.3f / %.3f s", timeline.time, timeline.duration);

            const float controlsHeight = 2.0f * ImGui::GetFrameHeightWithSpacing();
            const float trackHeight = fmaxf(40.0f, ImGui::GetContentRegionAvail().y - controlsHeight);
            if (ImGui::BeginChild("Tracks", ImVec2(0.0f, trackHeight)))
            {
                drawTrack("Motion", timeline.motionEnabled, 0.0f, timeline.duration,
                    IM_COL32(71, 130, 204, 255), timeline, 0);
                drawTrack("Expression", timeline.faceEnabled, timeline.faceStart, timeline.faceEnd,
                    IM_COL32(179, 109, 181, 255), timeline, 1, timeline.facePeak);
                for (const auto& morph : controls.extraMorphs)
                    drawMorphTrack(morph, timeline);
                drawTrack("Effect", timeline.effectEnabled, timeline.effectStart,
                    timeline.effectStart + Explosion::frameCount * Explosion::frameDuration,
                    IM_COL32(218, 151, 72, 255), timeline, 2);
            }
            ImGui::EndChild();

            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::SliderFloat("Duration", &timeline.duration, 0.5f, 10.0f, "%.2f s", sliderFlags))
            {
                timeline.seek(timeline.time);
                timeline.faceStart = fminf(timeline.faceStart, timeline.duration);
                timeline.facePeak = fminf(timeline.facePeak, timeline.duration);
                timeline.faceEnd = fminf(timeline.faceEnd, timeline.duration);
                timeline.effectStart = fminf(timeline.effectStart, timeline.duration);
                for (auto& morph : controls.extraMorphs)
                {
                    morph.start = fminf(morph.start, timeline.duration);
                    morph.peak = fminf(morph.peak, timeline.duration);
                    morph.end = fminf(morph.end, timeline.duration);
                }
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

            ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 0.0f), ImVec2(520.0f, 280.0f));
            if (ImGui::BeginPopup("Saved actions"))
            {
                if (controls.savedActionNames.empty())
                    ImGui::TextDisabled("No actions saved in asset/actions.");

                for (const auto& name : controls.savedActionNames)
                {
                    if (!ImGui::Selectable(name.c_str(), _stricmp(name.c_str(), controls.actionName) == 0))
                        continue;

                    const auto actionPath = ActionFile::pathForName(name);
                    ActionSequence action;
                    if (!ActionFile::loadNamed(name, action))
                        controls.actionStatus = "No valid action at " + actionPath;
                    else if (const char* error = applyAction(action, timeline, controls, morphs, selectMotion))
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

    void drawInspector(PreviewTimeline& timeline, PreviewControls& controls, PreviewCamera& camera,
        ToonSettings& shader, const std::vector<std::string>& morphs,
        const std::function<bool(const std::string&)>& selectMotion)
    {
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(fmaxf(16.0f, size.x - 336.0f), 16.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(320.0f, fmaxf(160.0f, size.y - timelineHeight(controls) - 48.0f)), ImGuiCond_Always);
        if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
        {
            ImGui::PushItemWidth(-102.0f);
            if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen))
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
                if (controls.motionStatus)
                    ImGui::TextWrapped("%s", controls.motionStatus);
                ImGui::TextDisabled("Muted motion holds frame 0.");
            }

            if (ImGui::CollapsingHeader("Expression", ImGuiTreeNodeFlags_DefaultOpen))
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

            if (ImGui::CollapsingHeader("Effect", ImGuiTreeNodeFlags_DefaultOpen))
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
    const std::function<bool(const std::string&)>& selectMotion)
{
    bool reload = false;
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(298.0f, 0.0f), ImGuiCond_Always);
    if (ImGui::Begin("Asset Preview", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        reload = ImGui::Button("Reload Blender export");
        ImGui::TextWrapped("%s", controls.assetStatus.c_str());
        ImGui::Text("%zu morph targets", morphs.size());
        ImGui::TextDisabled("Alt + LMB: orbit | MMB: pan");
        ImGui::TextDisabled("Wheel / Alt + RMB: zoom | F: frame");
    }
    ImGui::End();

    drawInspector(timeline, controls, camera, shader, morphs, selectMotion);
    drawTimeline(timeline, controls, morphs, selectMotion);
    return reload;
}
