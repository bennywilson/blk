/// game.h
///
/// 2016 blk
#pragma once

#include "blk_console.h"
#include "camera.h"
#include "entity_header.h"
#include "sound_manager.h"

/// Game
class Game : public CommandProcessor {
public:
	Game();
	virtual	~Game();

	void Update();

	// Editor user
	void InitGame(HWND hwnd, const int width, const int height);
	void LoadMap(const std::string& mapName);
	void StopGame();

	bool IsPlaying() const { return m_bIsPlaying; }
	bool IsRunning() const { return m_bIsRunning; }
	void RequestQuitGame();

	const std::string& GetMapName() const { return m_MapName; }

	GameEntity* GetLocalPlayer() const { return m_pLocalPlayer; }

	const std::vector<GameEntity*>& GetGameEntities() const { return m_GameEntityList; }
	const std::vector<GameEntity*>& GetPlayersList() const { return m_GamePlayersList; }

	virtual GameEntity* CreatePlayer(const int netId, const Guid& prefabGUID, const Vec3& desiredLocation) = 0;
	GameEntity* CreateEntity(const GameEntity* const pPrefab, const bool bIsPlayer = false);
	void RemoveGameEntity(GameEntity* const pNewEntity);

	GameEntityPtr GetEntityByName(const String name);

	SoundManager& GetSoundManager() { return m_SoundManager; }

	bool ProcessCommand(const std::string& command);

	void SetDeltaTimeScale(const float newScale) { m_DeltaTimeScale = newScale; }

	float GetFrameDT() const { return m_CurFrameDeltaTime; }

	bool HasFirstSyncCompleted() const { return m_bHasFirstSyncCompleted; }

	// Hacks to get PIE style functionality
	virtual void HackEditorInit(HWND hwnd, std::vector<class EditorEntity*>& editorEntities) { }
	virtual void HackEditorUpdate(const float DT, Camera* const pCamera) { };
	virtual void HackEditorShutdown() { }

	template<typename T>
	T* GetLevelComponent() const {
		blk::error_check(m_pLevelComp != nullptr && m_pLevelComp->IsA(T::GetType()), "GetLevelComponent<T>() - Incorrect level component type");
		return (T*)m_pLevelComp;
	}

protected:
	virtual void init_internal() = 0;
	virtual void play_internal() = 0;
	virtual void stop_internal() = 0;
	virtual void preupdate_internal() { };
	virtual void postupdate_internal() { };
	virtual void level_loaded_internal() = 0;
	virtual void add_entity_internal(GameEntity* const pEntity) = 0;
	virtual void remove_entity_internal(GameEntity* const pEntity) = 0;

	const Input_t& get_input() const { return m_InputManager.get_input(); }
	bool is_console_active() const { return m_Console.IsActive(); }

	virtual void swap_entities_by_idx(const size_t idx1, const size_t idx2);

private:
	void display_debug_commands();

protected:
	HWND m_Hwnd;
	GameEntity* m_pLocalPlayer;
	Timer m_Timer;

	InputManager m_InputManager;
	SoundManager m_SoundManager;

private:
	std::string	m_MapName;
	LevelComponent* m_pLevelComp;

	std::vector<GameEntity*> m_GameEntityList;
	std::vector<GameEntity*> m_GamePlayersList;

	// List of entities to remove during the next Game::Update() call
	std::vector<GameEntity*> m_RemoveEntityList;

	Console m_Console;

	float m_DeltaTimeScale;
	float m_CurFrameDeltaTime;

	bool m_bIsPlaying;
	bool m_bIsRunning;
	bool m_bQuitGameRequested;
	bool m_bHasFirstSyncCompleted;
};

extern Game* g_pGame;
