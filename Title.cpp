#include "Title.h"
#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Polygon2D.h"
#include "input.h"
#include "Game.h"
#include "GameObject.h"

void Title::Init()
{

	Manager::AddGameObj<Polygon2D>()->Init(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, L"asset\\bill_board\\tora.png");
}


void Title::Uninit()
{

}

void Title::Update()
{
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		Manager::ChangeScene<Game>(3.0f);
	}
}

void Title::Draw()
{

}