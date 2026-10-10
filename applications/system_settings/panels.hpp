// The Settings panels. Each builder returns the panel's content; it is built the first time the panel is
// shown and then kept, so its live bindings keep it current while hidden.
#pragma once

#include <mayonnaise/mayonnaise.hpp>

#include <functional>
#include <map>
#include <span>
#include <string>

struct Context {
    mayo::App& app;
    mayo::Window& window;
    /// Opens another panel by id.
    std::function<void(const std::string& id)> show;
    /// Anchor for popovers that report background failures (the toolbar title).
    mayo::Element anchor;
    /// Run every time the panel with this id is shown (Wi-Fi scans, Bluetooth discovery).
    std::multimap<std::string, std::function<void()>> onShown;
    /// Screenshot runs: MAYO_DEMO=<panel>:<action> opens a popover of that panel ("zones", "add", ...).
    std::string demo;
};

using PanelBuilder = mayo::Element (*)(Context&);

struct PanelInfo {
    const char* id;
    const char* title;
    mayo::Icon icon;
    mayo::Color mayo::ThemeColors::*tint;
    int group;             ///< sidebar group (a gap separates groups)
    const char* keywords;  ///< extra search terms, lower case
    PanelBuilder build;
};

std::span<const PanelInfo> panels();
const PanelInfo* findPanel(const std::string& id);

// Connectivity
mayo::Element wifiPanel(Context& ctx);
mayo::Element networkPanel(Context& ctx);
mayo::Element bluetoothPanel(Context& ctx);
// Devices
mayo::Element displaysPanel(Context& ctx);
mayo::Element soundPanel(Context& ctx);
mayo::Element powerPanel(Context& ctx);
// Desktop
mayo::Element appearancePanel(Context& ctx);
mayo::Element multitaskingPanel(Context& ctx);
mayo::Element notificationsPanel(Context& ctx);
mayo::Element searchPanel(Context& ctx);
mayo::Element appsPanel(Context& ctx);
// Input
mayo::Element mousePanel(Context& ctx);
mayo::Element keyboardPanel(Context& ctx);
mayo::Element accessibilityPanel(Context& ctx);
// Privacy
mayo::Element privacyPanel(Context& ctx);
mayo::Element wellbeingPanel(Context& ctx);
// System
mayo::Element regionPanel(Context& ctx);
mayo::Element dateTimePanel(Context& ctx);
mayo::Element usersPanel(Context& ctx);
mayo::Element aboutPanel(Context& ctx);
