/// kbCore.cpp
///
/// 2016 blk


#include <combaseapi.h>
#include <iostream>
#include <cstdarg>
#include "blk_core.h"
#include "job_manager.h"

FILE* g_LogFile = nullptr;
bool g_UseEditor = false;
kbOutputCB* g_OutputCB = nullptr;

std::string g_AdjustedBuffer;
HANDLE g_WriteFileMutex = nullptr;

char* g_FinalBuffer = nullptr;
int g_FinalBufferLength = 0;

kbOutputMessageType_t g_MessageType;
kbTimer g_GlobalTimer;

/// write_to_file
void write_to_file(const char* const msg, va_list arguments) {
	DWORD dwWaitResult = WaitForSingleObject(g_WriteFileMutex, INFINITE);

	g_AdjustedBuffer = msg;
	g_AdjustedBuffer += "\n";

	const int finalStringLength = _vscprintf(msg, arguments) + 2;

	if (!g_FinalBuffer || g_FinalBufferLength < finalStringLength) {
		g_FinalBufferLength = finalStringLength;
		g_FinalBuffer = new char[g_FinalBufferLength];
	}

	vsprintf_s(g_FinalBuffer, finalStringLength, g_AdjustedBuffer.c_str(), arguments);

	// Check incase logging happens before initialize_engine() or opening the
	// log file throws an error.
	if (g_LogFile) {
		fwrite(g_FinalBuffer, sizeof(char), finalStringLength, g_LogFile);
		fflush(g_LogFile);	// flush every line so the log survives a crash (abort() doesn't run atexit flushing)
	}

	if (g_OutputCB) {
		g_OutputCB(g_MessageType, g_FinalBuffer);
	}

	OutputDebugString(g_FinalBuffer);
	std::cout << g_FinalBuffer;
	ReleaseMutex(g_WriteFileMutex);
}

/// blk
namespace blk {
	// va_list-taking implementations. A va_list can't be forwarded through a
	// "..." parameter -- passing one as a vararg just copies its pointer
	// value, so the callee's va_start reads that pointer as the first format
	// argument instead of walking the original arguments. These exist so the
	// HRESULT overloads below can hand their captured va_list to the bool
	// overloads' logic directly, the same way vprintf relates to printf.
	static bool warn_check_v(const bool expression, const char* const msg, va_list args) {
		if (expression) {
			return true;
		}

		g_MessageType = Message_Warning;
		if (msg) {
			write_to_file(msg, args);
		} else {
			write_to_file("Warning - No msg supplied", args);
		}

		return false;
	}

	static bool error_check_v(const bool expression, const char* const msg, va_list args) {
		if (expression) {
			return true;
		}

		if (msg) {
			write_to_file(msg, args);
		} else {
			write_to_file("Error - No msg supplied", args);
		}

		DebugBreak();
		throw g_FinalBuffer;

		return false;
	}

	/// saved_path
	std::string saved_path(const char* const relative) {
		std::string path = "saved";
		CreateDirectoryA(path.c_str(), nullptr);

		// CreateDirectoryA only creates the leaf, so walk the segments and
		// create each one along the way -- "logs/logfile.txt" needs
		// "saved/logs" to exist before the caller can open the file.
		const char* segment = relative;
		for (const char* slash = strchr(segment, '/'); slash != nullptr; slash = strchr(segment, '/')) {
			path += "/";
			path.append(segment, slash - segment);
			CreateDirectoryA(path.c_str(), nullptr);
			segment = slash + 1;
		}

		path += "/";
		path += segment;

		return path;
	}

	/// initialize_engine
	void initialize_engine(char* const logName) {
		error_check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

		g_GlobalTimer.Reset();
		g_WriteFileMutex = CreateMutex(nullptr, FALSE, nullptr);

		// todo: Path may not support standalone builds
		SetCurrentDirectory("../");

		const std::string logPath = logName ? saved_path((std::string("logs/") + logName).c_str()) : saved_path("logs/logfile.txt");
		fopen_s(&g_LogFile, logPath.c_str(), "w");

		if (!g_LogFile) {
			// fopen_s fails if another instance of this app is open, so use
			// attempt to open logfile2.txt instead
			const std::string altLogPath = saved_path("logs/logfile2.txt");
			fopen_s(&g_LogFile, altLogPath.c_str(), "w");

			if (!g_LogFile) {
				const std::string message = "Failed to create the log file (tried " + logPath + " and " + altLogPath + "). Logging will not be available this session.";
				MessageBoxA(nullptr, message.c_str(), "blk engine - log file error", MB_OK | MB_ICONWARNING);
			}

			blk::error_check(g_LogFile, "InitializeKBEngine() - Cannot create log file");
		}

		blk::log("Initializing kbCore");

		g_pJobManager = new kbJobManager;
		blk::log("kbCore Initialized");
	}

	/// shutdown_engine
	void shutdown_engine() {
		blk::log("Shutting down kbCore...");

		delete g_pJobManager;
		g_pJobManager = nullptr;

		blk::log("kbCore Shutdown");

		fclose(g_LogFile);
		g_LogFile = nullptr;

		CloseHandle(g_WriteFileMutex);

		kbString::ShutDown();
	}

	/// warn
	void warn(const char* const msg, ...) {
		g_MessageType = Message_Warning;

		va_list args;
		va_start(args, msg);
		write_to_file(msg, args);
		va_end(args);
	}

	/// warn_check
	bool warn_check(const bool expression, const char* const msg, ...) {
		va_list args;
		va_start(args, msg);
		const bool ret = warn_check_v(expression, msg, args);
		va_end(args);

		return ret;
	}

	/// warn_check
	bool warn_check(const HRESULT hr, const char* const msg, ...) {
		va_list args;
		va_start(args, msg);
		const bool ret = warn_check_v(!FAILED(hr), msg, args);
		va_end(args);

		return ret;
	}

	/// error
	void error(const char* const msg, ...) {
		g_MessageType = Message_Error;

		va_list args;
		va_start(args, msg);
		write_to_file(msg, args);
		va_end(args);

		DebugBreak();
		throw g_FinalBuffer;
	}

	/// error_check
	bool error_check(const bool expression, const char* const msg, ...) {
		va_list args;
		va_start(args, msg);
		const bool ret = error_check_v(expression, msg, args);
		va_end(args);

		return ret;
	}

	// error_check
	bool error_check(const HRESULT hr, const char* const msg, ...) {
		va_list args;
		va_start(args, msg);
		const bool ret = error_check_v(!FAILED(hr), msg, args);
		va_end(args);

		return ret;
	}
	/// log
	void log(const char* const msg, ...) {
		g_MessageType = Message_Normal;

		va_list args;
		va_start(args, msg);
		write_to_file(msg, args);
		va_end(args);
	}
}

/// StringFromWString
#include <locale>
#include <codecvt>
#include <string>
void StringFromWString(std::string& outString, const std::wstring& srcString) {
	outString = WideCharToMultiByte(CP_ACP,
		0,
		srcString.c_str(),
		-1,
		NULL,
		0, NULL, NULL);
}

/// WStringFromString
void WStringFromString(std::wstring& outString, const std::string& srcString) {
	outString = std::wstring(srcString.begin(), srcString.end());
}

/// StringToLower
void StringToLower(std::string& outString) {
	std::transform(outString.begin(), outString.end(), outString.begin(), ::tolower);
}

/// GetFileExtension
std::string GetFileExtension(const std::string& FileName) {
	std::size_t found = FileName.find_last_of(".");
	if (found != std::string::npos) {
		return FileName.substr(found + 1);
	}

	return "";
}

/// GetFileExtension
std::wstring GetFileExtension(const std::wstring& FileName) {
	std::size_t found = FileName.find_last_of(L".");
	if (found != std::wstring::npos) {
		return FileName.substr(found + 1);
	}

	return L"";
}


std::map<ScopedTimerList_t, struct kbScopedTimerData_t*> g_ScopedTimerMap;

/// kbScopedTimerData_t::kbScopedTimerData_t
kbScopedTimerData_t::kbScopedTimerData_t(const ScopedTimerList_t timerIdx, const char* const stringName) {
	m_ReadableName = kbString(stringName);
	memset(&m_FrameTimes, 0, sizeof(m_FrameTimes));
	m_FrameTimeIdx = 0;

	g_ScopedTimerMap[timerIdx] = this;
}

/// kbScopedTimerData_t::GetFrameTime
float kbScopedTimerData_t::GetFrameTime() const {
	float totalMS = 0.0f;
	for (int i = 0; i < NUM_FRAME_TIMES; i++) {
		totalMS += m_FrameTimes[i];
	}

	return totalMS / NUM_FRAME_TIMES;
}

#define DECLARE_SCOPED_TIMER(Index, String) \
	kbScopedTimerData_t Index##Var(Index, String); \

DECLARE_SCOPED_TIMER(GAME_THREAD, "Game Thread")
DECLARE_SCOPED_TIMER(GAME_ENTITY_UPDATE, "   Entity Update")
DECLARE_SCOPED_TIMER(COMPONENT_UPDATE, "      Component Update")
DECLARE_SCOPED_TIMER(CLOTH_COMPONENT, "         Cloth Component")
DECLARE_SCOPED_TIMER(GAME_THREAD_IDLE, "   Game Thread Idle")
DECLARE_SCOPED_TIMER(RENDER_THREAD, "Render Thread")
DECLARE_SCOPED_TIMER(RENDER_THREAD_CLEAR_BUFFERS, "   Clear Buffers")
DECLARE_SCOPED_TIMER(RENDER_G_BUFFER, "   Render G-Buffer")
DECLARE_SCOPED_TIMER(RENDER_LIGHTING, "   Render Lighting")
DECLARE_SCOPED_TIMER(RENDER_SHADOW_DEPTH, "      Render Shadow Depth")
DECLARE_SCOPED_TIMER(RENDER_LIGHT, "      Render Lighting")
DECLARE_SCOPED_TIMER(RENDER_UNLIT, "      Render Unlit")
DECLARE_SCOPED_TIMER(RENDER_TRANSLUCENCY, "   Render Translucency")
DECLARE_SCOPED_TIMER(RENDER_LIGHTSHAFTS, "   Render Light Shafts")
DECLARE_SCOPED_TIMER(RENDER_POST_PROCESS, "   Render Post-Process")
DECLARE_SCOPED_TIMER(RENDER_TEXT, "   Render Text")
DECLARE_SCOPED_TIMER(RENDER_PRESENT, "   Present")
DECLARE_SCOPED_TIMER(RENDER_SYNC, "   Render Sync")
DECLARE_SCOPED_TIMER(RENDER_SYNC_PARTICLES, "   Render Sync Particles")
DECLARE_SCOPED_TIMER(RENDER_GPUTIMER_STALL, "GPU Timer Stall")
DECLARE_SCOPED_TIMER(RENDER_DEBUG, "Debug Rendering")
DECLARE_SCOPED_TIMER(RENDER_ENTITYID, "EntityId Rendering")
DECLARE_SCOPED_TIMER(TEMP_1, "Temp 1")
DECLARE_SCOPED_TIMER(TEMP_2, "Temp 2")
DECLARE_SCOPED_TIMER(TEMP_3, "Temp 3")
DECLARE_SCOPED_TIMER(TEMP_4, "Temp 4")
DECLARE_SCOPED_TIMER(TEMP_5, "Temp 5")
DECLARE_SCOPED_TIMER(TEMP_6, "Temp 6")
DECLARE_SCOPED_TIMER(TEMP_7, "Temp 7")
DECLARE_SCOPED_TIMER(TEMP_8, "Temp 8")
DECLARE_SCOPED_TIMER(TEMP_9, "Temp 9")
DECLARE_SCOPED_TIMER(TEMP_10, "Temp 10")

/// kbScopedTimer::kbScopedTimer
kbScopedTimer::kbScopedTimer(ScopedTimerList_t index) :
	m_TimerIndex(index) {
}

/// kbScopedTimer::~kbScopedTimer
kbScopedTimer::~kbScopedTimer() {

	kbScopedTimerData_t* const timerData = g_ScopedTimerMap[m_TimerIndex];
	timerData->m_FrameTimes[timerData->m_FrameTimeIdx] += m_Timer.TimeElapsedMS();
}

/// kbScopedTimer::UpdateScopedTimers
void UpdateScopedTimers() {

	for (int i = 0; i < MAX_NUM_SCOPED_TIMERS; i++) {
		kbScopedTimerData_t* const timerData = g_ScopedTimerMap[(ScopedTimerList_t)i];

		if (timerData == nullptr) {
			blk::error("Scoped timer at index %d is uninitialized", i);
		}

		timerData->m_FrameTimeIdx++;
		if (timerData->m_FrameTimeIdx >= kbScopedTimerData_t::NUM_FRAME_TIMES) {
			timerData->m_FrameTimeIdx = 0;
		}
		timerData->m_FrameTimes[timerData->m_FrameTimeIdx] = 0.0f;
	}
}

/// kbScopedTimer::GetScopedTimerData
const kbScopedTimerData_t& GetScopedTimerData(const ScopedTimerList_t index) {
	return *g_ScopedTimerMap[index];
}
