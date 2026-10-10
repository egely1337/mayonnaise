// Dragging files out of the app on Wayland (SDL3 can receive drops but can't start a drag).
//
// libwayland-client is dlopen()ed, like SDL itself does, so there is no build dependency. The objects live on
// SDL's wl_display but on their own event queue: a second wl_pointer on the seat tells us the serial of the
// button press that a drag must be started with, and a wl_data_device + wl_data_source carry the files as
// text/uri-list.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mayo::platform::wayland {

/// Starts a drag of `paths` from `surface` (both are SDL's wl_display / wl_surface). `ended` runs on the UI
/// thread (from dispatch()) once the drop happened or the drag was cancelled. False if not possible.
bool startFileDrag(void* display, void* surface, const std::vector<std::string>& paths, std::function<void()> ended);

/// Handles queued protocol events (pointer serials, data requests). Call after SDL pumped the display.
void dispatch();

/// While a drag runs, the event loop must wake up often enough to answer the drop target's data requests.
[[nodiscard]] bool dragActive();

/// Sets up the pointer tracking early, so the serial of the press that starts a drag is known.
void prepare(void* display);

}  // namespace mayo::platform::wayland
