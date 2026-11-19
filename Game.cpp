#include "Game.h"

#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Polygon2D.h"
#include "Particle.h"
#include "Camera.h"
//#include "field.h"
#include "MeshField.h"
#include "Wave.h"
#include "Player.h"
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


void Game::Init()
{


	Manager::AddGameObj<Camera>();
	//Manager::AddGameObj<field>();
	MeshField* terrain = Manager::AddGameObj<MeshField>();
	//Manager::AddGameObj<Wave>();
	Manager::AddGameObj<SkyDome>();

	Player* player = Manager::AddGameObj<Player>();
	// 外観を読み込み、開始時は標準の三人称カメラを使う。
	ToonPreset::loadAppearance(player->toonSettings());

	SceneEditor::init(*player, *terrain);
	Manager::AddGameObj<Score>()->SetPosition({ 100.0f,100.0f,0.0f });
}


void Game::Uninit()
{
    SceneEditor::shutdown();
}

void Game::Update()
{
	// Keep the scene active while testing the visual enemy prototype.
}

void Game::Draw()
{

}
