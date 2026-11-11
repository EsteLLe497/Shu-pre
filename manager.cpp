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

std::list<GameObject*> Manager::m_GameObjects;
Scene* Manager::m_Scene = nullptr;
Scene* Manager::m_NextScene = nullptr;
float Manager::m_ChangeTime = 0.0f;


void Manager::Init()
{
	Renderer::Init();
	Input::Init();
	ToonPanel::init(GetWindow());
	Audio::InitMaster();

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

	Audio::UninitMaster();
	ToonPanel::uninit();
	Renderer::Uninit();
	Input::Uninit();
}

void Manager::Update()
{
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
