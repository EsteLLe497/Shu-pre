#include "Result.h"
#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Polygon2D.h"
#include "input.h"
#include "Title.h"

#include "GameObject.h"

void Result::Init()
{

	Manager::AddGameObj<Polygon2D>()->Init(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, L"asset\\texture\\result.png");
}


void Result::Uninit()
{

}

void Result::Update()
{
	if (Input::GetKeyTrigger(VK_RETURN))
	{
		Manager::ChangeScene<Title>(3.0f);
	}
}

void Result::Draw()
{

}