// =========================================================
// ファイルの情報[input.cpp]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#include "main.h"
#include "input.h"

// =========================================================
// 入力状態
// =========================================================
BYTE Input::m_oldKeyState[256];
BYTE Input::m_keyState[256];
int Input::m_pendingMouseX = 0;
int Input::m_pendingMouseY = 0;
int Input::m_pendingMouseWheel = 0;
int Input::m_pendingCursorX = 0;
int Input::m_pendingCursorY = 0;
int Input::m_pendingCursorWheel = 0;
int Input::m_lastCursorX = 0;
int Input::m_lastCursorY = 0;
bool Input::m_hasCursorPosition = false;
int Input::m_mouseX = 0;
int Input::m_mouseY = 0;
int Input::m_mouseWheel = 0;
bool Input::m_mouseCaptured = false;
bool Input::m_acceptGameplayInput = true;

// =========================================================
// キーボードと相対マウス入力の初期化
// =========================================================
void Input::Init()
{
    memset(m_oldKeyState, 0, sizeof(m_oldKeyState));
    memset(m_keyState, 0, sizeof(m_keyState));

    RAWINPUTDEVICE mouseDevice{};
    mouseDevice.usUsagePage = 0x01;
    mouseDevice.usUsage = 0x02;
    mouseDevice.hwndTarget = GetWindow();
    const BOOL registered = RegisterRawInputDevices(&mouseDevice, 1, sizeof(mouseDevice));
    assert(registered);
    (void)registered;
}

// =========================================================
// ウィンドウ終了時にカーソルを解放
// =========================================================
void Input::Uninit()
{
    SetMouseCapture(false);
}

// =========================================================
// 1フレーム分の入力を確定
// =========================================================
void Input::Update(bool acceptGameplayInput)
{
    // UI を操作中もキーの状態を更新し、閉じた直後の誤トリガーを防ぐ。
    m_acceptGameplayInput = acceptGameplayInput;
    memcpy(m_oldKeyState, m_keyState, sizeof(m_keyState));
    GetKeyboardState(m_keyState);

    // Raw input wins when both APIs report the same physical mouse move.
    const bool hasRawMovement = m_pendingMouseX != 0 || m_pendingMouseY != 0;
    m_mouseX = hasRawMovement ? m_pendingMouseX : m_pendingCursorX;
    m_mouseY = hasRawMovement ? m_pendingMouseY : m_pendingCursorY;
    m_mouseWheel = m_pendingMouseWheel != 0 ? m_pendingMouseWheel : m_pendingCursorWheel;
    m_pendingMouseX = 0;
    m_pendingMouseY = 0;
    m_pendingMouseWheel = 0;
    m_pendingCursorX = m_pendingCursorY = m_pendingCursorWheel = 0;
}

bool Input::GetKeyPress(BYTE keyCode)
{
    return m_acceptGameplayInput && (m_keyState[keyCode] & 0x80) != 0;
}

bool Input::GetKeyTrigger(BYTE keyCode)
{
    return m_acceptGameplayInput && (m_keyState[keyCode] & 0x80) != 0 &&
        (m_oldKeyState[keyCode] & 0x80) == 0;
}

// =========================================================
// WM_INPUTから移動量とホイールを加算
// =========================================================
void Input::OnRawInput(LPARAM lParam)
{
    RAWINPUT rawInput{};
    UINT dataSize = sizeof(rawInput);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
        &rawInput, &dataSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        rawInput.header.dwType != RIM_TYPEMOUSE || !m_mouseCaptured)
    {
        return;
    }

    const RAWMOUSE& mouse = rawInput.data.mouse;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
    {
        m_pendingMouseX += mouse.lLastX;
        m_pendingMouseY += mouse.lLastY;
    }
    if ((mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0)
    {
        m_pendingMouseWheel += static_cast<SHORT>(mouse.usButtonData);
    }
}

// =========================================================
// 互換入力: 自動操作やRaw Inputを持たないポインター用
// =========================================================
void Input::OnMouseMove(LPARAM lParam)
{
    if (!m_mouseCaptured)
    {
        return;
    }

    const int cursorX = static_cast<SHORT>(LOWORD(lParam));
    const int cursorY = static_cast<SHORT>(HIWORD(lParam));
    if (m_hasCursorPosition)
    {
        m_pendingCursorX += cursorX - m_lastCursorX;
        m_pendingCursorY += cursorY - m_lastCursorY;
    }
    m_lastCursorX = cursorX;
    m_lastCursorY = cursorY;
    m_hasCursorPosition = true;
}

void Input::OnMouseWheel(WPARAM wParam)
{
    if (m_mouseCaptured)
    {
        m_pendingCursorWheel += GET_WHEEL_DELTA_WPARAM(wParam);
    }
}
// =========================================================
// フォーカスに合わせてカーソル拘束を切り替え
// =========================================================
void Input::SetMouseCapture(bool capture)
{
    if (m_mouseCaptured == capture)
    {
        return;
    }

    m_mouseCaptured = capture;
    m_pendingMouseX = m_pendingMouseY = m_pendingMouseWheel = 0;
    m_pendingCursorX = m_pendingCursorY = m_pendingCursorWheel = 0;
    m_hasCursorPosition = false;
    m_mouseX = m_mouseY = m_mouseWheel = 0;
    if (capture)
    {
        RefreshMouseClip();
    }
    else
    {
        ClipCursor(nullptr);
    }
}

// =========================================================
// ウィンドウの移動やサイズ変更後に拘束範囲を更新
// =========================================================
void Input::RefreshMouseClip()
{
    if (!m_mouseCaptured || IsIconic(GetWindow()))
    {
        return;
    }

    RECT clientRect{};
    GetClientRect(GetWindow(), &clientRect);
    POINT topLeft{ clientRect.left, clientRect.top };
    POINT bottomRight{ clientRect.right, clientRect.bottom };
    ClientToScreen(GetWindow(), &topLeft);
    ClientToScreen(GetWindow(), &bottomRight);
    RECT screenRect{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    ClipCursor(&screenRect);
}