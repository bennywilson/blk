/// blk_platform.h
///
/// 2026 blk

#pragma once

/// THROWAWAY SPIKE SCAFFOLDING - see the Phase 5 web-viewer plan.
///
/// This header exists to answer one question: does the engine core compile and
/// run under Emscripten at all? It answers it the cheap way, by supplying the
/// Win32 spellings the core already uses (`HWND`, `WPARAM`, `HRESULT`,
/// `DebugBreak`, `fopen_s`, ...) on platforms that are not Windows, so that
/// `renderer.h`, `game.h`, `input_manager.h` and `resource_manager.h` need no
/// edits at all while the question is still open.
///
/// That is deliberate debt, not a design. Win32 type names on a platform that
/// is not Win32 are a lie the compiler is happy to believe; the real fix is
/// engine-neutral names (`blk::WindowHandle`, `blk::Result`) with the Win32
/// types confined to the D3D12 backend. Do that once the viewer actually
/// renders, and delete this file in the same change.
///
/// Nothing here is compiled on Windows - the whole file is inside
/// `#if !defined(_WIN32)`, so the native builds see none of it.

#if !defined(_WIN32)

	#include <cfloat>
	#include <chrono>
	#include <climits>
	#include <cmath>
	#include <cstdarg>
	#include <cstdint>
	#include <cstdio>
	#include <cstdlib>
	#include <cstring>
	#include <filesystem>
	#include <mutex>
	#include <sys/stat.h>
	#include <unistd.h>

/// Scalar and handle types
typedef void* HWND;
typedef void* HANDLE;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* LPVOID;
typedef const char* LPCSTR;
typedef char* LPSTR;

typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef long LONG;
typedef unsigned long ULONG;
typedef short SHORT;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef long HRESULT;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;

/// `min`/`max` at global scope.
///
/// windows.h defines these as macros (the project does not set `NOMINMAX`) and
/// the engine calls them unqualified - `bounds.h` is the one in the viewer's
/// path. Function templates rather than macros: they behave the same at every
/// call site here, and unlike macros they do not wreck libc++'s own headers.
template<typename T>
inline constexpr T max(const T a, const T b) {
	return (a > b) ? a : b;
}

template<typename T>
inline constexpr T min(const T a, const T b) {
	return (a < b) ? a : b;
}

	#if !defined(__int64)
		#define __int64 long long
	#endif

/// No key is ever down. The callers in the viewer's path are debug toggles.
inline SHORT GetAsyncKeyState(int) {
	return 0;
}

	// Win32 virtual-key codes, at their Win32 values.
	#define VK_UP 0x26
	#define VK_DOWN 0x28
	#define VK_OEM_MINUS 0xBD

	#define TRUE 1
	#define FALSE 0
	#define INFINITE 0xFFFFFFFF
	#define MAX_PATH 260
	#define CALLBACK
	#define WINAPI
	#define APIENTRY

/// `GUID` - laid out to match the Win32 struct, because `Guid` in `blk_core.h`
/// unions it with a `uint[4]` and the serializer writes those four words.
struct GUID {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
};

/// HRESULT handling. `S_OK` is zero and failure is the sign bit, exactly as on
/// Win32, so `blk::error_check(HRESULT)` keeps working unmodified.
	#define S_OK ((HRESULT)0)
	#define E_FAIL ((HRESULT)0x80004005L)
	#define FAILED(hr) (((HRESULT)(hr)) < 0)
	#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)

	#define COINIT_MULTITHREADED 0
inline HRESULT CoInitializeEx(void*, int) {
	return S_OK;
}
inline void CoUninitialize() {}

/// Fills `out` with pseudo-random bytes. Good enough to keep entity GUIDs
/// distinct within a session; it is NOT a real UUID generator, and a viewer
/// only ever reads GUIDs that were authored on Windows.
inline HRESULT CoCreateGuid(GUID* const out) {
	uint8_t* const bytes = (uint8_t*)out;
	for (size_t i = 0; i < sizeof(GUID); i++) {
		bytes[i] = (uint8_t)(rand() & 0xFF);
	}

	return S_OK;
}

/// Mutex emulation.
///
/// `blk_core.cpp` guards its log buffer with `CreateMutex` /
/// `WaitForSingleObject` / `ReleaseMutex`. Emulating those four calls keeps
/// that file free of `#if`s. `WaitForSingleObject` ignores its timeout and
/// always blocks - every call site passes `INFINITE`.
	#define WAIT_OBJECT_0 0
inline HANDLE CreateMutex(void*, BOOL, const char*) {
	return (HANDLE)new std::recursive_mutex();
}

inline DWORD WaitForSingleObject(HANDLE handle, DWORD) {
	if (handle != nullptr) {
		((std::recursive_mutex*)handle)->lock();
	}

	return WAIT_OBJECT_0;
}

inline BOOL ReleaseMutex(HANDLE handle) {
	if (handle != nullptr) {
		((std::recursive_mutex*)handle)->unlock();
	}

	return TRUE;
}

inline BOOL CloseHandle(HANDLE handle) {
	delete (std::recursive_mutex*)handle;
	return TRUE;
}

inline void Sleep(const DWORD milliseconds) {
	usleep((useconds_t)milliseconds * 1000);
}

/// Filesystem
inline BOOL CreateDirectoryA(const char* const path, void*) {
	return mkdir(path, 0755) == 0 ? TRUE : FALSE;
}

	#define CreateDirectory CreateDirectoryA

inline BOOL SetCurrentDirectoryA(const char* const path) {
	return chdir(path) == 0 ? TRUE : FALSE;
}

	#define SetCurrentDirectory SetCurrentDirectoryA

inline BOOL CopyFileA(const char* const source, const char* const destination, const BOOL fail_if_exists) {
	const auto options = fail_if_exists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
	std::error_code ec;
	return std::filesystem::copy_file(source, destination, options, ec) ? TRUE : FALSE;
}

	#define CopyFile CopyFileA

inline BOOL DeleteFileA(const char* const path) {
	return remove(path) == 0 ? TRUE : FALSE;
}

	#define DeleteFile DeleteFileA

	#define ZeroMemory(destination, length) memset((destination), 0, (length))

/// Diagnostics.
///
/// `DebugBreak` is a no-op rather than `__builtin_trap()` on purpose:
/// `blk::error_check()` calls it and then throws, and the throw carries the
/// formatted message that makes a failure readable. Trapping here would kill
/// the process before that message ever reached the console.
inline void DebugBreak() {}

/// Deliberately a no-op. `write_to_file()` in `blk_core.cpp` sends every line
/// to BOTH std::cout and OutputDebugString - on Windows those land in different
/// places (console vs debugger), but here they would both land in the page, and
/// every log line would appear twice.
inline void OutputDebugStringA(const char* const msg) {}

	#define OutputDebugString OutputDebugStringA

	#define MB_OK 0
	#define MB_ICONWARNING 0
	#define MB_ICONERROR 0
inline int MessageBoxA(void*, const char* const text, const char* const caption, unsigned int) {
	fprintf(stderr, "[%s] %s\n", caption ? caption : "blk", text ? text : "");
	return 0;
}

	#define MessageBox MessageBoxA

/// MSVC CRT secure-variant shims. The engine is written against these
/// throughout; the portable equivalents differ only in how they report size.
inline int _vscprintf(const char* const format, va_list args) {
	va_list copy;
	va_copy(copy, args);
	const int length = vsnprintf(nullptr, 0, format, copy);
	va_end(copy);

	return length;
}

inline int vsprintf_s(char* const buffer, size_t size, const char* const format, va_list args) {
	return vsnprintf(buffer, size, format, args);
}

inline int sprintf_s(char* const buffer, size_t size, const char* const format, ...) {
	va_list args;
	va_start(args, format);
	const int written = vsnprintf(buffer, size, format, args);
	va_end(args);

	return written;
}

/// MSVC's array overload, which takes the size from the array's type.
template<size_t N>
inline int sprintf_s(char (&buffer)[N], const char* const format, ...) {
	va_list args;
	va_start(args, format);
	const int written = vsnprintf(buffer, N, format, args);
	va_end(args);

	return written;
}

inline int fopen_s(FILE** const out, const char* const path, const char* const mode) {
	*out = fopen(path, mode);
	return (*out != nullptr) ? 0 : -1;
}

inline int strcpy_s(char* const dest, size_t size, const char* const src) {
	if (dest == nullptr || src == nullptr || size == 0) {
		return -1;
	}

	strncpy(dest, src, size - 1);
	dest[size - 1] = '\0';

	return 0;
}

#endif // !defined(_WIN32)
