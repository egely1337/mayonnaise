#include "glib_support.hpp"

#include <gio/gdesktopappinfo.h>

#include <mayonnaise/app.hpp>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>

namespace gx {

namespace {

std::atomic<mayo::App*> g_app{nullptr};
std::atomic<bool> g_readOnly{false};
GMainLoop* g_loop = nullptr;
std::thread g_thread;

// Background worker.
std::mutex g_workMutex;
std::condition_variable g_workCv;
std::deque<std::function<void()>> g_work;
std::thread g_worker;
bool g_workStop = false;

// Streams are started on the UI thread and killed from it in stop(); the list is only touched there.
std::vector<GSubprocess*> g_streams;

struct ErrorFree {
    void operator()(GError* e) const {
        if (e) g_error_free(e);
    }
};
using Error = std::unique_ptr<GError, ErrorFree>;

std::string message(GError* e) {
    if (!e) return {};
    if (g_dbus_error_is_remote_error(e)) g_dbus_error_strip_remote_error(e);
    return e->message ? e->message : "Unknown error";
}

// GSettings ---------------------------------------------------------------------------------------

struct WatchKey {
    GSettings* settings;
    std::string key;
    bool operator<(const WatchKey& o) const { return settings != o.settings ? settings < o.settings : key < o.key; }
};

std::map<std::string, GSettings*>& settingsCache() {
    static std::map<std::string, GSettings*> cache;
    return cache;
}
std::multimap<WatchKey, std::function<void()>>& watchers() {
    static std::multimap<WatchKey, std::function<void()>> w;
    return w;
}

void onSettingsChanged(GSettings* settings, const char* key, gpointer /*data*/) {
    toUi([settings, k = std::string(key)] {
        auto range = watchers().equal_range({settings, k});
        // Copy first: a watcher may add another watcher (rebuilding a list).
        std::vector<std::function<void()>> fns;
        for (auto it = range.first; it != range.second; ++it) fns.push_back(it->second);
        for (auto& fn : fns) fn();
    });
}

GSettingsSchema* lookupSchema(const std::string& schema) {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    return source ? g_settings_schema_source_lookup(source, schema.c_str(), TRUE) : nullptr;
}

GSettings* settingsFor(const std::string& schema, const std::string& path) {
    auto& cache = settingsCache();
    const std::string id = schema + "|" + path;
    if (auto it = cache.find(id); it != cache.end()) return it->second;
    GSettings* s = nullptr;
    if (GSettingsSchema* sc = lookupSchema(schema)) {
        // A relocatable schema needs a path, a fixed one must not get one.
        const char* fixed = g_settings_schema_get_path(sc);
        if (fixed || !path.empty()) {
            s = g_settings_new_full(sc, nullptr, fixed ? nullptr : path.c_str());
            g_signal_connect(s, "changed", G_CALLBACK(onSettingsChanged), nullptr);
        }
        g_settings_schema_unref(sc);
    }
    cache.emplace(id, s);
    return s;
}

struct SchemaKey {
    GSettingsSchemaKey* key = nullptr;
    SchemaKey(GSettings* s, const std::string& name) {
        GSettingsSchema* schema = nullptr;
        g_object_get(s, "settings-schema", &schema, nullptr);
        if (schema) {
            if (g_settings_schema_has_key(schema, name.c_str())) key = g_settings_schema_get_key(schema, name.c_str());
            g_settings_schema_unref(schema);
        }
    }
    ~SchemaKey() {
        if (key) g_settings_schema_key_unref(key);
    }
    SchemaKey(const SchemaKey&) = delete;
    SchemaKey& operator=(const SchemaKey&) = delete;
};

// D-Bus ----------------------------------------------------------------------------------------------

struct CallData {
    Reply done;
};

void onCallDone(GObject* source, GAsyncResult* res, gpointer data) {
    std::unique_ptr<CallData> cd(static_cast<CallData*>(data));
    GError* raw = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &raw);
    Error err(raw);
    if (!cd->done) {
        if (reply) g_variant_unref(reply);
        return;
    }
    toUi([done = std::move(cd->done), v = std::make_shared<Variant>(reply), e = message(err.get())]() mutable {
        done(std::move(*v), e);
    });
}

void callImpl(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* method,
              GVariant* params, Reply done, int timeoutMs, GDBusCallFlags flags) {
    if (!bus) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        if (done) toUi([done] { done(nullptr, "The message bus is not available."); });
        return;
    }
    g_dbus_connection_call(bus, name, path, iface, method, params, nullptr, flags, timeoutMs, nullptr, onCallDone,
                           new CallData{std::move(done)});
}

void onSignal(GDBusConnection* /*bus*/, const char* /*sender*/, const char* path, const char* /*iface*/,
              const char* member, GVariant* params, gpointer data) {
    auto* fn = static_cast<SignalFn*>(data);
    toUi([fn, p = std::string(path ? path : ""), m = std::string(member ? member : ""),
          v = std::make_shared<Variant>(params ? g_variant_ref(params) : nullptr)] { (*fn)(p, m, v->get()); });
}

// Subprocesses ------------------------------------------------------------------------------------

GSubprocessLauncher* launcher(GSubprocessFlags flags) {
    GSubprocessLauncher* l = g_subprocess_launcher_new(flags);
    // Parsing relies on untranslated output; pactl's JSON writer also chokes on some locales.
    g_subprocess_launcher_setenv(l, "LC_ALL", "C.UTF-8", TRUE);
    return l;
}

std::vector<const char*> cArgv(const std::vector<std::string>& argv) {
    std::vector<const char*> out;
    for (const auto& a : argv) out.push_back(a.c_str());
    out.push_back(nullptr);
    return out;
}

GSubprocess* spawn(const std::vector<std::string>& argv, GSubprocessFlags flags, std::string& error) {
    Object<GSubprocessLauncher> l(launcher(flags));
    auto args = cArgv(argv);
    GError* raw = nullptr;
    GSubprocess* p = g_subprocess_launcher_spawnv(l.get(), args.data(), &raw);
    Error err(raw);
    if (!p) error = message(err.get());
    return p;
}

std::string bytesToString(GBytes* b) {
    if (!b) return {};
    gsize n = 0;
    const auto* data = static_cast<const char*>(g_bytes_get_data(b, &n));
    std::string s(data ? data : "", n);
    g_bytes_unref(b);
    return s;
}

int exitStatus(GSubprocess* p) { return g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : 128; }

struct RunData {
    std::function<void(ProcessResult)> done;
};

void onRunDone(GObject* source, GAsyncResult* res, gpointer data) {
    std::unique_ptr<RunData> rd(static_cast<RunData*>(data));
    auto* p = G_SUBPROCESS(source);
    GBytes* out = nullptr;
    GBytes* errOut = nullptr;
    GError* raw = nullptr;
    g_subprocess_communicate_finish(p, res, &out, &errOut, &raw);
    Error err(raw);
    ProcessResult r;
    r.out = bytesToString(out);
    r.err = bytesToString(errOut);
    r.status = err ? -1 : exitStatus(p);
    if (err && r.err.empty()) r.err = message(err.get());
    g_object_unref(p);
    if (rd->done) toUi([done = std::move(rd->done), r = std::move(r)] { done(r); });
}

struct StreamData {
    GDataInputStream* in;
    std::function<void(const std::string&)> onLine;
};

void readNextLine(StreamData* sd);

void onLine(GObject* source, GAsyncResult* res, gpointer data) {
    auto* sd = static_cast<StreamData*>(data);
    gsize len = 0;
    char* line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(source), res, &len, nullptr);
    if (!line) {  // end of stream or error: the process is gone
        g_object_unref(sd->in);
        delete sd;
        return;
    }
    toUi([fn = sd->onLine, s = std::string(line, len)] { fn(s); });
    g_free(line);
    readNextLine(sd);
}

void readNextLine(StreamData* sd) {
    g_data_input_stream_read_line_async(sd->in, G_PRIORITY_DEFAULT, nullptr, onLine, sd);
}

}  // namespace

// Lifetime ---------------------------------------------------------------------------------------------

void start(mayo::App& app) {
    g_app = &app;
    g_loop = g_main_loop_new(nullptr, FALSE);
    g_thread = std::thread([] { g_main_loop_run(g_loop); });
}

void stop() {
    {
        std::lock_guard lock(g_workMutex);
        g_workStop = true;
        g_work.clear();
    }
    g_workCv.notify_all();
    if (g_worker.joinable()) g_worker.join();
    for (GSubprocess* p : g_streams) {
        g_subprocess_force_exit(p);
        g_object_unref(p);
    }
    g_streams.clear();
    // Flush pending dconf writes before the process exits.
    g_settings_sync();
    g_app = nullptr;
    if (g_loop) {
        g_main_loop_quit(g_loop);
        if (g_thread.joinable()) g_thread.join();
        g_main_loop_unref(g_loop);
        g_loop = nullptr;
    }
    watchers().clear();
}

void toUi(std::function<void()> fn) {
    if (mayo::App* app = g_app.load()) app->post(std::move(fn));
}

void setReadOnly(bool readOnly) { g_readOnly = readOnly; }
bool readOnly() { return g_readOnly; }

// GSettings ------------------------------------------------------------------------------------------

bool hasSchema(const std::string& schema) {
    GSettingsSchema* s = lookupSchema(schema);
    if (s) g_settings_schema_unref(s);
    return s != nullptr;
}

Key::Key(const std::string& schema, std::string key, const std::string& path) : m_key(std::move(key)) {
    GSettings* s = settingsFor(schema, path);
    if (s && SchemaKey(s, m_key).key) m_settings = s;
}

std::string Key::type() const {
    if (!m_settings) return {};
    SchemaKey k(m_settings, m_key);
    return k.key ? g_variant_type_peek_string(g_settings_schema_key_get_value_type(k.key)) : std::string();
}

Variant Key::value() const { return m_settings ? Variant(g_settings_get_value(m_settings, m_key.c_str())) : nullptr; }

bool Key::getBool(bool fallback) const {
    auto v = value();
    return v && g_variant_is_of_type(v.get(), G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(v.get()) : fallback;
}

std::int64_t Key::getInt(std::int64_t fallback) const {
    auto v = value();
    if (!v) return fallback;
    switch (g_variant_classify(v.get())) {
        case G_VARIANT_CLASS_INT32: return g_variant_get_int32(v.get());
        case G_VARIANT_CLASS_UINT32: return g_variant_get_uint32(v.get());
        case G_VARIANT_CLASS_INT64: return g_variant_get_int64(v.get());
        case G_VARIANT_CLASS_UINT64: return static_cast<std::int64_t>(g_variant_get_uint64(v.get()));
        case G_VARIANT_CLASS_INT16: return g_variant_get_int16(v.get());
        case G_VARIANT_CLASS_UINT16: return g_variant_get_uint16(v.get());
        case G_VARIANT_CLASS_BYTE: return g_variant_get_byte(v.get());
        case G_VARIANT_CLASS_DOUBLE: return std::llround(g_variant_get_double(v.get()));
        default: return fallback;
    }
}

double Key::getDouble(double fallback) const {
    auto v = value();
    if (!v) return fallback;
    if (g_variant_is_of_type(v.get(), G_VARIANT_TYPE_DOUBLE)) return g_variant_get_double(v.get());
    return static_cast<double>(getInt(static_cast<std::int64_t>(fallback)));
}

std::string Key::getString(const std::string& fallback) const {
    auto v = value();
    return v && g_variant_is_of_type(v.get(), G_VARIANT_TYPE_STRING) ? g_variant_get_string(v.get(), nullptr)
                                                                      : fallback;
}

std::vector<std::string> Key::getStrv() const {
    auto v = value();
    return v ? strv(v.get()) : std::vector<std::string>{};
}

std::string Key::getText() const {
    auto v = value();
    if (!v) return {};
    switch (g_variant_classify(v.get())) {
        case G_VARIANT_CLASS_STRING: return g_variant_get_string(v.get(), nullptr);
        case G_VARIANT_CLASS_BOOLEAN: return g_variant_get_boolean(v.get()) ? "true" : "false";
        case G_VARIANT_CLASS_DOUBLE: {
            char buf[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(buf, sizeof buf, "%g", g_variant_get_double(v.get()));
            return buf;
        }
        default: return std::to_string(getInt());
    }
}

void Key::setValue(GVariant* v) const {
    Variant held = hold(v);
    if (!m_settings || !held || g_readOnly) return;
    g_settings_set_value(m_settings, m_key.c_str(), held.get());
}

void Key::set(bool v) const { setValue(g_variant_new_boolean(v)); }

void Key::setInt(std::int64_t v) const {
    const std::string t = type();
    if (t == "i") setValue(g_variant_new_int32(static_cast<gint32>(v)));
    else if (t == "u") setValue(g_variant_new_uint32(static_cast<guint32>(std::max<std::int64_t>(0, v))));
    else if (t == "x") setValue(g_variant_new_int64(v));
    else if (t == "t") setValue(g_variant_new_uint64(static_cast<guint64>(std::max<std::int64_t>(0, v))));
    else if (t == "n") setValue(g_variant_new_int16(static_cast<gint16>(v)));
    else if (t == "q") setValue(g_variant_new_uint16(static_cast<guint16>(v)));
    else if (t == "y") setValue(g_variant_new_byte(static_cast<guchar>(v)));
    else if (t == "d") setValue(g_variant_new_double(static_cast<double>(v)));
}

void Key::setDouble(double v) const {
    if (type() == "d") setValue(g_variant_new_double(v));
    else setInt(std::llround(v));
}

void Key::setString(const std::string& v) const { setValue(g_variant_new_string(v.c_str())); }

void Key::setStrv(const std::vector<std::string>& v) const {
    std::vector<const char*> c;
    for (const auto& s : v) c.push_back(s.c_str());
    setValue(g_variant_new_strv(c.data(), static_cast<gssize>(c.size())));
}

void Key::setText(const std::string& text) const {
    const std::string t = type();
    if (t == "s") setString(text);
    else if (t == "b") set(text == "true");
    else if (t == "d") setDouble(g_ascii_strtod(text.c_str(), nullptr));
    else if (!t.empty()) setInt(std::strtoll(text.c_str(), nullptr, 10));
}

void Key::reset() const {
    if (m_settings && !g_readOnly) g_settings_reset(m_settings, m_key.c_str());
}

bool Key::isDefault() const {
    if (!m_settings) return true;
    Variant user(g_settings_get_user_value(m_settings, m_key.c_str()));
    return !user;
}

std::optional<std::pair<double, double>> Key::range() const {
    if (!m_settings) return std::nullopt;
    SchemaKey k(m_settings, m_key);
    if (!k.key) return std::nullopt;
    Variant r(g_settings_schema_key_get_range(k.key));
    const char* kind = nullptr;
    GVariant* detail = nullptr;
    g_variant_get(r.get(), "(&sv)", &kind, &detail);
    Variant d(detail);
    if (std::string(kind) != "range") return std::nullopt;
    Variant lo(g_variant_get_child_value(d.get(), 0));
    Variant hi(g_variant_get_child_value(d.get(), 1));
    auto num = [](GVariant* v) -> double {
        switch (g_variant_classify(v)) {
            case G_VARIANT_CLASS_DOUBLE: return g_variant_get_double(v);
            case G_VARIANT_CLASS_INT32: return g_variant_get_int32(v);
            case G_VARIANT_CLASS_UINT32: return g_variant_get_uint32(v);
            case G_VARIANT_CLASS_INT64: return static_cast<double>(g_variant_get_int64(v));
            case G_VARIANT_CLASS_UINT64: return static_cast<double>(g_variant_get_uint64(v));
            default: return 0;
        }
    };
    return std::pair{num(lo.get()), num(hi.get())};
}

std::vector<std::string> Key::choices() const {
    if (!m_settings) return {};
    SchemaKey k(m_settings, m_key);
    if (!k.key) return {};
    Variant r(g_settings_schema_key_get_range(k.key));
    const char* kind = nullptr;
    GVariant* detail = nullptr;
    g_variant_get(r.get(), "(&sv)", &kind, &detail);
    Variant d(detail);
    const std::string type = kind;
    return type == "enum" || type == "flags" ? strv(d.get()) : std::vector<std::string>{};
}

void Key::watch(std::function<void()> fn) const {
    if (m_settings) watchers().emplace(WatchKey{m_settings, m_key}, std::move(fn));
}

// D-Bus ----------------------------------------------------------------------------------------------

GDBusConnection* systemBus() {
    static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    return bus;
}

GDBusConnection* sessionBus() {
    static GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    return bus;
}

bool hasName(GDBusConnection* bus, const char* name) {
    auto owned = callSync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "NameHasOwner", g_variant_new("(s)", name), "(b)");
    if (owned) {
        gboolean b = FALSE;
        g_variant_get(owned.get(), "(b)", &b);
        if (b) return true;
    }
    auto activatable = callSync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                "ListActivatableNames", nullptr, "(as)");
    if (!activatable) return false;
    Variant list(g_variant_get_child_value(activatable.get(), 0));
    for (const auto& n : strv(list.get())) {
        if (n == name) return true;
    }
    return false;
}

Variant callSync(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* method,
                 GVariant* params, const char* replyType, std::string* error, int timeoutMs) {
    if (!bus) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        if (error) *error = "The message bus is not available.";
        return nullptr;
    }
    GError* raw = nullptr;
    GVariant* r = g_dbus_connection_call_sync(bus, name, path, iface, method, params,
                                              replyType ? G_VARIANT_TYPE(replyType) : nullptr,
                                              G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, &raw);
    Error err(raw);
    if (error) *error = message(err.get());
    return Variant(r);
}

Variant property(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* prop) {
    auto r = callSync(bus, name, path, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", iface, prop),
                      "(v)", nullptr, 1500);
    if (!r) return nullptr;
    GVariant* inner = nullptr;
    g_variant_get(r.get(), "(v)", &inner);
    return Variant(inner);
}

Variant properties(GDBusConnection* bus, const char* name, const char* path, const char* iface) {
    auto r = callSync(bus, name, path, "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", iface),
                      "(a{sv})", nullptr, 1500);
    return r ? Variant(g_variant_get_child_value(r.get(), 0)) : nullptr;
}

void call(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* method,
          GVariant* params, Reply done, int timeoutMs) {
    callImpl(bus, name, path, iface, method, params, std::move(done), timeoutMs, G_DBUS_CALL_FLAGS_NONE);
}

void perform(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* method,
             GVariant* params, Reply done, int timeoutMs) {
    if (g_readOnly) {
        if (params) g_variant_unref(g_variant_ref_sink(params));
        if (done) toUi([done] { done(nullptr, {}); });
        return;
    }
    callImpl(bus, name, path, iface, method, params, std::move(done), timeoutMs,
             G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION);
}

void setProperty(GDBusConnection* bus, const char* name, const char* path, const char* iface, const char* prop,
                 GVariant* value, Reply done) {
    perform(bus, name, path, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", iface, prop, value),
            std::move(done));
}

void subscribe(GDBusConnection* bus, const char* sender, const char* iface, const char* member, const char* path,
               SignalFn fn) {
    if (!bus) return;
    g_dbus_connection_signal_subscribe(
        bus, sender, iface, member, path, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, onSignal, new SignalFn(std::move(fn)),
        [](gpointer p) { delete static_cast<SignalFn*>(p); });
}

// Variant helpers --------------------------------------------------------------------------------------

std::string str(GVariant* v) {
    if (!v) return {};
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT)) {
        Variant inner(g_variant_get_variant(v));
        return str(inner.get());
    }
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH))
        return g_variant_get_string(v, nullptr);
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) return g_variant_get_bytestring(v);
    return {};
}

Variant lookup(GVariant* dict, const char* key) {
    if (!dict || !g_variant_is_of_type(dict, G_VARIANT_TYPE("a{sv}"))) return nullptr;
    return Variant(g_variant_lookup_value(dict, key, nullptr));
}

std::string lookupString(GVariant* dict, const char* key, const std::string& fallback) {
    auto v = lookup(dict, key);
    return v ? str(v.get()) : fallback;
}

bool lookupBool(GVariant* dict, const char* key, bool fallback) {
    auto v = lookup(dict, key);
    return v && g_variant_is_of_type(v.get(), G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(v.get()) : fallback;
}

double lookupDouble(GVariant* dict, const char* key, double fallback) {
    auto v = lookup(dict, key);
    if (!v) return fallback;
    if (g_variant_is_of_type(v.get(), G_VARIANT_TYPE_DOUBLE)) return g_variant_get_double(v.get());
    return static_cast<double>(lookupInt(dict, key, static_cast<std::int64_t>(fallback)));
}

std::int64_t lookupInt(GVariant* dict, const char* key, std::int64_t fallback) {
    auto v = lookup(dict, key);
    if (!v) return fallback;
    switch (g_variant_classify(v.get())) {
        case G_VARIANT_CLASS_INT32: return g_variant_get_int32(v.get());
        case G_VARIANT_CLASS_UINT32: return g_variant_get_uint32(v.get());
        case G_VARIANT_CLASS_INT64: return g_variant_get_int64(v.get());
        case G_VARIANT_CLASS_UINT64: return static_cast<std::int64_t>(g_variant_get_uint64(v.get()));
        case G_VARIANT_CLASS_INT16: return g_variant_get_int16(v.get());
        case G_VARIANT_CLASS_UINT16: return g_variant_get_uint16(v.get());
        case G_VARIANT_CLASS_BYTE: return g_variant_get_byte(v.get());
        case G_VARIANT_CLASS_DOUBLE: return std::llround(g_variant_get_double(v.get()));
        default: return fallback;
    }
}

std::vector<std::string> strv(GVariant* v) {
    std::vector<std::string> out;
    if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE_STRING_ARRAY)) return out;
    gsize n = 0;
    const char** items = g_variant_get_strv(v, &n);
    for (gsize i = 0; i < n; ++i) out.emplace_back(items[i]);
    g_free(static_cast<gpointer>(items));
    return out;
}

// Desktop entries -------------------------------------------------------------------------------------

std::string appName(const std::string& desktopId) {
    std::string id = desktopId;
    if (id.size() < 8 || id.compare(id.size() - 8, 8, ".desktop") != 0) id += ".desktop";
    Object<GDesktopAppInfo> info(g_desktop_app_info_new(id.c_str()));
    if (!info) return {};
    const char* name = g_app_info_get_display_name(G_APP_INFO(info.get()));
    return name ? name : std::string();
}

// Subprocesses ---------------------------------------------------------------------------------------

bool haveProgram(const char* name) {
    char* p = g_find_program_in_path(name);
    g_free(p);
    return p != nullptr;
}

ProcessResult runSync(const std::vector<std::string>& argv) {
    ProcessResult r;
    std::string error;
    GSubprocess* p = spawn(argv, static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
                           error);
    if (!p) {
        r.err = error;
        return r;
    }
    GBytes* out = nullptr;
    GBytes* errOut = nullptr;
    GError* raw = nullptr;
    g_subprocess_communicate(p, nullptr, nullptr, &out, &errOut, &raw);
    Error err(raw);
    r.out = bytesToString(out);
    r.err = bytesToString(errOut);
    r.status = err ? -1 : exitStatus(p);
    g_object_unref(p);
    return r;
}

void run(const std::vector<std::string>& argv, std::function<void(ProcessResult)> done, bool mutates) {
    if (mutates && g_readOnly) {
        if (done) toUi([done] { done(ProcessResult{0, {}, {}}); });
        return;
    }
    std::string error;
    GSubprocess* p = spawn(argv, static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
                           error);
    if (!p) {
        if (done) toUi([done, error] { done(ProcessResult{-1, {}, error}); });
        return;
    }
    g_subprocess_communicate_async(p, nullptr, nullptr, onRunDone, new RunData{std::move(done)});
}

void stream(const std::vector<std::string>& argv, std::function<void(const std::string& line)> onLineFn) {
    std::string error;
    GSubprocess* p = spawn(argv, static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
                           error);
    if (!p) return;
    g_streams.push_back(p);
    auto* sd = new StreamData{g_data_input_stream_new(g_subprocess_get_stdout_pipe(p)), std::move(onLineFn)};
    readNextLine(sd);
}

void background(std::function<void()> work) {
    std::lock_guard lock(g_workMutex);
    if (g_workStop) return;
    g_work.push_back(std::move(work));
    if (!g_worker.joinable()) {
        g_worker = std::thread([] {
            while (true) {
                std::function<void()> job;
                {
                    std::unique_lock wait(g_workMutex);
                    g_workCv.wait(wait, [] { return g_workStop || !g_work.empty(); });
                    if (g_workStop) return;
                    job = std::move(g_work.front());
                    g_work.pop_front();
                }
                job();
            }
        });
    }
    g_workCv.notify_one();
}

// Debounce ---------------------------------------------------------------------------------------------

struct Debounce::State {
    double delay;
    std::function<void()> fn;
    bool pending = false;
};

Debounce::Debounce(double delay, std::function<void()> fn)
    : m_state(std::make_shared<State>(State{delay, std::move(fn)})) {}

void Debounce::operator()() {
    if (m_state->pending) return;
    m_state->pending = true;
    mayo::App::current().after(m_state->delay, [s = m_state] {
        s->pending = false;
        s->fn();
    });
}

}  // namespace gx
