// =========================================================
// ファイルの情報[ToonPanel.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <windows.h>

struct ToonSettings;
struct CameraOrbit;

namespace ToonPanel
{
    void init(HWND window, bool previewMode);
    void uninit();
    bool isOpen();
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void beginFrame();
    void beginFrame(ToonSettings* settings, CameraOrbit* orbit);
    void editAppearance(ToonSettings& settings);
    void draw();
}
