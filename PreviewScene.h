// =========================================================
// ファイルの情報[PreviewScene.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <memory>
#include "Scene.h"
#include "GameObject.h"
#include "Explosion.h"
#include "ToonShader.h"
#include "animationModel.h"
#include "PreviewPanel.h"
#include "PreviewCamera.h"
#include "PreviewTimeline.h"

class PreviewScene : public Scene
{
private:
    GameObject m_modelAnchor;
    std::unique_ptr<AnimationModel> m_model;
    ToonShader m_toonShader;
    Explosion m_effect;
    PreviewTimeline m_timeline;
    PreviewControls m_controls;
    PreviewCamera m_camera;
    std::string m_sampledClip;
    int m_sampledMorph = -1;
    int m_sampledBaseMorph = -1;
    std::vector<MorphTarget> m_sampledExtraMorphs;
    float m_sampledTime = -1.0f;
    float m_sampledWeight = -1.0f;
    float m_sampledExtraTime = -1.0f;

    bool loadModel(const char* path);
    bool selectMotion(const std::string& name);
    void sampleModel();

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;
};
