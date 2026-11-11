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


	Camera* camera = Manager::AddGameObj<Camera>();
	//Manager::AddGameObj<field>();
	Manager::AddGameObj<MeshField>();
	//Manager::AddGameObj<Wave>();
	Manager::AddGameObj<SkyDome>();

	Player* player = Manager::AddGameObj<Player>();
	// 保存済みの調整値があれば、ゲーム開始時に復元する。
	ToonPreset::load(player->toonSettings(), camera->orbitSettings());

	Manager::AddGameObj<Enemy>()->SetPosition({ -2.0f,0.0f,1.0f });
	/*Manager::AddGameObj<Enemy>()->SetPosition({ 0.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 2.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 2.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 3.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 4.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 5.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 6.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 7.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 8.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 9.0f,0.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 5.0f,-1.0f,1.0f });
	Manager::AddGameObj<Enemy>()->SetPosition({ 5.0f,1.0f,1.0f });*/



	Box* box = Manager::AddGameObj<Box>();
	box->SetPosition({ 5.0f, 0.0f, 5.0f });
	box->SetScale({ 2.0f, 1.0f, 2.0f });

	Manager::AddGameObj<Grass>()->SetPosition({ 10.0f,0.0f,-5.0f });

	for (int i = 0; i < 300; i++)
	{
		Tree* tree = Manager::AddGameObj<Tree>();

		Vector3 position = {
			-100.0f + 200.0f * ((float)rand() / RAND_MAX),
			-1.0f,
			-100.0f + 200.0f * ((float)rand() / RAND_MAX)
		};
		tree->SetPosition(position);

		Vector3 scale = {
			2.0f,
			2.0f + 3.0f * ((float)rand() / RAND_MAX),
			1.0f
		};
		tree->SetScale(scale);

		Vector3 rot = {
			0.0f,
			0.0f,
			-0.1f + 0.2f * ((float)rand() / RAND_MAX)
		};
		tree->SetRotation(rot);
	}


	Manager::AddGameObj<Particle>()->SetPosition({ -2.0f,1.0f,-1.0f });

	Manager::AddGameObj<Score>()->SetPosition({ 100.0f,100.0f,0.0f });
}


void Game::Uninit()
{

}

void Game::Update()
{
	// Keep the game scene active while no enemies are spawned for model preview.
}

void Game::Draw()
{

}
