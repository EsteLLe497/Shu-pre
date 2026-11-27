#pragma once

// Sections share the existing ImGui frame; F3 owns only the debug workspace.
namespace DebugPanel
{
    bool isOpen();
    void toggle();
    void close();
    void draw();
    void drawWorldMarkers();
}
