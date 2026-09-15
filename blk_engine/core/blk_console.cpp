/// blk_console.cpp
///
/// 2016 blk

#include <fstream>
#include "blk_console.h"

/// ConsoleVarManager::Initialize
void ConsoleVariable::Initialize() {
	if (m_InputKeys.size() > 0) {
		g_pInputManager->MapKeysToCallback(m_InputKeys, this, 0, GetDescription() + ".  Also a CVar");
	}
}

/// ConsoleVarManager::InputKeyPressedCB
void ConsoleVariable::InputKeyPressedCB(const int cbParam) {
	if (m_VarType == Console_Bool) {
		m_CurrentVal.m_bValue = !m_CurrentVal.m_bValue;
	}
}

/// ConsoleVarManager::GetInputCBName
const char* ConsoleVariable::GetInputCBName() const {
	return "Console variable";
}

/// ConsoleVarManager::GetConsoleVarManager
static ConsoleVarManager* g_pConsoleVarManager = nullptr;
ConsoleVarManager* ConsoleVarManager::GetConsoleVarManager() {
	if (g_pConsoleVarManager == nullptr) {
		g_pConsoleVarManager = new ConsoleVarManager();
	}

	return g_pConsoleVarManager;
}

/// ConsoleVarManager::DeleteConsoleVarManager
void ConsoleVarManager::DeleteConsoleVarManager() {
	delete g_pConsoleVarManager;
	g_pConsoleVarManager = nullptr;
}

/// ConsoleVarManager::GetConsoleVar
ConsoleVariable* ConsoleVarManager::GetConsoleVar(const String& variableName) {
	ConsoleVarManager* const pConsoleVarMgr = ConsoleVarManager::GetConsoleVarManager();

	std::map<String, ConsoleVariable*>::iterator it = pConsoleVarMgr->m_ConsoleVarMap.find(variableName);
	if (it == pConsoleVarMgr->m_ConsoleVarMap.end()) {
		return NULL;
	}

	return (*it).second;
}

/// ConsoleVarManager::Initialize
void ConsoleVarManager::Initialize() {
	std::map<String, ConsoleVariable*>::iterator it = m_ConsoleVarMap.begin();
	while (it != m_ConsoleVarMap.end()) {
		it->second->Initialize();
		++it;
	}
}

/// ConsoleVarManager::Update
void ConsoleVarManager::Update() {}

/// Console::Console
const int StartingCommandHistoryIdx = -999;
Console::Console() :
	m_TimeSinceLastUpdate(0.0f),
	m_CommandHistoryIdx(0),
	m_bIsActive(false) {

	std::fstream commandHistoryFile;
	commandHistoryFile.open(blk::saved_path("config/commandHistory.txt"), std::fstream::in);
	if (commandHistoryFile.is_open()) {
		commandHistoryFile.seekg(0, commandHistoryFile.end);
		const size_t length = commandHistoryFile.tellg();
		commandHistoryFile.seekg(0, commandHistoryFile.beg);

		char* buffer = new char[length];
		commandHistoryFile.read(buffer, length);
		std::string sBuffer = buffer;
		delete[] buffer;

		size_t endString = sBuffer.find_first_of("\n", 0);
		size_t curString = 0;

		while (endString != std::string::npos) {
			std::string command = sBuffer.substr(curString, endString - curString);
			m_CommandHistory.push_back(command);

			curString = endString + 1;
			endString = sBuffer.find_first_of("\n", curString);
		}

		m_CommandHistoryIdx = StartingCommandHistoryIdx;
	}

	commandHistoryFile.close();
}

/// Console::~Console
Console::~Console() {
	std::fstream commandHistoryFile;
	commandHistoryFile.open(blk::saved_path("config/commandHistory.txt"), std::fstream::out);
	if (commandHistoryFile.is_open()) {
		for (int i = 0; i < m_CommandHistory.size(); i++) {
			commandHistoryFile.write(m_CommandHistory[i].c_str(), m_CommandHistory[i].length());
			commandHistoryFile.write("\n", 1);
		}
	}
}

/// Console::SetActive
void Console::SetActive(const bool bIsActive) {
	m_bIsActive = bIsActive;
}

/// Console::Update
void Console::Update(const float DT, const Input_t& Input) {

	if (Input.KeyState[192].m_Action == Input_t::KA_JustPressed) {
		m_bIsActive = !m_bIsActive;
	}

	if (m_bIsActive == false) {
		return;
	}

	m_TimeSinceLastUpdate = 0.0f;
	// DEL = 46
	// Enter = 13
	const float curTimeSec = g_GlobalTimer.TimeElapsedSeconds();
	const float minTimeBetweenPresses = 1.0f;

	for (int i = 0; i < 256; i++) {
		if (Input.KeyState[i].m_Action == Input_t::KA_JustPressed || (Input.KeyState[i].m_Action == Input_t::KA_Down && curTimeSec > Input.KeyState[i].m_LastActionTimeSec + minTimeBetweenPresses)) {
			if (i >= 65 && i <= 90) {      // A through Z ----------------------------------------------------------- */
				m_CurrentCommand += (char)i + 32;
			} else if (i == 46 || i == 8) {    // Del Pressed ----------------------------------------------------------- */
				if (m_CurrentCommand.size() > 0) {
					m_CurrentCommand.pop_back();
				}
			} else if (i == 32) {       // Space Pressed -------------------------------------------------------- */
				m_CurrentCommand += " ";
			} else if (i == 13) {       // Enter Pressed -------------------------------------------------------- */
				for (int icmd = 0; icmd < m_CommandProcessors.size(); icmd++) {
					m_CommandProcessors[icmd]->ProcessCommand(m_CurrentCommand);
				}

				if (m_CurrentCommand.length() > 0 && m_CurrentCommand != "exit") {

					if (m_CommandHistory.size() == MaxCommandHistoryEntries) {
						m_CommandHistory.erase(m_CommandHistory.begin());
					}

					m_CommandHistory.push_back(m_CurrentCommand);
					m_CommandHistoryIdx = (int)m_CommandHistory.size();
				}

				m_CurrentCommand.clear();
			} else if (i >= 48 && i <= 57) {    // 0 - 9 Pressed -------------------------------------------------------- */
				m_CurrentCommand += std::to_string(i - 48);
			} else if (i == VK_UP) {
				if (m_CommandHistory.size() > 0) {
					if (m_CommandHistoryIdx == StartingCommandHistoryIdx) {
						m_CommandHistoryIdx = (int)m_CommandHistory.size() - 1;
					} else {
						m_CommandHistoryIdx--;
						if (m_CommandHistoryIdx < 0) {
							m_CommandHistoryIdx = (int)m_CommandHistory.size() - 1;
						}
					}
					m_CurrentCommand = m_CommandHistory[m_CommandHistoryIdx].c_str();
				}
			} else if (i == VK_DOWN) {     // Down Arrow ----------------------------------------------------------- */
				if (m_CommandHistory.size() > 0) {
					if (m_CommandHistoryIdx == StartingCommandHistoryIdx) {
						m_CommandHistoryIdx = (int)m_CommandHistory.size() - 1;
					} else {
						m_CommandHistoryIdx++;
						if (m_CommandHistoryIdx >= m_CommandHistory.size()) {
							m_CommandHistoryIdx = 0;
						}
					}
					m_CurrentCommand = m_CommandHistory[m_CommandHistoryIdx].c_str();
				}
			} else if (i == 190) {      // Period -------------------------------------------------------------- */
				m_CurrentCommand += ".";
			} else if (i == VK_OEM_MINUS) {    // Minus --------------------------------------------------------------- */
				m_CurrentCommand += "-";
			}
		}
	}
}
