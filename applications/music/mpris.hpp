// MPRIS (org.mpris.MediaPlayer2) on the session bus, so media keys, the desktop's media controls and
// playerctl drive the app. Built with GIO when it is available; otherwise every call is a no-op.
//
// GDBus runs on a thread with its own main context; method calls hop to the UI thread with App::post,
// and property reads are served from a snapshot the UI thread updates.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mayo {
class App;
}

class Mpris {
public:
    struct State {
        std::string status = "Stopped";  ///< "Playing", "Paused" or "Stopped"
        std::string trackPath;           ///< identifies the track (becomes mpris:trackid)
        std::string title;
        std::string artist;
        std::string album;
        std::string albumArtist;
        std::string artUrl;  ///< file:// URI of the cover
        int trackNumber = 0;
        double length = 0;  ///< seconds
        double volume = 1;
        bool shuffle = false;
        std::string loop = "None";  ///< "None", "Track" or "Playlist"
        bool canNext = false;
        bool canPrevious = false;
        bool operator==(const State&) const = default;
    };
    struct Handlers {
        std::function<void()> raise, quit, playPause, play, pause, stop, next, previous;
        std::function<void(double seconds)> seekBy;
        std::function<void(double seconds)> setPosition;
        std::function<void(double)> setVolume;
        std::function<void(bool)> setShuffle;
        std::function<void(const std::string&)> setLoop;
    };

    Mpris(mayo::App& app, const std::string& desktopId, Handlers handlers);
    ~Mpris();
    Mpris(const Mpris&) = delete;
    Mpris& operator=(const Mpris&) = delete;

    /// UI thread: publishes changed properties.
    void update(const State& state);
    /// UI thread: the playback position, for clients that poll it.
    void setPosition(double seconds);
    /// UI thread: after a jump (seeks, a new track), so clients resynchronise their progress bars.
    void seeked(double seconds);

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};
