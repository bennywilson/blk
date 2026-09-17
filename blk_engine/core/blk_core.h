/// blk_core.h
///
/// 2016 blk

#pragma once
#pragma warning(disable : 4482 4711)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
//#include <fstream>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <string>
#include "blk_string.h"

void StringFromWString(std::string& outString, const std::wstring& srcString);
void WStringFromString(std::wstring& outString, const std::string& srcString);
void StringToLower(std::string& outString);

std::string GetFileExtension(const std::string& FileName);
std::wstring GetFileExtension(const std::wstring& FileName);

/// Reflection markers, read by tools/type_info/generate_type_info.py, which regenerates
/// type_info_generated.h/.inl from them on every build. They expand to nothing.
///
/// BLK_PROPERTY() above a member of a BLK_DECLARE_COMPONENT class makes it saved and
/// editable. The name it's stored under is derived from the member name - m_min_spawn_rate
/// becomes MinSpawnRate - so renaming a member renames the key in every level file, and
/// the levels have to be migrated with it. The loader skips keys it doesn't recognize, so
/// a missed migration loses those values silently rather than failing.
///
/// SerializedAs pins a key that shouldn't follow its member - a member shadowing an
/// ancestor's needs one, since the loader takes the first match up the chain. MinVal/MaxVal
/// bound a FLOAT or INT property; the editor clamps to them as an edit is committed.
///
///     BLK_PROPERTY(SerializedAs = "MaterialList")
///     std::vector<MaterialComponent> m_materials;
///
///     BLK_PROPERTY(MinVal = 1)
///     int m_grassCellsPerTerrainSide;
///
/// An enum a property uses is reflected automatically. Its values are stored by name, with
/// the prefix its enumerators share stripped off, and matched back by position - so the
/// enumerator names AND their order are on-disk format too. Rename or reorder one and the
/// levels need migrating with it; an unmatched value silently loads as the first enumerator.
/// BLK_ENUM() marks an enumerator, written inline: SerializedAs pins its stored string, Skip
/// leaves out a trailing non-value such as NUM_RENDER_PASSES. The generator rejects any
/// specifier it doesn't know or act on.
#define BLK_PROPERTY(...)
#define BLK_ENUM(...)

/// TypeInfoType_t
enum TypeInfoType_t {
	BLK_TYPEINFO_NONE,
	BLK_TYPEINFO_BOOL,
	BLK_TYPEINFO_INT,
	BLK_TYPEINFO_FLOAT,
	BLK_TYPEINFO_STD_STRING,
	BLK_TYPEINFO_VECTOR,
	BLK_TYPEINFO_VECTOR4,
	BLK_TYPEINFO_PTR,
	BLK_TYPEINFO_TEXTURE,
	BLK_TYPEINFO_STATICMODEL,
	BLK_TYPEINFO_SOUNDWAVE,
	BLK_TYPEINFO_SHADER,
	BLK_TYPEINFO_ENUM,
	BLK_TYPEINFO_ANIMATION,
	BLK_TYPEINFO_STRING,
	BLK_TYPEINFO_STRUCT,
	BLK_TYPEINFO_GAMEENTITY,
};

typedef unsigned short ushort;
typedef unsigned int uint;
typedef float f32;
typedef double f64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int32_t i32;

///	Guid - Each GameEntity is given a GUID at construction that is saved out and referenced across multiple files
struct Guid {
	Guid() {
		m_iGuid[0] = m_iGuid[1] = m_iGuid[2] = m_iGuid[3] = 0;
	}

	union {
		GUID m_Guid;
		uint m_iGuid[4];
	};

	bool operator==(const Guid& rhs) const { return m_iGuid[0] == rhs.m_iGuid[0] && m_iGuid[1] == rhs.m_iGuid[1] && m_iGuid[2] == rhs.m_iGuid[2] && m_iGuid[3] == rhs.m_iGuid[3]; }
	bool IsValid() const { return m_iGuid[0] != 0 && m_iGuid[1] != 0 && m_iGuid[2] != 0 && m_iGuid[3] != 0; }
};

extern FILE* g_LogFile;
extern bool g_UseEditor;

enum OutputMessageType_t {
	Message_Normal,
	Message_Warning,
	Message_Assert,
	Message_Error,
};

typedef void(OutputCallback)(OutputMessageType_t, const char*);
extern OutputCallback* g_OutputCB;

namespace blk {
	/// Call `initialize_engine()` before any other blk functions
	void initialize_engine(char* const logName = nullptr);
	void shutdown_engine();

	/// Path under `saved/`, the root for everything the engine generates at
	/// runtime: logs, editor layout, command history, caches, etc. Creates each
	/// directory named in `relative` (forward slashes) so callers can open the
	/// returned path directly. The whole tree is safe to delete offline.
	std::string saved_path(const char* const relative);

	/// True for "blklevel" and the legacy "kbLevel"/"kblevel". Takes the extension without its dot.
	inline bool is_level_extension(const std::string& extension) {
		return extension == "blklevel" || extension == "kbLevel" || extension == "kblevel";
	}

	/// True for "blkpkg" and the legacy "kbPkg". Takes the extension without its dot.
	inline bool is_package_extension(const std::string& extension) {
		return extension == "blkpkg" || extension == "kbPkg";
	}

	void log(const char* const msg, ...);

	void error(const char* const msg, ...);
	bool error_check(const bool expression, const char* const msg = nullptr, ...);
	bool error_check(const HRESULT hr, const char* const msg = nullptr, ...);

	void warn(const char* const msg, ...);
	bool warn_check(const bool expression, const char* const msg = nullptr, ...);
	bool warn_check(const HRESULT hr, const char* const msg = nullptr, ...);
};

#define SAFE_RELEASE(object) \
	{ \
		if (object != nullptr) { \
			object->Release(); \
			object = nullptr; \
		} \
	}

/// Timer
class Timer {
public:
	Timer() {
		LARGE_INTEGER largeInt;
		QueryPerformanceFrequency(&largeInt);
		m_ClockFrequency = (f64)largeInt.QuadPart / 1000.0;

		Reset();
	}

	void Reset() {
		LARGE_INTEGER largeInt;
		QueryPerformanceCounter(&largeInt);
		m_Counter = largeInt.QuadPart;
	}

	float TimeElapsedMS() const {
		LARGE_INTEGER largeInt;
		QueryPerformanceCounter(&largeInt);

		return (float)((largeInt.QuadPart - m_Counter) / m_ClockFrequency);
	}

	float TimeElapsedSeconds() const {
		return TimeElapsedMS() / 1000.0f;
	}

private:
	double m_ClockFrequency;
	__int64 m_Counter;
};

extern Timer g_GlobalTimer;

enum ScopedTimerList_t {
	GAME_THREAD,
	GAME_ENTITY_UPDATE,
	COMPONENT_UPDATE,
	CLOTH_COMPONENT,
	GAME_THREAD_IDLE,
	RENDER_THREAD,
	RENDER_THREAD_CLEAR_BUFFERS,
	RENDER_G_BUFFER,
	RENDER_LIGHTING,
	RENDER_SHADOW_DEPTH,
	RENDER_LIGHT,
	RENDER_UNLIT,
	RENDER_TRANSLUCENCY,
	RENDER_LIGHTSHAFTS,
	RENDER_POST_PROCESS,
	RENDER_TEXT,
	RENDER_DEBUG,
	RENDER_PRESENT,
	RENDER_ENTITYID,
	RENDER_SYNC,
	RENDER_SYNC_PARTICLES,
	RENDER_GPUTIMER_STALL,
	TEMP_1,
	TEMP_2,
	TEMP_3,
	TEMP_4,
	TEMP_5,
	TEMP_6,
	TEMP_7,
	TEMP_8,
	TEMP_9,
	TEMP_10,
	MAX_NUM_SCOPED_TIMERS,
};

struct ScopedTimerData_t {
	ScopedTimerData_t(const ScopedTimerList_t timerIdx, const char* const stringName);

	String m_ReadableName;

	float GetFrameTime() const;

	const static int NUM_FRAME_TIMES = 10;
	float m_FrameTimes[NUM_FRAME_TIMES];
	int m_FrameTimeIdx;
};

/// ScopedTimer
class ScopedTimer {
public:
	ScopedTimer(ScopedTimerList_t index);
	~ScopedTimer();

private:
	Timer m_Timer;
	ScopedTimerList_t m_TimerIndex;
};

#define START_SCOPED_TIMER(index) ScopedTimer a##index(index);

void UpdateScopedTimers();
const ScopedTimerData_t& GetScopedTimerData(const ScopedTimerList_t index);

/// TextParser
struct TextParser {
	TextParser(std::string& inString) :
		m_StringBuffer(inString),
		m_StartBlock(0),
		m_EndBlock(m_StringBuffer.size() - 1) {
	}

	std::string& m_StringBuffer;
	std::string::size_type m_StartBlock;
	std::string::size_type m_EndBlock;

	bool SetBlock(const char* blockName) {
		m_StartBlock = 0;
		m_EndBlock = m_StringBuffer.size() - 1;

		static char delimiters[] = " \n\t";
		auto startBlock = m_StringBuffer.find(blockName);
		if (startBlock == std::string::npos) {
			return false;
		}

		auto endBlock = m_StringBuffer.find("}", startBlock);
		if (endBlock == std::string::npos) {
			return false;
		}

		m_StartBlock = startBlock;
		m_EndBlock = endBlock;

		return true;
	}

	void MakeLowerCase() {

		if (m_StartBlock == std::string::npos) {
			std::transform(m_StringBuffer.begin(), m_StringBuffer.end(), m_StringBuffer.begin(), ::tolower);
		} else {
			std::transform(m_StringBuffer.begin() + m_StartBlock, m_StringBuffer.begin() + m_EndBlock, m_StringBuffer.begin() + m_StartBlock, ::tolower);
		}
	}

	bool ContainsKey(const char* const key) const {
		auto keyStartPos = m_StringBuffer.find(key, m_StartBlock /*TODO: Should confine scope of search shaderStateEndBlock - shaderStateStartBlock*/);
		return (keyStartPos != std::string::npos);
	}

	bool GetValueForKey(std::string& outValue, const char* const key) const {
		outValue.clear();
		auto keyStartPos = m_StringBuffer.find(key, m_StartBlock /*TODO: Should confine scope of search shaderStateEndBlock - shaderStateStartBlock*/);
		if (keyStartPos != std::string::npos) {

			static char delimiters[] = " \n\t";
			auto valueStartPos = m_StringBuffer.find_first_of(delimiters, keyStartPos);
			if (valueStartPos == std::string::npos) {
				return false;
			}

			valueStartPos = m_StringBuffer.find_first_not_of(delimiters, valueStartPos + 1);
			if (valueStartPos == std::string::npos) {
				return false;
			}

			auto endValuePos = m_StringBuffer.find_first_of(delimiters, valueStartPos);
			if (endValuePos == std::string::npos) {
				return false;
			}

			outValue = m_StringBuffer.substr(valueStartPos, endValuePos - valueStartPos);
			return true;
		}

		return false;
	}

	void EraseBlock() {
		m_StringBuffer.erase(m_StartBlock, 1 + m_EndBlock - m_StartBlock);
	}

	void ReplaceBlockWithSpaces() {
		for (size_t i = m_StartBlock; i <= m_EndBlock; i++) {
			if (m_StringBuffer[i] != '\n') {
				m_StringBuffer[i] = ' ';
			}
		}
	}

	void RemoveComments() {

		// Remove comments
		for (int i = 0; i < m_StringBuffer.size() - 1; i++) {
			if (m_StringBuffer[i] == '/' && m_StringBuffer[i + 1] == '*') {
				int j = i;
				while (j < m_StringBuffer.size() - 1 && !(m_StringBuffer[j] == '*' && m_StringBuffer[j + 1] == '/')) {

					// Preserve new lines so that any error messages still line up with the source flie
					if (m_StringBuffer[j] != '\n') {
						m_StringBuffer[j] = ' ';
					}
					j++;
				}

				if (j < m_StringBuffer.size() - 1) {
					m_StringBuffer[j] = ' ';
					m_StringBuffer[j + 1] = ' ';
				}
			}

			if (m_StringBuffer[i] == '/' && m_StringBuffer[i + 1] == '/') {
				int j = i;
				while (j < m_StringBuffer.size() - 1 && m_StringBuffer[j] != '\n') {
					m_StringBuffer[j] = ' ';
					j++;
				}
			}
		}
	}
};
