#pragma once

class GameObject;
class Scene;
class Manager
{
private:
	static std::list<GameObject*> m_GameObjects;
	static Scene* m_Scene;
	static Scene* m_NextScene;
	static float m_ChangeTime;
	static float m_animationDeltaSeconds;

public:
	static void Init(bool previewMode);
	static void Uninit();
	static void Update(float frameSeconds);
	static float animationDeltaSeconds() { return m_animationDeltaSeconds; }
	static void Draw();

	template<typename T>
	static void ChangeScene(float time)
	{
		if (m_NextScene != nullptr)
			return;

		m_ChangeTime = time;
		m_NextScene = new T();
	}

	template<typename T>
	static T* AddGameObj()
	{
		T* obj = new T();
		obj->Init();
		m_GameObjects.push_back(obj);

		return obj;
	}

	template<typename T>
	static T* GetGameObj()
	{
		for (GameObject* gameObject : m_GameObjects)
		{
			T* find = dynamic_cast<T*>(gameObject);
			if (find != nullptr)
			{
				return find;
			}
		}
		return nullptr;
	}

	template<typename T>
	static std::vector<T*> GetGameObjs()
	{
		std::vector<T*> gameObjects;
		for (GameObject* gameObject : m_GameObjects)
		{
			T* find = dynamic_cast<T*>(gameObject);
			if (find != nullptr)
			{
				gameObjects.push_back(find);
			}
		}
		return gameObjects;
	}


};