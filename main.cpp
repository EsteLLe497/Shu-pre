

#include "main.h"
#include "manager.h"
#include "input.h"
#include "ToonPanel.h"
#include <thread>


const char* CLASS_NAME = "AppClass";
const char* WINDOW_NAME = "DX11ƒQ[ƒ€";


LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


HWND g_Window;

HWND GetWindow()
{
	return g_Window;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{


	WNDCLASSEX wcex;
	{
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = 0;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = nullptr;
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = nullptr;
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = CLASS_NAME;
		wcex.hIconSm = nullptr;

		RegisterClassEx(&wcex);


		RECT rc = { 0, 0, (LONG)SCREEN_WIDTH, (LONG)SCREEN_HEIGHT };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		g_Window = CreateWindowEx(0, CLASS_NAME, WINDOW_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);
	}

	CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);


	Manager::Init();



	ShowWindow(g_Window, nCmdShow);
	UpdateWindow(g_Window);




	DWORD dwExecLastTime;
	DWORD dwCurrentTime;
	timeBeginPeriod(1);
	dwExecLastTime = timeGetTime();
	dwCurrentTime = 0;



	MSG msg{};
	while(1)
	{
        // Drain high-frequency raw mouse messages before each fixed update.
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.message == WM_QUIT)
        {
            break;
        }

        dwCurrentTime = timeGetTime();
        if ((dwCurrentTime - dwExecLastTime) >= (1000 / 60))
        {
            dwExecLastTime = dwCurrentTime;
            Manager::Update();
            Manager::Draw();
        }
	}

	timeEndPeriod(1);

	UnregisterClass(CLASS_NAME, wcex.hInstance);

	Manager::Uninit();

	CoUninitialize();

	return (int)msg.wParam;
}




LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

    if (const LRESULT handled = ToonPanel::handleMessage(hWnd, uMsg, wParam, lParam))
        return handled;

    switch (uMsg)
    {
    case WM_SETFOCUS:
        Input::SetMouseCapture(!ToonPanel::isOpen());
        return 0;

    case WM_KILLFOCUS:
        Input::SetMouseCapture(false);
        return 0;

    case WM_ENTERSIZEMOVE:
        Input::SetMouseCapture(false);
        break;

    case WM_EXITSIZEMOVE:
        if (GetFocus() == hWnd && !ToonPanel::isOpen()) Input::SetMouseCapture(true);
        break;

    case WM_MOVE:
        Input::RefreshMouseClip();
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) Input::SetMouseCapture(false);
        else Input::RefreshMouseClip();
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && Input::IsMouseCaptured())
        {
            SetCursor(nullptr);
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
        Input::OnMouseMove(lParam);
        break;

    case WM_MOUSEWHEEL:
        Input::OnMouseWheel(wParam);
        break;

    case WM_INPUT:
        Input::OnRawInput(lParam);
        break;

    case WM_DESTROY:
        Input::SetMouseCapture(false);
        PostQuitMessage(0);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hWnd);
        break;

    default:
        break;
    }
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

