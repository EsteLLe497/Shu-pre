#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Camera.h"
#include "input.h"
#include "Title.h"
#include "Game.h"
#include "Result.h"
#include "audio.h"

#include "GameObject.h"
#include "Player.h"
#include "Enemy.h"
#include "SceneEditor.h"
#include "ToonPanel.h"
#include "PreviewScene.h"
#include "planetarium.h"

std::list<GameObject*> Manager::m_GameObjects;
std::list<GameObject*> Manager::m_EditObjects;
Scene* Manager::m_Scene = nullptr;
Scene* Manager::m_NextScene = nullptr;
float Manager::m_ChangeTime = 0.0f;
float Manager::m_animationDeltaSeconds = 1.0f / 60.0f;
float Manager::m_hitStopRemaining = 0.0f;

namespace
{
    bool previewWorkspace = false;

    void clearObjects(std::list<GameObject*>& objects)
    {
        for (auto* object : objects)
        {
            if (!object) continue;
            object->Uninit();
            delete object;
        }
        objects.clear();
    }
}


void Manager::Init(bool previewMode)
{
    previewWorkspace = previewMode;
    m_hitStopRemaining = 0.0f;
	Renderer::Init();
    if (!previewMode)
        planetarium::init();
	Input::Init();
	ToonPanel::init(GetWindow(), previewMode);
	Audio::InitMaster();

    if (previewWorkspace)
        ChangeScene<PreviewScene>(0.0f);
    else
        ChangeScene<Game>(0.0f);
}


void Manager::Uninit()
{
	if (m_Scene != nullptr)
		m_Scene->Uninit();

	clearObjects(m_GameObjects);
    clearObjects(m_EditObjects);
    delete m_Scene;
    delete m_NextScene;
    m_Scene = nullptr;
    m_NextScene = nullptr;

	Audio::UninitMaster();
	ToonPanel::uninit();
    planetarium::uninit();
	Renderer::Uninit();
	Input::Uninit();
}

bool Manager::beginEditorPlay()
{
    if (m_NextScene || !m_EditObjects.empty() || !dynamic_cast<Game*>(m_Scene)) return false;
    m_EditObjects.swap(m_GameObjects);
    planetarium::reset();
    m_hitStopRemaining = 0.0f;
    return true;
}

void Manager::endEditorPlay()
{
    if (m_EditObjects.empty()) return;
    clearObjects(m_GameObjects);
    m_GameObjects.swap(m_EditObjects);
    planetarium::reset();
    m_hitStopRemaining = 0.0f;
}

void Manager::requestHitStop(float seconds)
{
    // ï°êîÇÃìGÇ÷ìØéûÇ…ìñÇΩÇ¡ÇƒÇ‡ÅAí‚é~éûä‘Çë´Çµë±ÇØÇ»Ç¢ÅB
    if (seconds > 0.0f && seconds <= 1.0f)
        m_hitStopRemaining = std::max(m_hitStopRemaining, seconds);
}

void Manager::Update(float frameSeconds)
{
	// ï`âÊïââ◊Ç…ÇÊÇÈçƒê∂ÇÃíxÇÍÇñhÇ¨ÅAí∑Ç¢íÜífÇ©ÇÁÇÃïúãAÇ≈ÇÕã}Ç…îÚÇŒÇ≥Ç»Ç¢ÅB
	m_animationDeltaSeconds = fminf(frameSeconds, 0.1f);
	float dt = 1.0f / 60.0f;
	SceneEditor::processPlayMode();
	Input::Update(!ToonPanel::isOpen());
    if (ToonPanel::isPauseMenuOpen()) return;

    const bool editing = SceneEditor::isOpen();
    const bool stopping = m_hitStopRemaining > 0.0f;
    m_hitStopRemaining = std::max(0.0f, m_hitStopRemaining - m_animationDeltaSeconds);
    // îíì_ñ≈ÇÕí‚é~íÜÇ‚ÉfÉoÉbÉOÇ≈ìGÇé~ÇﬂÇƒÇ¢ÇƒÇ‡é¿éûä‘Ç≈êiÇﬂÇÈÅB
    for (auto* enemy : GetGameObjs<Enemy>()) enemy->updateHitFeedback(m_animationDeltaSeconds);
    const bool updateWorld = !editing && !stopping && planetarium::updateWorld();
    const auto* player = updateWorld ? nullptr : GetGameObj<Player>();
    const auto* camera = GetGameObj<Camera>();
	if (m_Scene && updateWorld)
		m_Scene->Update();

	for (GameObject* obj : m_GameObjects) 
	{
        // ñΩíÜÇµÇΩÉtÉåÅ[ÉÄÇÃìríÜÇ≈Ç‡ÅAà»ç~ÇÃÉQÅ[ÉÄçXêVÇÕé~ÇﬂÇÈÅBÉJÉÅÉâÇÕìÆÇ©Ç∑ÅB
        const bool hitStopped = stopping || m_hitStopRemaining > 0.0f;
        if (obj && !editing && (obj == camera || (!hitStopped && (updateWorld || obj == player)))) {
			obj->Update();
		}
	}

	m_GameObjects.remove_if([](GameObject* object)
		{
			return object->Destory();
		});

	if (m_NextScene != nullptr)
	{
		m_ChangeTime -= dt;
		if (m_ChangeTime < 0.0f)
		{
			if (m_Scene != nullptr)
			{
				m_Scene->Uninit();
				delete m_Scene;
			}

			clearObjects(m_GameObjects);
            planetarium::reset();
            m_hitStopRemaining = 0.0f;

			m_Scene = m_NextScene;
			m_Scene->Init();


			m_NextScene = nullptr;
		}
	}

}

void Manager::Draw()
{
    if (previewWorkspace && m_Scene)
    {
        constexpr float studioColor[] = { 0.055f, 0.071f, 0.106f, 1.0f };
        Renderer::Begin(studioColor);
        m_Scene->Draw();
        Renderer::End();
        return;
    }

	Renderer::Begin();
    const auto* lightPlayer = GetGameObj<Player>();
    planetarium::bindLighting(lightPlayer ? lightPlayer->GetPosition() : Vector3(0.0f, 0.0f, 0.0f));
	Camera* camera = GetGameObj<Camera>();
	const bool editing = SceneEditor::isOpen();
    const bool showToonPanel = ToonPanel::isOpen();
    if (editing) ToonPanel::beginFrame();
	else if (showToonPanel)
	{
		Player* player = GetGameObj<Player>();
		ToonPanel::beginFrame(player ? &player->toonSettings() : nullptr,
			camera ? &camera->orbitSettings() : nullptr);
	}
    else ToonPanel::beginFrame();

	if (camera)
	{
		Vector3 forward = camera->GetFoward();
		Vector3 position = camera->GetPosition();

		for (GameObject* obj : m_GameObjects)
		{
			obj->CalcCameraZ(position, forward);
		}
		m_GameObjects.sort([](GameObject* a, GameObject* b)
			{
				return a->GetCameraZ() > b->GetCameraZ();
			});
	}

	for (int layer = 0; layer < 5; layer++)
	{
        // UIÇÕÉhÅ[ÉÄÇÃêFÇ‚é’ï¡ÇÃâeãøÇéÛÇØÇ»Ç¢ÅB
        if (layer == 4)
        {
            if (auto* player = GetGameObj<Player>())
                player->drawEffects();
        }
        if (editing && layer == 4) continue;
        if (layer == 4 && camera)
            planetarium::draw(camera->GetViewMatrix(), camera->projectionMatrix());
		for (GameObject* obj : m_GameObjects) {
			if (obj->GetLayer() == layer)
			{
				if (obj != nullptr) {
					obj->Draw();
				}
			}
		}
	}
    for (auto* enemy : GetGameObjs<Enemy>()) enemy->drawHealthBar();
    if (editing) SceneEditor::draw();
    else SceneEditor::drawPlayMode();
    ToonPanel::draw();
	Renderer::End();
}
