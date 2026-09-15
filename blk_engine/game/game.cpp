/// Game.cpp
///
///
/// 2016 blk

#include "game.h"
#include "file.h"

Game* g_pGame = nullptr;

ConsoleVariable g_ShowPerfTimers("showperftimers", false, ConsoleVariable::Console_Bool, "Display game/engine perf timers", "ctrl p");
ConsoleVariable g_ShowEntityInfo("showentityinfo", false, ConsoleVariable::Console_Bool, "Show entity info?", "");
ConsoleVariable g_DumpEntityInfo("dumpentityinfo", false, ConsoleVariable::Console_Bool, "Dump entity info?", "");
ConsoleVariable g_TimeScale("timescale", (float)1.0f, ConsoleVariable::Console_Float, "Dilate time", "");
ConsoleVariable g_EnableHelpScreen("help", false, ConsoleVariable::Console_Bool, "Display help screen", "ctrl h");
ConsoleVariable g_ShowFPS("showfps", false, ConsoleVariable::Console_Bool, "Show FPS", "ctrl f");

/// Game::Game
Game::Game() :
	m_Hwnd(nullptr),
	m_pLocalPlayer(nullptr),
	m_pLevelComp(nullptr),
	m_DeltaTimeScale(1.0f),
	m_CurFrameDeltaTime(0.0f),

	m_bIsPlaying(false),
	m_bIsRunning(true),
	m_bQuitGameRequested(false),
	m_bHasFirstSyncCompleted(false) {

	g_pGame = this;
	m_Console.RegisterCommandProcessor(this);
}

/// Game::~Game
Game::~Game() {
	m_Console.RemoveCommandProcessor(this);
}

///  *  Game::InitGame
void Game::InitGame(HWND hwnd, const int backBufferWidth, const int backBufferHeight) {

	m_Hwnd = hwnd;

	m_InputManager.Init(m_Hwnd);
	ConsoleVarManager::GetConsoleVarManager()->Initialize();

	init_internal();
}

///  *  Game::LoadMap
void Game::LoadMap(const std::string& mapName) {
	blk::log("LoadMap() called on %s", mapName.c_str());

	m_pLevelComp = nullptr;

	// Load map
	if (mapName.empty() == false) {

		TCHAR NPath[MAX_PATH];

		GetCurrentDirectory(MAX_PATH, NPath);

		WIN32_FIND_DATA fdFile;
		HANDLE hFind = nullptr;

		std::string LevelPath = NPath;
		LevelPath += "/Assets/Levels/";
		std::string curLevelFolder = "";

		// Bare map names try .blklevel first, then the legacy .kblevel.
		m_MapName = mapName;
		std::string legacyMapName;
		if (m_MapName.find(".") == std::string::npos) {
			legacyMapName = m_MapName + ".kblevel";
			m_MapName += ".blklevel";
		}

		hFind = FindFirstFile((LevelPath + "*").c_str(), &fdFile);
		BOOL nextFileFound = (hFind != INVALID_HANDLE_VALUE);
		do {
			const std::string fullFilePath = LevelPath + curLevelFolder + m_MapName;

			File inFile;
			bool bOpened = inFile.Open(fullFilePath.c_str(), File::FT_Read);
			if (!bOpened && !legacyMapName.empty() && inFile.Open((LevelPath + curLevelFolder + legacyMapName).c_str(), File::FT_Read)) {
				m_MapName = legacyMapName;
				bOpened = true;
			}
			if (bOpened) {

				StopGame();

				for (int i = 0; i < m_GameEntityList.size(); i++) {
					m_GameEntityList[i]->render_sync();
				}

				//m_/ParticleManager.render_sync();

				GameEntity* gameEntity = inFile.ReadGameEntity();
				while (gameEntity != nullptr) {

					if (m_pLevelComp == nullptr) {
						m_pLevelComp = gameEntity->component<LevelComponent>();
					}
					m_GameEntityList.push_back(gameEntity);
					gameEntity = inFile.ReadGameEntity();
				}
				inFile.Close();

				level_loaded_internal();

				m_Timer.Reset();
				play_internal();
				m_bIsPlaying = true;

				break;
			}

			if (nextFileFound == FALSE) {
				break;
			}

			do {
				if ((fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && strcmp(fdFile.cFileName, ".") != 0 && strcmp(fdFile.cFileName, "..") != 0) {
					curLevelFolder = "/";
					curLevelFolder += fdFile.cFileName;
					curLevelFolder += "/";
					break;
				}

			} while (nextFileFound = FindNextFile(hFind, &fdFile) != FALSE);

			if (nextFileFound != false) {
				nextFileFound = FindNextFile(hFind, &fdFile);
			}
		} while (true);
	}
}

///  *  Game::StopGame
void Game::StopGame() {
	stop_internal();


	m_bIsPlaying = false;

	for (int i = 0; i < m_GameEntityList.size(); i++) {
		delete m_GameEntityList[i];
	}
	m_GameEntityList.clear();
	m_GamePlayersList.clear();

	m_pLevelComp = nullptr;
}

///  *  Game::RequestQuitGame
void Game::RequestQuitGame() {

	m_bQuitGameRequested = true;
}

/// Game::Update
void Game::Update() {

	START_SCOPED_TIMER(GAME_THREAD);

	m_CurFrameDeltaTime = m_Timer.TimeElapsedSeconds() * m_DeltaTimeScale;
	m_Timer.Reset();

	static int NumFrames = 0;
	static float StartTime = g_GlobalTimer.TimeElapsedSeconds();

	NumFrames++;
	static float FPS = 0;

	if (NumFrames > 100) {
		const float curTime = g_GlobalTimer.TimeElapsedSeconds();
		FPS = (float)NumFrames / (curTime - StartTime);
		NumFrames = 0;
		StartTime = curTime;
	}

	if (g_TimeScale.GetFloat() > 0.0f) {
		m_CurFrameDeltaTime *= g_TimeScale.GetFloat();
	}

	m_InputManager.Update(m_CurFrameDeltaTime);
	m_SoundManager.Update();

	preupdate_internal();

	for (int i = 0; i < m_GameEntityList.size(); i++) {
		m_GameEntityList[i]->update(m_CurFrameDeltaTime);
	}

	postupdate_internal();

/*	if (g_pRenderer != nullptr) {

		const float fontHeight = (16.0f) / g_pRenderer->GetBackBufferHeight();

		g_pRenderer->EnableConsole(false);

		if (m_Console.IsActive()) {
			g_pRenderer->EnableConsole(true);
			g_pRenderer->DrawDebugText(m_Console.GetCurrentCommandString().c_str() + std::string("_"), 0, 0.75f - fontHeight, 0.0125f, 0.0125f, Color::green);

			if (g_EnableHelpScreen.GetBool()) {
				display_debug_commands();
			}
		}
		g_pRenderer->SetRenderWindow(m_Hwnd);

		{
			START_SCOPED_TIMER(GAME_THREAD_IDLE);

			// Wait for rendering to complete, sync up any game objects that need it and kick off a new scene to render
			g_pRenderer->WaitForRenderingToComplete();
		}

		{
			START_SCOPED_TIMER(RENDER_SYNC);

			for (int i = 0; i < m_GameEntityList.size(); i++) {
				m_GameEntityList[i]->render_sync();
			}

			//m_ParticleManager.render_sync();
			g_pRenderer->render_sync();

			g_ResourceManager.render_sync();

			for (int i = 0; i < m_RemoveEntityList.size(); i++) {
				std::vector<GameEntity*>::iterator it;
				it = find(m_GameEntityList.begin(), m_GameEntityList.end(), m_RemoveEntityList[i]);

				if (it != m_GameEntityList.end()) {
					const int index = (int)(it - m_GameEntityList.begin());
					std::swap(m_GameEntityList[index], m_GameEntityList.back());
					m_GameEntityList.pop_back();
					delete m_RemoveEntityList[i];
				}
			}
		}

		if (g_ShowFPS.GetBool()) {
			std::string fpsString = "FPS: ";
			std::stringstream stream;
			stream << std::fixed << std::setprecision(2) << FPS;
			fpsString += stream.str();
			g_pRenderer->DrawDebugText(fpsString, 0.85f, 0, g_DebugTextSize, g_DebugTextSize, Color::green);
		}
		if (g_ShowPerfTimers.GetBool()) {

			float curY = g_DebugLineSpacing + 0.1f;
			for (int i = 0; i < (int)MAX_NUM_SCOPED_TIMERS; i++, curY += g_DebugLineSpacing) {
				const ScopedTimerData_t& timingData = GetScopedTimerData((ScopedTimerList_t)i);
				std::string timing = timingData.m_ReadableName.stl_str();
				timing += ": ";

				std::stringstream stream;
				stream << std::fixed << std::setprecision(3) << timingData.GetFrameTime();
				timing += stream.str();

				g_pRenderer->DrawDebugText(timing, 0.15f, curY, g_DebugTextSize, g_DebugTextSize, Color::green);
			}
		} else if (g_ShowEntityInfo.GetBool() || g_DumpEntityInfo.GetBool()) {
			float curY = g_DebugLineSpacing + 0.1f;
			std::string NumEntities = "Num Entities: ";
			NumEntities += std::to_string((long long)m_GameEntityList.size());
			g_pRenderer->DrawDebugText(NumEntities, 0.25f, curY, g_DebugTextSize, g_DebugTextSize, Color::green);
			curY += g_DebugLineSpacing;
			std::map<std::string, int> componentMap;

			if (g_DumpEntityInfo.GetBool()) {
				blk::log("=============================================================================");
				blk::log("Dumping entity and component info");

			}

			for (int i = 0; i < (int)m_GameEntityList.size(); i++) {
				const GameEntity* const pCurEntity = m_GameEntityList[i];

				if (g_DumpEntityInfo.GetBool()) {
					blk::log("Entity [%d] - %s", i, pCurEntity->GetName().c_str());
				}

				for (int iComp = 0; iComp < pCurEntity->num_components(); iComp++) {
					Component* const pComponent = pCurEntity->component(iComp);
					const std::string pComponentTypeName = pComponent->GetComponentClassName();
					componentMap[pComponentTypeName]++;

					if (g_DumpEntityInfo.GetBool()) {
						TransformComponent* pTransformComponent = pComponent->GetAs<TransformComponent>();
						if (pTransformComponent != nullptr) {
							blk::log("	Component [%d] - %s %s", iComp, pComponentTypeName.c_str(), pTransformComponent->GetName().c_str());
						} else {
							blk::log("	Component [%d] - %s", iComp, pComponentTypeName.c_str());
						}
					}

				}
			}

			g_DumpEntityInfo.SetBool(false);

			for (std::map<std::string, int>::iterator it = componentMap.begin(); it != componentMap.end(); ++it) {
				std::string outputName = it->first;
				outputName += ": ";
				outputName += std::to_string((long long)it->second);
				g_pRenderer->DrawDebugText(outputName, 0.25f, curY, g_DebugTextSize, g_DebugTextSize, Color::green);
				curY += g_DebugLineSpacing;

			}
		}

		UpdateScopedTimers();
		m_bHasFirstSyncCompleted = true;
		g_pRenderer->SetReadyToRender();

		m_RemoveEntityList.clear();
	} else {

		for (int i = 0; i < m_RemoveEntityList.size(); i++) {
			std::vector<GameEntity*>::iterator it;
			it = find(m_GameEntityList.begin(), m_GameEntityList.end(), m_RemoveEntityList[i]);

			if (it != m_GameEntityList.end()) {
				const int index = (int)(it - m_GameEntityList.begin());
				std::swap(m_GameEntityList[index], m_GameEntityList.back());
				m_GameEntityList.pop_back();
				delete m_RemoveEntityList[i];
			}
		}

		m_RemoveEntityList.clear();
	}*/

	ConsoleVarManager::GetConsoleVarManager()->Update();
	m_Console.Update(m_CurFrameDeltaTime, m_InputManager.get_input());

	if (m_bQuitGameRequested) {
		StopGame();
		m_bIsRunning = false;
	}
}

/// Game::CreateEntity
GameEntity* Game::CreateEntity(const GameEntity* const pPrefab, const bool bIsPlayer) {
	if (pPrefab == nullptr) {
		blk::error("Game::CreateEntity() - nullptr prefab passed in");
		return nullptr;
	}

	GameEntity* const pSpawnedEntity = new GameEntity(pPrefab, false, nullptr);
	m_GameEntityList.push_back(pSpawnedEntity);

	if (bIsPlayer) {
		m_GamePlayersList.push_back(pSpawnedEntity);
	}

	add_entity_internal(pSpawnedEntity);

	return pSpawnedEntity;
}

/// Game::RemoveGameEntity
void Game::RemoveGameEntity(GameEntity* const pEntityToRemove) {

	remove_entity_internal(pEntityToRemove);

	std::vector<GameEntity*>::iterator it;
	it = find(m_GameEntityList.begin(), m_GameEntityList.end(), pEntityToRemove);

	if (it != m_GameEntityList.end()) {
		pEntityToRemove->disable_all_components();
		m_RemoveEntityList.push_back(pEntityToRemove);
	} else {
		it = find(m_GamePlayersList.begin(), m_GamePlayersList.end(), pEntityToRemove);
		if (it != m_GamePlayersList.end()) {
			pEntityToRemove->disable_all_components();
			m_RemoveEntityList.push_back(pEntityToRemove);
		}
	}
}

/// Game::GetEntityByName
GameEntityPtr Game::GetEntityByName(const String entName) {

	// TODO - Optimize
	for (int i = 0; i < m_GameEntityList.size(); i++) {
		if (m_GameEntityList[i]->name() == entName) {
			GameEntityPtr retEntity;
			retEntity.SetEntity(m_GameEntityList[i]);
			return GameEntityPtr(retEntity);
		}
	}

	return GameEntityPtr();
}

/// Game::SwapEntitiesByIdx
void Game::swap_entities_by_idx(const size_t idx1, const size_t idx2) {
	if (idx1 < 0 || idx1 >= m_GameEntityList.size() || idx2 < 0 || idx2 >= m_GameEntityList.size()) {
		blk::warn("Game::swap_entities_by_idx() - Invalid index(es) [%d], [%d]", idx1, idx2);
		return;
	}

	std::swap(m_GameEntityList[idx1], m_GameEntityList[idx2]);
}

/// Game::ProcessCommand
bool Game::ProcessCommand(const std::string& InCommand) {

	std::string command = InCommand;
	std::transform(command.begin(), command.end(), command.begin(), ::tolower);

	// Get parameters
	std::vector<std::string> commandParams;

	size_t endString = command.find_first_of(" ", 0);
	std::string finalCommand = command.substr(0, endString);

	while (endString != std::string::npos) {
		std::string param = command.substr(endString + 1, command.size());
		if (param[0] != ' ') {
			std::transform(param.begin(), param.end(), param.begin(), ::tolower);
			commandParams.push_back(param);
		}

		endString = command.find_first_of(" ", endString + 1);
	}

	ConsoleVarManager* const pConsoleVarMgr = ConsoleVarManager::GetConsoleVarManager();
	ConsoleVariable* const pConsoleVar = pConsoleVarMgr->GetConsoleVar(String(finalCommand.c_str()));

	if (finalCommand == "help") {
		g_EnableHelpScreen.SetBool(!g_EnableHelpScreen.GetBool());
		return true;
	} else if (pConsoleVar == nullptr) {

		if (finalCommand == "open") {
			if (commandParams.size() > 0) {
				LoadMap(commandParams[0]);
			}
		} else if (finalCommand == "exit") {
			RequestQuitGame();
		}
		return true;
	}

	if (commandParams.size() == 0) {
		return false;
	}

	if (pConsoleVar->GetType() == ConsoleVariable::Console_Float) {
		const float val = std::stof(commandParams[0]);
		pConsoleVar->SetFloat(val);
	} else {

		int val = 0;

		if (commandParams[0].find_first_not_of("0123456789") == std::string::npos) {
			val = std::stoi(commandParams[0]);
		} else if (commandParams[0] == "true") {
			val = 1;
		} else {
			val = 0;
		}

		pConsoleVar->SetInt(val);
	}

	return true;
}

/// Game::display_debug_commands
void Game::display_debug_commands() {
/*	const float aspectRatio = (float)g_pRenderer->GetBackBufferWidth() / (float)g_pRenderer->GetBackBufferHeight();
	const float fontScreenSizeX = 0.02f;
	const float fontScreenSizeY = fontScreenSizeX * aspectRatio;
	const float spaceSize = fontScreenSizeY * 0.5f;

	g_pRenderer->DrawDebugText("Help Screen", 0.0f, 0.0f, fontScreenSizeX, fontScreenSizeY, Color::green);

	float curScreenY = fontScreenSizeY + spaceSize;

	g_pRenderer->DrawDebugText("CVars:", 0.0f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::red);
	curScreenY += spaceSize;

	auto consoleVarMap = ConsoleVarManager::GetConsoleVarManager()->GetConsoleVarMap();
	auto consoleVarIt = consoleVarMap.begin();

	while (consoleVarIt != consoleVarMap.end()) {
		const String consoleVarName = consoleVarIt->first;
		g_pRenderer->DrawDebugText(consoleVarName.stl_str() + ":", 0.0f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::green);
		g_pRenderer->DrawDebugText(consoleVarIt->second->GetDescription(), 0.25f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::red);

		curScreenY += spaceSize;
		consoleVarIt++;
	}

	curScreenY += spaceSize;
	g_pRenderer->DrawDebugText("Short-cut Keys:", 0.0f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::red);
	curScreenY += spaceSize;

	KeyComboMapType keyComboMap = g_pInputManager->GetKeyComboMap();
	KeyComboMapType::iterator it = keyComboMap.begin();

	while (it != keyComboMap.end()) {
		g_pRenderer->DrawDebugText(it->second.m_HelpDescription + ": ", 0.0f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::green);
		g_pRenderer->DrawDebugText(it->second.m_KeyComboDisplayString, 0.5f, curScreenY, fontScreenSizeX, fontScreenSizeY, Color::red);
		it++;
		curScreenY += spaceSize;
	}*/
}