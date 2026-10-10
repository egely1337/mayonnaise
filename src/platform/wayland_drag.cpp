#include "wayland_drag.hpp"

#if defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#endif

namespace mayo::platform::wayland {

#if !defined(__linux__)

bool startFileDrag(void*, void*, const std::vector<std::string>&, std::function<void()>) { return false; }
void dispatch() {}
bool dragActive() { return false; }
void prepare(void*) {}

#else

namespace {

// Only opaque pointers are needed; the protocol is driven through wl_proxy_marshal_flags with the opcodes and
// event orders of wayland.xml (core protocol, stable since 2012).
struct wl_display;
struct wl_proxy;
struct wl_event_queue;
struct wl_interface;

using MarshalFlags = wl_proxy* (*)(wl_proxy*, std::uint32_t, const wl_interface*, std::uint32_t, std::uint32_t, ...);

struct Lib {
    bool ok = false;
    wl_event_queue* (*createQueue)(wl_display*) = nullptr;
    int (*dispatchQueuePending)(wl_display*, wl_event_queue*) = nullptr;
    int (*roundtripQueue)(wl_display*, wl_event_queue*) = nullptr;
    int (*flush)(wl_display*) = nullptr;
    MarshalFlags marshal = nullptr;
    int (*addListener)(wl_proxy*, void (**)(void), void*) = nullptr;
    void (*setQueue)(wl_proxy*, wl_event_queue*) = nullptr;
    std::uint32_t (*getVersion)(wl_proxy*) = nullptr;
    void* (*createWrapper)(void*) = nullptr;
    void (*wrapperDestroy)(void*) = nullptr;
    const wl_interface* registryIface = nullptr;
    const wl_interface* seatIface = nullptr;
    const wl_interface* pointerIface = nullptr;
    const wl_interface* managerIface = nullptr;
    const wl_interface* deviceIface = nullptr;
    const wl_interface* sourceIface = nullptr;
    const wl_interface* offerIface = nullptr;
};

Lib& lib() {
    static Lib l = [] {
        Lib r;
        void* h = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) return r;
        auto sym = [&](auto& out, const char* name) {
            out = reinterpret_cast<std::remove_reference_t<decltype(out)>>(dlsym(h, name));
            return out != nullptr;
        };
        r.ok = sym(r.createQueue, "wl_display_create_queue") && sym(r.dispatchQueuePending, "wl_display_dispatch_queue_pending") &&
               sym(r.roundtripQueue, "wl_display_roundtrip_queue") && sym(r.flush, "wl_display_flush") &&
               sym(r.marshal, "wl_proxy_marshal_flags") && sym(r.addListener, "wl_proxy_add_listener") &&
               sym(r.setQueue, "wl_proxy_set_queue") && sym(r.getVersion, "wl_proxy_get_version") &&
               sym(r.createWrapper, "wl_proxy_create_wrapper") && sym(r.wrapperDestroy, "wl_proxy_wrapper_destroy") &&
               sym(r.registryIface, "wl_registry_interface") && sym(r.seatIface, "wl_seat_interface") &&
               sym(r.pointerIface, "wl_pointer_interface") && sym(r.managerIface, "wl_data_device_manager_interface") &&
               sym(r.deviceIface, "wl_data_device_interface") && sym(r.sourceIface, "wl_data_source_interface") &&
               sym(r.offerIface, "wl_data_offer_interface");
        return r;
    }();
    return l;
}

// Opcodes (requests) from wayland.xml.
constexpr std::uint32_t kDisplayGetRegistry = 1;
constexpr std::uint32_t kRegistryBind = 0;
constexpr std::uint32_t kSeatGetPointer = 0;
constexpr std::uint32_t kManagerCreateSource = 0;
constexpr std::uint32_t kManagerGetDevice = 1;
constexpr std::uint32_t kSourceOffer = 0;
constexpr std::uint32_t kSourceDestroy = 1;
constexpr std::uint32_t kSourceSetActions = 2;
constexpr std::uint32_t kDeviceStartDrag = 0;
constexpr std::uint32_t kOfferDestroy = 2;
constexpr std::uint32_t kMarshalDestroy = 1;  // WL_MARSHAL_FLAG_DESTROY
constexpr std::uint32_t kActionCopy = 1;
constexpr std::uint32_t kActionMove = 2;

struct State {
    wl_display* display = nullptr;
    wl_event_queue* queue = nullptr;
    wl_proxy* registry = nullptr;
    wl_proxy* seat = nullptr;
    wl_proxy* pointer = nullptr;
    wl_proxy* manager = nullptr;
    wl_proxy* device = nullptr;
    std::uint32_t pressSerial = 0;  // last button press on our surfaces
    bool failed = false;

    wl_proxy* source = nullptr;
    std::string uriList;
    std::string plain;
    std::function<void()> ended;
};

State g;

/// A request whose only argument is the new object's id.
wl_proxy* create(wl_proxy* p, std::uint32_t opcode, const wl_interface* iface) {
    return lib().marshal(p, opcode, iface, lib().getVersion(p), 0, nullptr);
}

std::string percentEncode(const std::string& path) {
    std::string out;
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr("/-_.~", c)) {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

void finishDrag(bool destroySource) {
    if (g.ended) {
        auto fn = std::move(g.ended);
        g.ended = nullptr;
        fn();
    }
    if (destroySource && g.source) {
        lib().marshal(g.source, kSourceDestroy, nullptr, lib().getVersion(g.source), kMarshalDestroy);
        g.source = nullptr;
        lib().flush(g.display);
    }
}

// Listeners ------------------------------------------------------------------------------------------------

void noop() {}

void registryGlobal(void* /*data*/, wl_proxy* registry, std::uint32_t name, const char* iface, std::uint32_t version) {
    auto bind = [&](const wl_interface* interface, const char* ifaceName, std::uint32_t want) {
        const std::uint32_t v = version < want ? version : want;
        return lib().marshal(registry, kRegistryBind, interface, v, 0, name, ifaceName, v, nullptr);
    };
    if (!g.seat && std::strcmp(iface, "wl_seat") == 0) g.seat = bind(lib().seatIface, "wl_seat", 3);
    if (!g.manager && std::strcmp(iface, "wl_data_device_manager") == 0)
        g.manager = bind(lib().managerIface, "wl_data_device_manager", 3);
}
void registryRemove(void*, wl_proxy*, std::uint32_t) {}
void (*kRegistryListener[])(void) = {reinterpret_cast<void (*)()>(registryGlobal), reinterpret_cast<void (*)()>(registryRemove)};

void pointerEnter(void*, wl_proxy*, std::uint32_t, wl_proxy*, std::int32_t, std::int32_t) {}
void pointerLeave(void*, wl_proxy*, std::uint32_t, wl_proxy*) {}
void pointerMotion(void*, wl_proxy*, std::uint32_t, std::int32_t, std::int32_t) {}
void pointerButton(void*, wl_proxy*, std::uint32_t serial, std::uint32_t, std::uint32_t, std::uint32_t state) {
    if (state == 1) g.pressSerial = serial;  // WL_POINTER_BUTTON_STATE_PRESSED
}
void pointerAxis(void*, wl_proxy*, std::uint32_t, std::uint32_t, std::int32_t) {}

void (*kPointerListener[])(void) = {
    reinterpret_cast<void (*)()>(pointerEnter),  reinterpret_cast<void (*)()>(pointerLeave),
    reinterpret_cast<void (*)()>(pointerMotion), reinterpret_cast<void (*)()>(pointerButton),
    reinterpret_cast<void (*)()>(pointerAxis),
    // wl_pointer v5+ events; we bind v3, but keep slots so an unexpected event can't hit a null entry.
    reinterpret_cast<void (*)()>(noop),          reinterpret_cast<void (*)()>(noop),
    reinterpret_cast<void (*)()>(noop),          reinterpret_cast<void (*)()>(noop),
};

void seatCapabilities(void*, wl_proxy* seat, std::uint32_t caps) {
    if ((caps & 1) && !g.pointer) {  // WL_SEAT_CAPABILITY_POINTER
        g.pointer = create(seat, kSeatGetPointer, lib().pointerIface);
        lib().addListener(g.pointer, kPointerListener, nullptr);
    }
}
void seatName(void*, wl_proxy*, const char*) {}
void (*kSeatListener[])(void) = {reinterpret_cast<void (*)()>(seatCapabilities), reinterpret_cast<void (*)()>(seatName)};

void deviceDataOffer(void*, wl_proxy*, wl_proxy* offer) {
    // Drops and the clipboard are SDL's business; we never read offers, so let them go right away.
    if (offer) lib().marshal(offer, kOfferDestroy, nullptr, lib().getVersion(offer), kMarshalDestroy);
}
void deviceEnter(void*, wl_proxy*, std::uint32_t, wl_proxy*, std::int32_t, std::int32_t, wl_proxy*) {}
void deviceLeave(void*, wl_proxy*) {}
void deviceMotion(void*, wl_proxy*, std::uint32_t, std::int32_t, std::int32_t) {}
void deviceDrop(void*, wl_proxy*) {}
void deviceSelection(void*, wl_proxy*, wl_proxy*) {}
void (*kDeviceListener[])(void) = {
    reinterpret_cast<void (*)()>(deviceDataOffer), reinterpret_cast<void (*)()>(deviceEnter),
    reinterpret_cast<void (*)()>(deviceLeave),     reinterpret_cast<void (*)()>(deviceMotion),
    reinterpret_cast<void (*)()>(deviceDrop),      reinterpret_cast<void (*)()>(deviceSelection),
};

void sourceTarget(void*, wl_proxy*, const char*) {}
void sourceSend(void*, wl_proxy*, const char* mime, std::int32_t fd) {
    const std::string& data = std::strcmp(mime, "text/uri-list") == 0 ? g.uriList : g.plain;
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
    ::close(fd);
}
void sourceCancelled(void*, wl_proxy*) { finishDrag(true); }
void sourceDropPerformed(void*, wl_proxy*) { finishDrag(false); }  // data may still be requested
void sourceFinished(void*, wl_proxy*) { finishDrag(true); }
void sourceAction(void*, wl_proxy*, std::uint32_t) {}
void (*kSourceListener[])(void) = {
    reinterpret_cast<void (*)()>(sourceTarget),        reinterpret_cast<void (*)()>(sourceSend),
    reinterpret_cast<void (*)()>(sourceCancelled),     reinterpret_cast<void (*)()>(sourceDropPerformed),
    reinterpret_cast<void (*)()>(sourceFinished),      reinterpret_cast<void (*)()>(sourceAction),
};


bool init(void* display) {
    if (g.failed) return false;
    if (g.queue) return g.device != nullptr;
    if (!display || !lib().ok) {
        g.failed = true;
        return false;
    }
    g.display = static_cast<wl_display*>(display);
    g.queue = lib().createQueue(g.display);
    // The registry must be created on our queue from the start, or its events could reach SDL's queue first.
    auto* wrapper = static_cast<wl_proxy*>(lib().createWrapper(display));
    lib().setQueue(wrapper, g.queue);
    g.registry = lib().marshal(wrapper, kDisplayGetRegistry, lib().registryIface, lib().getVersion(wrapper), 0, nullptr);
    lib().wrapperDestroy(wrapper);
    lib().addListener(g.registry, kRegistryListener, nullptr);
    lib().roundtripQueue(g.display, g.queue);  // globals -> seat, manager
    if (!g.seat || !g.manager) {
        g.failed = true;
        return false;
    }
    lib().addListener(g.seat, kSeatListener, nullptr);
    lib().roundtripQueue(g.display, g.queue);  // seat capabilities -> pointer
    g.device = lib().marshal(g.manager, kManagerGetDevice, lib().deviceIface, lib().getVersion(g.manager), 0, nullptr, g.seat);
    lib().addListener(g.device, kDeviceListener, nullptr);
    lib().flush(g.display);
    if (std::getenv("MAYO_VERBOSE"))
        std::fprintf(stderr, "mayonnaise: Wayland file drags ready (pointer %s)\n", g.pointer ? "yes" : "no");
    return true;
}

}  // namespace

void prepare(void* display) { init(display); }

bool startFileDrag(void* display, void* surface, const std::vector<std::string>& paths, std::function<void()> ended) {
    if (paths.empty() || !surface || !init(display) || !g.pointer) return false;
    dispatch();  // pick up the serial of the press that started this drag
    if (g.pressSerial == 0) return false;
    if (g.source) finishDrag(true);  // a previous drag never finished

    g.uriList.clear();
    g.plain.clear();
    for (const auto& p : paths) {
        g.uriList += "file://" + percentEncode(p) + "\r\n";
        g.plain += (g.plain.empty() ? "" : "\n") + p;
    }
    g.ended = std::move(ended);
    g.source = create(g.manager, kManagerCreateSource, lib().sourceIface);
    lib().addListener(g.source, kSourceListener, nullptr);
    for (const char* mime : {"text/uri-list", "text/plain;charset=utf-8", "text/plain", "UTF8_STRING"})
        lib().marshal(g.source, kSourceOffer, nullptr, lib().getVersion(g.source), 0, mime);
    if (lib().getVersion(g.source) >= 3)
        lib().marshal(g.source, kSourceSetActions, nullptr, lib().getVersion(g.source), 0, kActionCopy | kActionMove);
    lib().marshal(g.device, kDeviceStartDrag, nullptr, lib().getVersion(g.device), 0, g.source, surface, nullptr, g.pressSerial);
    lib().flush(g.display);
    return true;
}

void dispatch() {
    if (!g.queue) return;
    lib().dispatchQueuePending(g.display, g.queue);
    lib().flush(g.display);
}

bool dragActive() { return g.source != nullptr; }

#endif

}  // namespace mayo::platform::wayland
