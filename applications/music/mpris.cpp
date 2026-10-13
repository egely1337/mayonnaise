#include "mpris.hpp"

#include <mayonnaise/app.hpp>

#ifdef MAYO_MUSIC_MPRIS

#include <gio/gio.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

constexpr const char* kPath = "/org/mpris/MediaPlayer2";

constexpr const char* kXml = R"(
<node>
  <interface name="org.mpris.MediaPlayer2">
    <method name="Raise"/>
    <method name="Quit"/>
    <property name="CanQuit" type="b" access="read"/>
    <property name="CanRaise" type="b" access="read"/>
    <property name="HasTrackList" type="b" access="read"/>
    <property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/>
    <property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <method name="Next"/>
    <method name="Previous"/>
    <method name="Pause"/>
    <method name="PlayPause"/>
    <method name="Stop"/>
    <method name="Play"/>
    <method name="Seek"><arg direction="in" name="Offset" type="x"/></method>
    <method name="SetPosition"><arg direction="in" name="TrackId" type="o"/><arg direction="in" name="Position" type="x"/></method>
    <method name="OpenUri"><arg direction="in" name="Uri" type="s"/></method>
    <signal name="Seeked"><arg name="Position" type="x"/></signal>
    <property name="PlaybackStatus" type="s" access="read"/>
    <property name="LoopStatus" type="s" access="readwrite"/>
    <property name="Rate" type="d" access="readwrite"/>
    <property name="Shuffle" type="b" access="readwrite"/>
    <property name="Metadata" type="a{sv}" access="read"/>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Position" type="x" access="read"/>
    <property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/>
    <property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/>
    <property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/>
    <property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
  </interface>
</node>)";

/// mpris:trackid must be an object path; derive a stable one from the file path.
std::string trackId(const std::string& path) {
    if (path.empty()) return "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    return "/io/github/egely1337/Music/Track/T" + std::to_string(std::hash<std::string>{}(path));
}

}  // namespace

struct Mpris::Impl {
    mayo::App& app;
    std::string desktopId;
    Handlers handlers;
    std::mutex mutex;
    State state;
    double position = 0;  // seconds, at positionTime
    std::chrono::steady_clock::time_point positionTime = std::chrono::steady_clock::now();

    GMainContext* context = nullptr;
    GMainLoop* loop = nullptr;
    std::atomic<GDBusConnection*> bus{nullptr};  // set by the D-Bus thread once registered
    GDBusNodeInfo* info = nullptr;
    std::vector<guint> registrations;
    guint owner = 0;
    std::thread thread;

    Impl(mayo::App& a, std::string id, Handlers h) : app(a), desktopId(std::move(id)), handlers(std::move(h)) {}

    void post(const std::function<void()>& fn) {
        if (fn) app.post(fn);
    }

    double currentPosition() {
        // Called with the mutex held: extrapolate while playing so polling clients see time pass.
        double p = position;
        if (state.status == "Playing")
            p += std::chrono::duration<double>(std::chrono::steady_clock::now() - positionTime).count();
        return std::max(0.0, std::min(p, state.length > 0 ? state.length : p));
    }

    GVariant* metadata() {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&b, "{sv}", "mpris:trackid", g_variant_new_object_path(trackId(state.trackPath).c_str()));
        if (!state.trackPath.empty()) {
            g_variant_builder_add(&b, "{sv}", "mpris:length", g_variant_new_int64(static_cast<gint64>(state.length * 1e6)));
            g_variant_builder_add(&b, "{sv}", "xesam:title", g_variant_new_string(state.title.c_str()));
            const char* artists[] = {state.artist.c_str(), nullptr};
            g_variant_builder_add(&b, "{sv}", "xesam:artist", g_variant_new_strv(artists, 1));
            g_variant_builder_add(&b, "{sv}", "xesam:album", g_variant_new_string(state.album.c_str()));
            if (!state.albumArtist.empty()) {
                const char* aa[] = {state.albumArtist.c_str(), nullptr};
                g_variant_builder_add(&b, "{sv}", "xesam:albumArtist", g_variant_new_strv(aa, 1));
            }
            if (state.trackNumber > 0) g_variant_builder_add(&b, "{sv}", "xesam:trackNumber", g_variant_new_int32(state.trackNumber));
            char* uri = g_filename_to_uri(state.trackPath.c_str(), nullptr, nullptr);
            if (uri) {
                g_variant_builder_add(&b, "{sv}", "xesam:url", g_variant_new_string(uri));
                g_free(uri);
            }
            if (!state.artUrl.empty()) g_variant_builder_add(&b, "{sv}", "mpris:artUrl", g_variant_new_string(state.artUrl.c_str()));
        }
        return g_variant_builder_end(&b);
    }

    GVariant* property(const std::string& iface, const std::string& name) {
        std::scoped_lock lock(mutex);
        if (iface == "org.mpris.MediaPlayer2") {
            if (name == "CanQuit" || name == "CanRaise") return g_variant_new_boolean(TRUE);
            if (name == "HasTrackList") return g_variant_new_boolean(FALSE);
            if (name == "Identity") return g_variant_new_string("Music");
            if (name == "DesktopEntry") return g_variant_new_string(desktopId.c_str());
            if (name == "SupportedUriSchemes") {
                const char* s[] = {"file", nullptr};
                return g_variant_new_strv(s, 1);
            }
            if (name == "SupportedMimeTypes") {
                const char* m[] = {"audio/mpeg", "audio/flac", "audio/ogg", "audio/opus", "audio/mp4", "audio/x-wav", nullptr};
                return g_variant_new_strv(m, -1);
            }
            return nullptr;
        }
        if (name == "PlaybackStatus") return g_variant_new_string(state.status.c_str());
        if (name == "LoopStatus") return g_variant_new_string(state.loop.c_str());
        if (name == "Rate" || name == "MinimumRate" || name == "MaximumRate") return g_variant_new_double(1.0);
        if (name == "Shuffle") return g_variant_new_boolean(state.shuffle);
        if (name == "Metadata") return metadata();
        if (name == "Volume") return g_variant_new_double(state.volume);
        if (name == "Position") return g_variant_new_int64(static_cast<gint64>(currentPosition() * 1e6));
        if (name == "CanGoNext") return g_variant_new_boolean(state.canNext);
        if (name == "CanGoPrevious") return g_variant_new_boolean(state.canPrevious);
        if (name == "CanPlay" || name == "CanPause" || name == "CanSeek") return g_variant_new_boolean(!state.trackPath.empty());
        if (name == "CanControl") return g_variant_new_boolean(TRUE);
        return nullptr;
    }

    void emitChanged(const std::vector<std::string>& names) {
        GDBusConnection* conn = bus.load();
        if (!conn || names.empty()) return;
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
        for (const auto& n : names) {
            if (GVariant* v = property("org.mpris.MediaPlayer2.Player", n)) g_variant_builder_add(&b, "{sv}", n.c_str(), v);
        }
        g_dbus_connection_emit_signal(conn, nullptr, kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                      g_variant_new("(sa{sv}as)", "org.mpris.MediaPlayer2.Player", &b, nullptr), nullptr);
    }

    static void onMethod(GDBusConnection*, const char*, const char*, const char*, const char* method, GVariant* params,
                         GDBusMethodInvocation* invocation, gpointer data) {
        auto* self = static_cast<Impl*>(data);
        const std::string m = method;
        const Handlers& h = self->handlers;
        if (m == "Raise") self->post(h.raise);
        else if (m == "Quit") self->post(h.quit);
        else if (m == "Next") self->post(h.next);
        else if (m == "Previous") self->post(h.previous);
        else if (m == "Pause") self->post(h.pause);
        else if (m == "PlayPause") self->post(h.playPause);
        else if (m == "Stop") self->post(h.stop);
        else if (m == "Play") self->post(h.play);
        else if (m == "Seek") {
            gint64 offset = 0;
            g_variant_get(params, "(x)", &offset);
            if (h.seekBy) self->app.post([fn = h.seekBy, offset] { fn(static_cast<double>(offset) / 1e6); });
        } else if (m == "SetPosition") {
            const char* id = nullptr;
            gint64 pos = 0;
            g_variant_get(params, "(&ox)", &id, &pos);
            bool same = false;
            {
                std::scoped_lock lock(self->mutex);
                same = trackId(self->state.trackPath) == id;
            }
            // The spec says to ignore positions for a track that is no longer current.
            if (same && h.setPosition) self->app.post([fn = h.setPosition, pos] { fn(static_cast<double>(pos) / 1e6); });
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }

    static GVariant* onGet(GDBusConnection*, const char*, const char*, const char* iface, const char* name, GError** error, gpointer data) {
        GVariant* v = static_cast<Impl*>(data)->property(iface, name);
        if (!v) g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", name);
        return v;
    }

    static gboolean onSet(GDBusConnection*, const char*, const char*, const char*, const char* name, GVariant* value, GError**,
                          gpointer data) {
        auto* self = static_cast<Impl*>(data);
        const std::string n = name;
        const Handlers& h = self->handlers;
        if (n == "Volume" && h.setVolume) {
            const double v = g_variant_get_double(value);
            self->app.post([fn = h.setVolume, v] { fn(std::max(0.0, std::min(1.0, v))); });
        } else if (n == "Shuffle" && h.setShuffle) {
            const bool on = g_variant_get_boolean(value) != FALSE;
            self->app.post([fn = h.setShuffle, on] { fn(on); });
        } else if (n == "LoopStatus" && h.setLoop) {
            std::string s = g_variant_get_string(value, nullptr);
            self->app.post([fn = h.setLoop, s] { fn(s); });
        }
        return TRUE;
    }

    void run() {
        g_main_context_push_thread_default(context);
        GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        info = g_dbus_node_info_new_for_xml(kXml, nullptr);
        if (conn && info) {
            static const GDBusInterfaceVTable vtable = {onMethod, onGet, onSet, {}};
            for (GDBusInterfaceInfo** i = info->interfaces; *i; ++i)
                registrations.push_back(g_dbus_connection_register_object(conn, kPath, *i, &vtable, this, nullptr, nullptr));
            // A second instance would fail to own the name; it then simply isn't controllable.
            owner = g_bus_own_name_on_connection(conn, "org.mpris.MediaPlayer2.mayonnaise_music", G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                                 nullptr, nullptr, nullptr, nullptr);
            bus = conn;
        }
        g_main_loop_run(loop);
        bus = nullptr;
        if (owner) g_bus_unown_name(owner);
        for (guint r : registrations) {
            if (r) g_dbus_connection_unregister_object(conn, r);
        }
        if (conn) {
            g_dbus_connection_flush_sync(conn, nullptr, nullptr);
            g_object_unref(conn);
        }
        if (info) g_dbus_node_info_unref(info);
        // Unregistering queues "done" callbacks on this context; run them so nothing keeps it alive.
        for (int i = 0; i < 100 && g_main_context_iteration(context, FALSE); ++i) {
        }
        g_main_context_pop_thread_default(context);
    }
};

Mpris::Mpris(mayo::App& app, const std::string& desktopId, Handlers handlers)
    : m_impl(std::make_unique<Impl>(app, desktopId, std::move(handlers))) {
    m_impl->context = g_main_context_new();
    m_impl->loop = g_main_loop_new(m_impl->context, FALSE);
    m_impl->thread = std::thread([impl = m_impl.get()] { impl->run(); });
}

Mpris::~Mpris() {
    g_main_context_invoke(m_impl->context, [](gpointer loop) -> gboolean {
        g_main_loop_quit(static_cast<GMainLoop*>(loop));
        return G_SOURCE_REMOVE;
    }, m_impl->loop);
    if (m_impl->thread.joinable()) m_impl->thread.join();
    g_main_loop_unref(m_impl->loop);
    g_main_context_unref(m_impl->context);
}

void Mpris::update(const State& s) {
    std::vector<std::string> changed;
    {
        std::scoped_lock lock(m_impl->mutex);
        State& o = m_impl->state;
        if (o == s) return;
        if (o.status != s.status) {
            // Freeze the extrapolated position at the moment playback stops or starts.
            m_impl->position = m_impl->currentPosition();
            m_impl->positionTime = std::chrono::steady_clock::now();
            changed.emplace_back("PlaybackStatus");
        }
        if (o.trackPath != s.trackPath || o.title != s.title || o.artist != s.artist || o.album != s.album || o.artUrl != s.artUrl ||
            o.length != s.length)
            changed.emplace_back("Metadata");
        if (o.volume != s.volume) changed.emplace_back("Volume");
        if (o.shuffle != s.shuffle) changed.emplace_back("Shuffle");
        if (o.loop != s.loop) changed.emplace_back("LoopStatus");
        if (o.canNext != s.canNext) changed.emplace_back("CanGoNext");
        if (o.canPrevious != s.canPrevious) changed.emplace_back("CanGoPrevious");
        if (o.trackPath.empty() != s.trackPath.empty()) {
            changed.emplace_back("CanPlay");
            changed.emplace_back("CanPause");
            changed.emplace_back("CanSeek");
        }
        o = s;
    }
    m_impl->emitChanged(changed);
}

void Mpris::setPosition(double seconds) {
    std::scoped_lock lock(m_impl->mutex);
    m_impl->position = seconds;
    m_impl->positionTime = std::chrono::steady_clock::now();
}

void Mpris::seeked(double seconds) {
    setPosition(seconds);
    if (GDBusConnection* conn = m_impl->bus.load())
        g_dbus_connection_emit_signal(conn, nullptr, kPath, "org.mpris.MediaPlayer2.Player", "Seeked",
                                      g_variant_new("(x)", static_cast<gint64>(seconds * 1e6)), nullptr);
}

#else

struct Mpris::Impl {};
Mpris::Mpris(mayo::App& /*app*/, const std::string& /*desktopId*/, Handlers /*handlers*/) {}
Mpris::~Mpris() = default;
void Mpris::update(const State& /*state*/) {}
void Mpris::setPosition(double /*seconds*/) {}
void Mpris::seeked(double /*seconds*/) {}

#endif
