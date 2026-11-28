#pragma once

class GameObject;
class Scene;
class Manager
{
private:
	static std::list<GameObject*> m_GameObjects;
    static std::list<GameObject*> m_EditObjects;
	static Scene* m_Scene;
	static Scene* m_NextScene;
	static float m_ChangeTime;
	static float m_animationDeltaSeconds;
    static float m_hitStopRemaining;

public:
	static void Init(bool previewMode, bool graphicsLab = false);
	static void Uninit();
	static void Update(float frameSeconds);
	static float animationDeltaSeconds() { return m_animationDeltaSeconds; }
	static void Draw();
    static bool beginEditorPlay();
    static void endEditorPlay();
    static void requestHitStop(float seconds); // ŽÀŽžŠÔ‚Å•œ‹A‚·‚é’Z‚¢’âŽ~

	template<typename T>
	static void ChangeScene(float time)
	{
		if (m_NextScene != nullptr || !m_EditObjects.empty())
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