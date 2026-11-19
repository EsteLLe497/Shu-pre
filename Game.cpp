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


	Manager::AddGameObj<Camera>();
	//Manager::AddGameObj<field>();
	Manager::AddGameObj<MeshField>();
	//Manager::AddGameObj<Wave>();
	Manager::AddGameObj<SkyDome>();

	Player* player = Manager::AddGameObj<Player>();
	// 外観を読み込み、開始時は標準の三人称カメラを使う。
	ToonPreset::loadAppearance(player->toonSettings());

	const Vector3 smallEnemyPositions[] = {
        { -2.4f, 0.0f, 5.4f },
        {  0.0f, 0.0f, 5.0f },
        {  2.4f, 0.0f, 5.4f },
    };
    for (const Vector3& position : smallEnemyPositions)
    {
        Enemy* enemy = Manager::AddGameObj<Enemy>();
        enemy->Configure(false);
        enemy->SetPosition(position);
    }

    Enemy* midBoss = Manager::AddGameObj<Enemy>();
    midBoss->Configure(true);
    midBoss->SetPosition({ -1.0f, 0.0f, 15.0f });



	Box* box = Manager::AddGameObj<Box>();
	box->SetPosition({ 5.0f, 0.0f, 5.0f });
	box->SetScale({ 2.0f, 1.0f, 2.0f });

	Manager::AddGameObj<Grass>()->SetPosition({ 10.0f,0.0f,-5.0f });

	/*for (int i = 0; i < 300; i++)
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
	}*/


	Manager::AddGameObj<Particle>()->SetPosition({ -2.0f,1.0f,-1.0f });

	Manager::AddGameObj<Score>()->SetPosition({ 100.0f,100.0f,0.0f });
}


void Game::Uninit()
{

}

void Game::Update()
{
	// Keep the scene active while testing the visual enemy prototype.
}

void Game::Draw()
{

}
