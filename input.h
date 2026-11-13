#pragma once

class Input
{
private:
    static BYTE m_oldKeyState[256];
    static BYTE m_keyState[256];
    static int m_pendingMouseX;
    static int m_pendingMouseY;
    static int m_pendingMouseWheel;
    static int m_pendingCursorX;
    static int m_pendingCursorY;
    static int m_pendingCursorWheel;
    static int m_lastCursorX;
    static int m_lastCursorY;
    static bool m_hasCursorPosition;
    static int m_mouseX;
    static int m_mouseY;
    static int m_mouseWheel;
    static bool m_mouseCaptured;
    static bool m_acceptGameplayInput;
    static bool m_pendingAttackPress;
    static bool m_attackPress;

public:
    static void Init();
    static void Uninit();
    static void Update(bool acceptGameplayInput);

    static bool GetKeyPress(BYTE keyCode);
    static bool GetKeyTrigger(BYTE keyCode);
    static bool isGameplayActive();
    static bool hasGameplayActivity();
    static int GetMouseDeltaX() { return m_mouseX; }
    static int GetMouseDeltaY() { return m_mouseY; }
    static int GetMouseWheel() { return m_mouseWheel; }
    static bool IsMouseCaptured() { return m_mouseCaptured; }

    // Window messages feed raw input; Camera reads the accumulated frame delta.
    static void OnAttackPress() { m_pendingAttackPress = true; }
    static void OnRawInput(LPARAM lParam);
    static void OnMouseMove(LPARAM lParam);
    static void OnMouseWheel(WPARAM wParam);
    static void SetMouseCapture(bool capture);
    static void RefreshMouseClip();
};