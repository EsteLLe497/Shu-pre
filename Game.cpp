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
#include "SceneAudio.h"
#include "SceneEditor.h"
#include "ToonPreset.h"
#include "Enemy.h"
#include "bullet.h"
#include "Explosion.h"
#include "Tree.h"
#include "Grass.h"
#include "Box.h"
#include "SkyDome.h"
#include "input.h"
#include "Score.h"

#include "GameObject.h"
#include "Result.h"


std::pair<Player*, MeshField*> Game::createWorld()
{


	Manager::AddGameObj<Camera>();
	//Manager::AddGameObj<field>();
	MeshField* terrain = Manager::AddGameObj<MeshField>();
	Manager::AddGameObj<SkyDome>();

	Player* player = Manager::AddGameObj<Player>();
	// 外観を読み込み、開始時は標準の三人称カメラを使う。
	ToonPreset::loadAppearance(player->toonSettings());

	Manager::AddGameObj<Score>()->SetPosition({ 100.0f,100.0f,0.0f });
    return { player, terrain };
}

void Game::Init()
{
    const auto world = createWorld();
    SceneAudio::init();
    SceneEditor::init(*world.first, *world.second);
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
