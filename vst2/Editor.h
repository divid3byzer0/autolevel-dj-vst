#pragma once
#include "Plugin.h"
#include <memory>

namespace aldj {

/**
 * The AutoLevel DJ editor for the VST2 build: the JUCE build's editor (src/PluginEditor.cpp),
 * redrawn with Win32 + GDI+ so it needs no JUCE. Same 840 x 700 layout, colours, controls and
 * behaviour; it scales from the bottom-right corner when the host allows resizing.
 */
class Editor {
public:
    explicit Editor(Plugin& plugin);
    ~Editor();

    bool open(void* parentWindow);
    void close();
    vst2::ERect* rect();
    void idle() {}

    struct Impl;

private:
    Plugin& m_plugin;
    std::unique_ptr<Impl> m_impl;
    vst2::ERect m_rect{};
};

} // namespace aldj
