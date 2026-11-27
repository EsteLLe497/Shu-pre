#include "Game.h"

#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Polygon2D.h"
#include "Particle.h"
#include "Camera.h"
//#include "field.h"
#include "MeshField.h"
#include "Player.h"
#include "Heroine.h"
#include "SceneAudio.h"
#include "SceneEditor.h"
#include "ToonPreset.h"
#include "Tree.h"
#include "Grass.h"
#include "Box.h"
#include "SkyDome.h"
#include "input.h"

#include "GameObject.h"


std::pair<Player*, MeshField*> Game::createWorld()
{


	Manager::AddGameObj<Camera>();
	//Manager::AddGameObj<field>();
	MeshField* terrain = Manager::AddGameObj<MeshField>();
	Manager::AddGameObj<SkyDome>();

	Player* player = Manager::AddGameObj<Player>();
	// 外観を読み込み、開始時は標準の三人称カメラを使う。
	auto* heroine = Manager::AddGameObj<Heroine>();
	ToonPreset::loadAppearance(heroine->toonSettings());

    return { player, terrain };
}

void Game::Init()
{
    const auto world = createWorld();
    SceneAudio::init();
    SceneEditor::init(*world.first, *world.second);
    // 一人称の開始方向をマップに保存されたプレイヤーの向きに合わせる。
    Manager::GetGameObj<Camera>()->orbitSettings().yaw = world.first->GetRotation().y;
}


void Game::Uninit()
{
    SceneEditor::shutdown();
    SceneAudio::shutdown();
}

void Game::Update()
{
	SceneEditor::updatePhysics(Manager::animationDeltaSeconds());
}

void Game::Draw()
{

}
