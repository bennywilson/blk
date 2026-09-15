/// input_manager.h
///
/// 2017 blk

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>
#include <unordered_map>
#include "blk_core.h"
#include "Matrix.h"

/// Input_t
struct Input_t {
	Input_t() {
		memset(this, 0, sizeof(Input_t));
	}

	enum KeyAction_t {
		KA_None,
		KA_JustPressed,
		KA_Down,
		KA_JustReleased,
	};

	enum Arrow_t {
		Up,
		Left,
		Right,
		Down
	};

	enum NonCharKey_t {
		Escape = 0,
		LCtrl,
		RCtrl,
		Return,
		Num_NonCharKeys,
	};

	const static char KB_SPACE = 32;

	bool IsKeyPressedOrDown(const char key) const { return KeyState[key].m_Action == KA_JustPressed || KeyState[key].m_Action == KA_Down; }
	bool WasKeyJustPressed(const char key, const bool bConsumeInput = true) const {
		return KeyState[key].m_Action == KA_JustPressed;
	}

	bool IsArrowPressedOrDown(const Arrow_t arrow) const { return ArrowState[arrow].m_Action == KA_JustPressed || ArrowState[arrow].m_Action == KA_Down; }
	bool WasArrowJustPressed(const Arrow_t arrow) const { return ArrowState[arrow].m_Action == KA_JustPressed; }

	bool IsNonCharKeyPressedOrDown(const NonCharKey_t key) const { return NonCharKeyState[key].m_Action == KA_JustPressed || NonCharKeyState[key].m_Action == KA_Down; }
	bool WasNonCharKeyJustPressed(const NonCharKey_t key) const { return NonCharKeyState[key].m_Action == KA_JustPressed; }

	struct KeyState_t {
		KeyAction_t	m_Action;
		float			m_LastActionTimeSec;
	};

	KeyState_t	KeyState[256];
	KeyState_t	ArrowState[4];
	KeyState_t	GamepadButtonStates[16];
	KeyState_t	NonCharKeyState[Num_NonCharKeys];

	Vec2			m_LeftStick;
	Vec2			m_PrevLeftStick;

	Vec2			m_RightStick;
	Vec2			m_PrevRightStick;

	float			LeftTrigger;
	float			RightTrigger;
	bool			RightTriggerPressed;
	LONG			MouseDeltaX;
	LONG			MouseDeltaY;
	LONG			AbsCursorX;
	LONG			AbsCursorY;
	bool			LeftMouseButtonPressed;
	bool			LeftMouseButtonDown;
	bool			RightMouseButtonPressed;
	bool			RightMouseButtonDown;
};

/// InputCallback
class InputCallback {
	friend class InputManager;

private:
	virtual void InputKeyPressedCB(const int cbParam) = 0;
	virtual const char* GetInputCBName() const = 0;
};

///	KeyComboBitField_t - Represents a combination of pressed keys
struct KeyComboBitField_t {
	KeyComboBitField_t() :
		m_Bits0(0),
		m_Bits1(0),
		m_Ctrl(false),
		m_Shift(false) { }

	bool operator==(const KeyComboBitField_t& rhs) const {
		return m_Bits0 == rhs.m_Bits0 && m_Bits1 == rhs.m_Bits1 && m_Ctrl == rhs.m_Ctrl && m_Shift == rhs.m_Shift;
	}

	__int64	m_Bits0;
	__int64	m_Bits1;
	bool m_Ctrl;
	bool m_Shift;
};

///	KeyComboBitFieldHash_t
struct KeyComboBitFieldHash_t {
	std::size_t operator() (const KeyComboBitField_t& rhs) const {
		return rhs.m_Bits0 ^ rhs.m_Bits1 ^ ((__int64)rhs.m_Ctrl << 63) ^ ((__int64)rhs.m_Shift << 62);
	}
};

///	InputCallbackInfo_t - Callback used when a combination of keys matches a KeyComboBitField_t
struct InputCallbackInfo_t {
	InputCallbackInfo_t() :
		m_CallbackObject(nullptr),
		m_CallbackParam(0) { }

	InputCallback* m_CallbackObject;
	int	m_CallbackParam;
	std::string	m_HelpDescription;
	std::string	m_KeyComboDisplayString;
};

typedef std::unordered_map<KeyComboBitField_t, InputCallbackInfo_t, KeyComboBitFieldHash_t> KeyComboMapType;

typedef DWORD(WINAPI* LPXINPUTGETSTATE)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD(WINAPI* LPXINPUTSETSTATE)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
typedef DWORD(WINAPI* LPXINPUTGETCAPABILITIES)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
typedef void (WINAPI* LPXINPUTENABLE)(BOOL bEnable);
typedef DWORD(WINAPI* LPXINPUTGETSTATE)(DWORD dwUserIndex, XINPUT_STATE* pState);

/// IInputListener - inherit to make your class a listener for key combo presses
class IInputListener abstract {
	friend class InputManager;

protected:
	virtual void InputCB(const Input_t& input) = 0;
};

///  InputManager
class InputManager {
public:
	InputManager();
	~InputManager();

	const Input_t& get_input() const { return m_Input; }

	void Init(HWND Hwnd);
	void Update(const float Delta);
	void UpdateKey(const uint keyPress);

	void MapKeysToCallback(const std::string& stringCombo, InputCallback* const pCB, int CallbackParam, const std::string& helpDescription = "");
	void UnmapCallback(InputCallback* const pCB);

	const KeyComboMapType& GetKeyComboMap() const { return m_KeyComboToCallbackMap; }

	enum eMouseBehavior_t {
		MB_LockToCenter,
		MB_LockToWindow
	};

	void SetMouseBehavior(const eMouseBehavior_t newMouseBehavior) { m_MouseBehavior = newMouseBehavior; }
	eMouseBehavior_t GetMouseBehavior() const { return m_MouseBehavior; }

	Vec2i GetMouseCursorPosition() const { return Vec2i(m_Input.AbsCursorX, m_Input.AbsCursorY); }

	void RegisterInputListener(IInputListener* const pListener);
	void UnregisterInputListener(IInputListener* const pListener);

private:
	LPXINPUTENABLE m_FuncXInputEnable;
	LPXINPUTGETSTATE m_FuncXInputGetState;
	HWND m_Hwnd;

	Input_t m_Input;

	KeyComboBitField_t m_KeyComboBitField;

private:
	KeyComboMapType	m_KeyComboToCallbackMap;

	eMouseBehavior_t m_MouseBehavior;

	std::vector<IInputListener*> m_InputListeners;
};

extern InputManager* g_pInputManager;

