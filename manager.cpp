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
#include "ToonPanel.h"
#include "PreviewScene.h"

std::list<GameObject*> Manager::m_GameObjects;
Scene* Manager::m_Scene = nullptr;
Scene* Manager::m_NextScene = nullptr;
float Manager::m_ChangeTime = 0.0f;
float Manager::m_animationDeltaSeconds = 1.0f / 60.0f;

namespace
{
    bool previewWorkspace = false;
}


void Manager::Init(bool previewMode)
{
    previewWorkspace = previewMode;
	Renderer::Init();
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

	for (GameObject* obj : m_GameObjects) {
		if (obj != nullptr) {
			obj->Uninit();
			delete obj;
			obj = nullptr;
		}
	}
	m_GameObjects.clear();
    delete m_Scene;
    delete m_NextScene;
    m_Scene = nullptr;
    m_NextScene = nullptr;

	Audio::UninitMaster();
	ToonPanel::uninit();
	Renderer::Uninit();
	Input::Uninit();
}

void Manager::Update(float frameSeconds)
{
	// •`‰æ•‰‰×‚É‚æ‚éÄ¶‚Ì’x‚ê‚ð–h‚¬A’·‚¢’†’f‚©‚ç‚Ì•œ‹A‚Å‚Í‹}‚É”ò‚Î‚³‚È‚¢B
	m_animationDeltaSeconds = fminf(frameSeconds, 0.1f);
	float dt = 1.0f / 60.0f;
	Input::Update(!ToonPanel::isOpen());

	if(m_Scene != nullptr)
	m_Scene->Update();

	for (GameObject* obj : m_GameObjects) 
	{
		if (obj != nullptr) {
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

			for (GameObject* obj : m_GameObjects)
			{
				if (obj != nullptr) {
					obj->Uninit();
					delete obj;
					obj = nullptr;
				}
			}

			m_GameObjects.clear();

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
	Camera* camera = GetGameObj<Camera>();
	const bool showToonPanel = ToonPanel::isOpen();
	if (showToonPanel)
	{
		Player* player = GetGameObj<Player>();
		ToonPanel::beginFrame(player ? &player->toonSettings() : nullptr,
			camera ? &camera->orbitSettings() : nullptr);
	}

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
		for (GameObject* obj : m_GameObjects) {
			if (obj->GetLayer() == layer)
			{
				if (obj != nullptr) {
					obj->Draw();
				}
			}
		}
	}
	if (showToonPanel)
		ToonPanel::draw();
	Renderer::End();
}
