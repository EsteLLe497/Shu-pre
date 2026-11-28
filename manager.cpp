#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Camera.h"
#include "SkyDome.h"
#include "input.h"
#include "Game.h"
#include "audio.h"

#include "GameObject.h"
#include "Player.h"
#include "Heroine.h"
#include "SceneEditor.h"
#include "Scenery.h"
#include "ToonPanel.h"
#include "PreviewScene.h"
#include "GraphicsLab.h"
#include "planetarium.h"

#include <algorithm>
#include <vector>
#include <wrl/client.h>

std::list<GameObject*> Manager::m_GameObjects;
std::list<GameObject*> Manager::m_EditObjects;
Scene* Manager::m_Scene = nullptr;
Scene* Manager::m_NextScene = nullptr;
float Manager::m_ChangeTime = 0.0f;
float Manager::m_animationDeltaSeconds = 1.0f / 60.0f;

namespace
{
    bool previewWorkspace = false;

    void drawAlphaObjects(std::vector<GameObject*>& objects, Camera* camera)
    {
        if (objects.empty()) return;
        if (camera)
        {
            const auto view = camera->GetViewMatrix();
            const auto depth = [&view](const GameObject* object)
            {
                const auto& position = object->GetPosition();
                return XMVectorGetZ(XMVector3TransformCoord(XMVectorSet(position.x, position.y, position.z, 1.0f), view));
            };
            std::stable_sort(objects.begin(), objects.end(), [&depth](const GameObject* a, const GameObject* b)
            {
                return depth(a) > depth(b);
            });
        }

        auto* context = Renderer::GetDeviceContext();
        Microsoft::WRL::ComPtr<ID3D11BlendState> previousBlend;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> previousDepth;
        float blendFactor[4]{};
        UINT sampleMask = 0, stencilRef = 0;
        context->OMGetBlendState(&previousBlend, blendFactor, &sampleMask);
        context->OMGetDepthStencilState(&previousDepth, &stencilRef);

        Renderer::SetAddBlendEnable(false);
        Renderer::SetDepthEnable(false); // Test against opaque geometry without writing depth.
        for (auto* object : objects)
        {
            Renderer::SetObjectOpacity(object->opacity());
            object->Draw();
        }
        Renderer::SetObjectOpacity(1.0f);
        context->OMSetDepthStencilState(previousDepth.Get(), stencilRef);
        context->OMSetBlendState(previousBlend.Get(), blendFactor, sampleMask);
    }

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


// Graphics Lab ÇÃãNìÆÉÇÅ[ÉhÇí«â¡ íSìñ:ozawa souma        ì˙ït:2026/11/28
void Manager::Init(bool previewMode, bool graphicsLab)
{
    previewWorkspace = previewMode || graphicsLab;
	Renderer::Init(graphicsLab);
    if (!previewWorkspace)
        planetarium::init();
	Input::Init();
	ToonPanel::init(GetWindow(), previewWorkspace);
	Audio::InitMaster();

    if (graphicsLab)
        ChangeScene<GraphicsLab>(0.0f);
    else if (previewWorkspace)
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
    return true;
}

void Manager::endEditorPlay()
{
    if (m_EditObjects.empty()) return;
    clearObjects(m_GameObjects);
    m_GameObjects.swap(m_EditObjects);
    planetarium::reset();
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
    if (auto* sky = GetGameObj<SkyDome>()) sky->advance(frameSeconds, editing);
    const bool updateWorld = !editing && planetarium::updateWorld();
    const auto* player = updateWorld ? nullptr : GetGameObj<Player>();
    const auto* camera = GetGameObj<Camera>();
	if (m_Scene && updateWorld)
		m_Scene->Update();

	for (GameObject* obj : m_GameObjects) 
	{
        if (obj && !editing && (obj == camera || (updateWorld || obj == player))) {
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
    auto* sky = GetGameObj<SkyDome>();
    if (sky) sky->bindFrame();
    const auto* lightPlayer = GetGameObj<Player>();
    planetarium::bindLighting(lightPlayer ? lightPlayer->GetPosition() : Vector3(0.0f, 0.0f, 0.0f));
	Camera* camera = GetGameObj<Camera>();
	const bool editing = SceneEditor::isOpen();
    const bool showToonPanel = ToonPanel::isOpen();
    if (editing) ToonPanel::beginFrame();
	else if (showToonPanel)
	{
		Heroine* heroine = GetGameObj<Heroine>();
		ToonPanel::beginFrame(heroine ? &heroine->toonSettings() : nullptr,
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

	std::vector<GameObject*> alphaObjects;
	for (int layer = 0; layer < 5; layer++)
	{
        // UIÇÕÉhÅ[ÉÄÇÃêFÇ‚é’ï¡ÇÃâeãøÇéÛÇØÇ»Ç¢ÅB
        if (layer == 1 && camera)
            Scenery::begin(camera->GetViewMatrix(), camera->projectionMatrix(),
                ToonPanel::isPauseMenuOpen() ? 0.0f : m_animationDeltaSeconds);
        if (layer == 2)
        {
            if (camera)
            {
                SceneEditor::drawVegetation(camera->renderPosition());
                SceneEditor::drawWildlife(camera->renderPosition(),
                    ToonPanel::isPauseMenuOpen() ? 0.0f : m_animationDeltaSeconds);
            }
            Scenery::flush();
        }
        if (layer == 4)
        {
            if (camera && sky) sky->Draw();
            drawAlphaObjects(alphaObjects, camera);
            if (camera) SceneEditor::drawWater(camera->renderPosition(),
                camera->GetViewMatrix() * camera->projectionMatrix(),
                ToonPanel::isPauseMenuOpen() ? 0.0f : m_animationDeltaSeconds);
        }
        if (editing && layer == 4) continue;
        if (layer == 4 && camera)
            planetarium::draw(camera->GetViewMatrix(), camera->projectionMatrix());
        for (auto* object : m_GameObjects)
        {
            if (!object || object->GetLayer() != layer) continue;
            if (layer > 0 && layer < 4 && object->usesAlphaBlend())
            {
                if (object->opacity() > 0.0f) alphaObjects.push_back(object);
            }
            else
                object->Draw();
        }
	}
    if (editing) SceneEditor::draw();
    else SceneEditor::drawPlayMode();
    ToonPanel::draw();
	Renderer::End();
}
