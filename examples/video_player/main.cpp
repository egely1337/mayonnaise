// Video player — QuickTime-style playback on Mayonnaise, decoding with FFmpeg.
//
//   video_player [file...]
//
// Space / K play-pause · ← → seek 5 s · J L seek 10 s · ↑ ↓ volume · M mute · F full screen ·
// click the video to play/pause, double-click for full screen · drop files to play them · Ctrl+O open.
#include <mayonnaise/mayonnaise.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>

#include "media_player.hpp"

using namespace mayo;

namespace {

std::string formatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    auto total = static_cast<long>(seconds);
    char buf[32];
    if (total >= 3600)
        std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
    else
        std::snprintf(buf, sizeof buf, "%ld:%02ld", total / 60, total % 60);
    return buf;
}

/// Round icon button for the HUD: white glyph, soft circular hover.
class HudButton : public Widget {
public:
    HudButton(Icon icon, float size) : m_icon(icon), m_size(size) {}
    std::function<void()> onClick;
    void setIcon(Icon i) {
        if (i != m_icon) {
            m_icon = i;
            invalidate();
        }
    }
    void setActive(bool a) {
        if (a != m_active) {
            m_active = a;
            invalidate();
        }
    }
    [[nodiscard]] bool isInteractive() const override { return true; }
    Size measureContent(Size /*available*/) override { return {m_size + 14, m_size + 14}; }
    void draw(Painter& p) override {
        Rect r = frame();
        float wash = isPressed() ? 0.22f : isHovered() ? 0.12f : 0.0f;
        if (wash > 0) p.fillCircle(r.center(), r.width * 0.5f, Color::white(wash));
        Color c = m_active ? theme().colors.accent.lighter(0.25f) : Color::white(0.95f);
        p.drawIcon(m_icon, {r.center().x - m_size * 0.5f, r.center().y - m_size * 0.5f, m_size, m_size}, c, 1.15f);
    }
    bool onPointerDown(const PointerEvent& e) override { return e.button == MouseButton::Left; }
    void onPointerUp(const PointerEvent& e) override {
        if (frame().contains(e.position) && onClick) onClick();
    }

private:
    Icon m_icon;
    float m_size;
    bool m_active = false;
};

/// Thin white track used for the timeline and the volume. Grows while hovered, like QuickTime.
class TrackBar : public Widget {
public:
    std::function<void(float, bool)> onChange;  // value, final (pointer released)
    std::function<std::string(float)> hoverLabel;
    void setValue(float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        if (std::abs(v - m_value) > 1e-4f && !m_dragging) {
            m_value = v;
            invalidate();
        }
    }
    [[nodiscard]] float value() const { return m_value; }
    [[nodiscard]] bool isDragging() const { return m_dragging; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    Size measureContent(Size available) override {
        return {std::isfinite(available.width) ? available.width : 120.0f, 18};
    }
    [[nodiscard]] bool expandsContent(Axis a) const override { return a == Axis::Horizontal; }
    void onHoverChanged(bool h) override { m_grow.animateTo(h ? 1.0f : 0.0f, now(), Transition::fast()); }
    void draw(Painter& p) override {
        const Rect r = frame();
        const float g = std::max(sample(m_grow), m_dragging ? 1.0f : 0.0f);
        const float h = 4 + 3 * g;
        Rect track{r.x, r.center().y - h * 0.5f, r.width, h};
        p.fillRoundedRect(track, h * 0.5f, Color::white(0.22f));
        p.fillRoundedRect({track.x, track.y, std::max(h, track.width * m_value), h}, h * 0.5f, Color::white(0.92f));
        if (hoverLabel && (isHovered() || m_dragging) && m_hoverX >= 0) {
            // Time bubble above the pointer.
            float f = std::clamp((m_hoverX - r.x) / std::max(1.0f, r.width), 0.0f, 1.0f);
            std::string text = hoverLabel(m_dragging ? m_value : f);
            Font font = theme().font(TextStyle::Subheadline).withWeight(FontWeight::Medium);
            float w = p.measureText(text, font) + 14;
            Rect bubble{std::clamp(m_hoverX - w * 0.5f, r.x - 20, r.right() + 20 - w), r.y - 30, w, 22};
            p.drawShadow(bubble, 8, Color::black(0.4f), 8, {0, 2});
            p.fillRoundedRect(bubble, 8, Color::rgb8(40, 40, 44, 0.95f));
            p.drawText(text, bubble, font, Color::white(), TextAlign::Center);
        }
    }
    bool onPointerDown(const PointerEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        m_dragging = true;
        update(e.position.x, false);
        return true;
    }
    void onPointerMove(const PointerEvent& e) override {
        m_hoverX = e.position.x;
        if (m_dragging) update(e.position.x, false);
        if (hoverLabel) invalidate();
    }
    void onPointerUp(const PointerEvent& e) override {
        update(e.position.x, true);
        m_dragging = false;
    }
    void onPointerLeave() override { m_hoverX = -1; }

private:
    void update(float x, bool final) {
        const Rect r = frame();
        m_value = std::clamp((x - r.x) / std::max(1.0f, r.width), 0.0f, 1.0f);
        invalidate();
        if (onChange) onChange(m_value, final);
    }
    float m_value = 0;
    float m_hoverX = -1;
    bool m_dragging = false;
    Animated<float> m_grow{0.0f};
};

/// Dark Liquid Glass panel holding the transport controls.
class HudPanel : public Widget {
public:
    explicit HudPanel(const Element& content) {
        setPadding(Insets(12, 18, 14, 18));
        addChild(content.widget());
    }
    void draw(Painter& p) override {
        const Rect r = frame();
        const float radius = 22;
        p.drawShadow(r, radius, Color::black(0.45f), 30, {0, 10});
        MaterialStyle m = theme().material(Material::Popover);
        m.tint = Color::rgb8(28, 28, 32, 0.55f);
        m.saturation = 1.4f;
        m.highlight = Color::white(0.18f);
        m.border = Color::white(0.10f);
        p.drawMaterial(r, radius, m);
        paintChildren(p);
    }
};

/// Draws the current video frame letterboxed on black and keeps frames coming while playing.
class VideoSurface : public Widget {
public:
    explicit VideoSurface(MediaPlayer& player) : m_player(player) {}
    std::function<void()> onClick;
    std::function<void()> onDoubleClick;
    std::function<void()> onTick;  // every rendered frame while playing (HUD time, end detection)
    std::string audioTitle;

    void reset() {
        m_texture.reset();
        m_shown = {};
    }
    [[nodiscard]] bool isInteractive() const override { return true; }
    [[nodiscard]] bool expandsContent(Axis /*axis*/) const override { return true; }

    void draw(Painter& p) override {
        const Rect r = frame();
        p.fillRect(r, Color::black());
        if (!m_player.isOpen()) return;
        MediaPlayer::Frame f;
        if (m_player.takeFrame(f)) {
            if (!m_texture) m_texture = VideoTexture::create();
            if (f.uploadTo(*m_texture)) {
                // Keep the frame (and with it the decoder surface) alive while it's on screen.
                m_shown = std::move(f);
            } else {
                m_player.disableZeroCopy();  // import failed: later frames come through system memory
            }
        }
        if (m_player.hasVideo() && m_texture && !m_texture->isEmpty()) {
            const auto aspect = static_cast<float>(m_player.aspect());
            float w = r.width;
            float h = w / aspect;
            if (h > r.height) {
                h = r.height;
                w = h * aspect;
            }
            p.drawVideo({r.center().x - w * 0.5f, r.center().y - h * 0.5f, w, h}, *m_texture);
        } else if (!m_player.hasVideo()) {
            // Audio only: a big note-like glyph and the title.
            const Theme& t = theme();
            Rect icon{r.center().x - 48, r.center().y - 90, 96, 96};
            p.fillRoundedRect(icon.outset(18), 30, Color::white(0.08f));
            p.drawIcon(Icon::Speaker, icon, Color::white(0.7f), 0.8f);
            p.drawText(audioTitle, {r.x, icon.bottom() + 34, r.width, 30}, t.font(TextStyle::Title2), Color::white(0.9f),
                       TextAlign::Center);
        }
        if (m_player.isPlaying() || m_player.isSeeking()) requestAnimationFrame();
        if (onTick) onTick();
    }
    bool onPointerDown(const PointerEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        if (e.clickCount == 2) {
            if (onDoubleClick) onDoubleClick();
        } else if (onClick) {
            onClick();
        }
        return true;
    }

private:
    MediaPlayer& m_player;
    std::shared_ptr<VideoTexture> m_texture;
    MediaPlayer::Frame m_shown;
};

/// Root: video under everything (including the title bar), HUD floating at the bottom, empty state.
class PlayerPage : public Widget {
public:
    std::shared_ptr<VideoSurface> surface;
    std::shared_ptr<HudPanel> hud;
    std::shared_ptr<Widget> empty;
    std::function<void()> onActivity;

    [[nodiscard]] bool extendsIntoTitleBar() const override { return true; }
    [[nodiscard]] bool expandsContent(Axis /*axis*/) const override { return true; }
    void setHudVisible(bool visible) {
        m_hud.animateTo(visible ? 1.0f : 0.0f, now(), Transition{visible ? 0.18f : 0.4f, Curve::EaseInOut});
        invalidate();
    }
    void arrangeContent(const Rect& c) override {
        surface->measure(c.size());
        surface->arrange(c);
        const float w = std::min(620.0f, c.width - 40);
        Size s = hud->measure({w, c.height});
        hud->arrange({c.center().x - w * 0.5f, c.bottom() - s.height - 24, w, s.height});
        const float bar = host() ? host()->titleBarHeight() : 0.0f;
        Rect below{c.x, c.y + bar, c.width, c.height - bar};
        empty->measure(below.size());
        empty->arrange(below);
    }
    void draw(Painter& p) override {
        hud->setOpacity(sample(m_hud));
        paintChildren(p);
    }
    void onPointerMove(const PointerEvent& /*e*/) override {
        if (onActivity) onActivity();
    }

private:
    Animated<float> m_hud{1.0f};
};

}  // namespace

int main(int argc, char** argv) {
    App app("video_player");
    app.setDefaultAppearanceMode(AppearanceMode::Dark);  // controls float over video; dark chrome reads best
    auto& window = app.createWindow(
        {.title = "Video Player", .width = 1000, .height = 620, .minWidth = 480, .minHeight = 320, .autosaveName = "main"});
    Settings& prefs = app.settings();
    MediaPlayer player;
    player.setHardwareDecoding(prefs.getBool("hardwareDecoding", true));

    std::deque<std::string> playlist;
    bool looping = prefs.getBool("loop");
    bool muted = prefs.getBool("muted");
    float volume = std::clamp(prefs.getFloat("volume", 1.0f), 0.0f, 1.0f);
    std::string currentPath;

    // Where each recently played file was left: "seconds<TAB>path" lines, newest first.
    auto loadPositions = [&] {
        std::vector<std::pair<double, std::string>> out;
        const std::string all = prefs.getString("positions");
        size_t start = 0;
        while (start < all.size()) {
            size_t end = all.find('\n', start);
            if (end == std::string::npos) end = all.size();
            const std::string line = all.substr(start, end - start);
            if (const size_t tab = line.find('\t'); tab != std::string::npos)
                out.emplace_back(std::atof(line.substr(0, tab).c_str()), line.substr(tab + 1));
            start = end + 1;
        }
        return out;
    };
    auto rememberPosition = [&] {
        if (currentPath.empty() || !player.isOpen()) return;
        auto list = loadPositions();
        std::erase_if(list, [&](const auto& e) { return e.second == currentPath; });
        const double pos = player.position();
        const double dur = player.duration();
        // Nothing to resume at the very start or end.
        if (prefs.getBool("resumePlayback", true) && pos > 5 && (dur <= 0 || pos < dur - 5)) list.emplace(list.begin(), pos, currentPath);
        if (list.size() > 30) list.resize(30);
        std::string text;
        char buf[32];
        for (const auto& [seconds, path] : list) {
            std::snprintf(buf, sizeof buf, "%.1f", seconds);
            text += (text.empty() ? "" : "\n") + std::string(buf) + "\t" + path;
        }
        prefs.set("positions", text);
    };
    int activity = 0;

    // Controls ---------------------------------------------------------------------------------
    auto play = std::make_shared<HudButton>(Icon::Play, 26);
    auto back = std::make_shared<HudButton>(Icon::SkipBack, 20);
    auto forward = std::make_shared<HudButton>(Icon::SkipForward, 20);
    auto speaker = std::make_shared<HudButton>(Icon::Speaker, 16);
    auto loop = std::make_shared<HudButton>(Icon::Repeat, 16);
    auto fullscreen = std::make_shared<HudButton>(Icon::Expand, 16);
    auto infoButton = std::make_shared<HudButton>(Icon::Info, 16);
    auto settingsButton = std::make_shared<HudButton>(Icon::Gear, 16);
    auto volumeBar = std::make_shared<TrackBar>();
    volumeBar->setValue(1);
    volumeBar->setFixedWidth(84);
    auto timeline = std::make_shared<TrackBar>();
    auto elapsed = Label("0:00").font(TextStyle::Subheadline).foreground(Color::white(0.85f)).width(56);
    auto remaining = Label("-0:00").font(TextStyle::Subheadline).foreground(Color::white(0.85f)).width(56).textAlign(TextAlign::Trailing);

    auto hudContent = VStack({
                                 HStack({speaker, Element(volumeBar), Spacer(), back, play, forward, Spacer(), Element(infoButton), Element(settingsButton), loop, fullscreen})
                                     .spacing(6)
                                     .align(Alignment::Center),
                                 HStack({elapsed, Element(timeline), remaining}).spacing(10).align(Alignment::Center),
                             })
                          .spacing(6);
    auto hud = std::make_shared<HudPanel>(hudContent);

    auto surface = std::make_shared<VideoSurface>(player);

    auto emptyIcon = Canvas([](Painter& p, const Rect& r) { p.drawIcon(Icon::Film, r, Color::white(0.45f), 0.7f); });
    emptyIcon.frame(64, 64);
    auto emptyError = Label("").foreground(theme().colors.red).hidden();
    auto openButton = Button("Open…").style(ButtonStyle::Prominent).controlSize(ControlSize::Large);
    auto settingsLink = Button("Settings…").style(ButtonStyle::Borderless);
    auto empty = VStack({emptyIcon, Label("Open a Video").font(TextStyle::Title2),
                         Label("Drop a movie or audio file here, or choose one.").secondary(), openButton, settingsLink, emptyError})
                     .spacing(10)
                     .align(Alignment::Center);

    auto page = std::make_shared<PlayerPage>();
    page->surface = surface;
    page->hud = hud;
    page->empty = empty.node();
    page->addChild(surface);
    page->addChild(hud);
    page->addChild(empty.node());

    // Behaviour ----------------------------------------------------------------------------------
    auto showChrome = [&](bool visible) {
        page->setHudVisible(visible);
        window.setChromeHidden(!visible);
    };
    // Any activity shows the controls; they fade out again after a few idle seconds while playing.
    auto poke = [&] {
        const int mine = ++activity;
        showChrome(true);
        app.after(2.8, [&, mine] {
            if (mine == activity && player.isPlaying() && !timeline->isDragging()) showChrome(false);
        });
    };
    page->onActivity = poke;

    auto updatePlayButton = [&] { play->setIcon(player.isPlaying() ? Icon::Pause : Icon::Play); };
    auto togglePlay = [&] {
        if (!player.isOpen()) return;
        player.isPlaying() ? player.pause() : player.play();
        updatePlayButton();
        poke();
    };
    auto seekBy = [&](double delta) {
        if (!player.isOpen()) return;
        player.seek(player.position() + delta);
        poke();
        surface->invalidate();
    };
    auto applyVolume = [&] {
        prefs.set("volume", volume);
        prefs.set("muted", muted);
        player.setVolume(muted ? 0.0f : volume * volume);  // perceptual curve
        speaker->setIcon(muted || volume <= 0.001f ? Icon::SpeakerSlash : Icon::Speaker);
        volumeBar->setValue(muted ? 0.0f : volume);
    };
    // Track the state ourselves: the compositor applies full screen asynchronously.
    bool isFull = false;
    auto toggleFullscreen = [&] {
        isFull = !isFull;
        window.setFullscreen(isFull);
        fullscreen->setIcon(isFull ? Icon::Contract : Icon::Expand);
    };

    std::function<bool(const std::string&)> openFile = [&](const std::string& path) {
        rememberPosition();
        std::string error;
        surface->reset();
        if (!player.open(path, error)) {
            emptyError.setText(error);
            emptyError.setHidden(false);
            empty.setHidden(false);
            hud->setHidden(true);
            window.setTitle("Video Player");
            return false;
        }
        emptyError.setHidden(true);
        empty.setHidden(true);
        hud->setHidden(false);
        surface->audioTitle = player.title();
        if (std::getenv("MAYO_VERBOSE")) {
            // After a second of decoding, so a hardware → software fallback is visible too.
            app.after(1.0, [&] {
                std::fprintf(stderr, "video_player: %s — %s, %d dropped\n", player.codecDescription().c_str(),
                             player.decoderDescription().c_str(), player.droppedFrames());
            });
        }
        window.setTitle(player.title());
        timeline->setValue(0);
        applyVolume();
        currentPath = path;
        if (prefs.getBool("resumePlayback", true)) {
            for (const auto& [seconds, p] : loadPositions()) {
                if (p == path) player.seek(seconds);
            }
        }
        player.play();
        updatePlayButton();
        poke();
        surface->invalidate();
        return true;
    };
    auto openPaths = [&](const std::vector<std::string>& paths) {
        if (paths.empty()) return;
        playlist.assign(paths.begin() + 1, paths.end());
        openFile(paths.front());
    };
    const std::vector<FileFilter> filters{
        {"Movies & audio", "mp4;m4v;mov;mkv;webm;avi;ogv;mpg;mpeg;ts;mp3;m4a;aac;flac;wav;ogg;opus"}, {"All files", "*"}};
    auto chooseFile = [&] { app.openFiles(filters, true, openPaths, &window); };

    play->onClick = togglePlay;
    back->onClick = [&] { seekBy(-10); };
    forward->onClick = [&] { seekBy(10); };
    speaker->onClick = [&] {
        muted = !muted;
        applyVolume();
    };
    volumeBar->onChange = [&](float v, bool) {
        volume = v;
        muted = false;
        applyVolume();
    };
    loop->setActive(looping);
    loop->onClick = [&] {
        looping = !looping;
        prefs.set("loop", looping);
        loop->setActive(looping);
    };
    fullscreen->onClick = toggleFullscreen;

    // Playback info: what decodes the video, and whether frames are being dropped.
    auto infoTitle = Label("").font(TextStyle::Headline).lineLimit(1).maxWidth(320);
    auto infoCodec = Label("").secondary();
    auto infoDecoder = Label("");
    auto infoStats = Label("").secondary();
    auto info = Popover(VStack({infoTitle, infoCodec, infoDecoder, infoStats}).spacing(4));
    auto refreshInfo = [&] {
        infoTitle.setText(player.title());
        std::string codec = player.codecDescription();
        if (player.frameRate() > 0) {
            char fps[32];
            std::snprintf(fps, sizeof fps, " · %.3g fps", player.frameRate());
            codec += fps;
        }
        infoCodec.setText(codec);
        infoDecoder.setText(player.decoderDescription());
        infoStats.setText(std::to_string(player.droppedFrames()) + " dropped frames");
    };
    infoButton->onClick = [&] {
        refreshInfo();
        info.show(Element(infoButton), PopupEdge::Above);
    };
    timeline->hoverLabel = [&](float f) { return formatTime(f * player.duration()); };
    timeline->onChange = [&](float v, bool /*final*/) {
        player.seek(v * player.duration());
        surface->invalidate();
    };
    openButton.onClick(chooseFile);
    surface->onClick = togglePlay;
    surface->onDoubleClick = [&] {
        togglePlay();  // undo the first click's toggle
        toggleFullscreen();
    };
    surface->onTick = [&] {
        const double pos = player.position();
        const double dur = player.duration();
        if (!timeline->isDragging() && dur > 0) timeline->setValue(static_cast<float>(pos / dur));
        std::string e = formatTime(pos);
        std::string r = "-" + formatTime(std::max(0.0, dur - pos));
        if (e != elapsed.text()) elapsed.setText(e);
        if (r != remaining.text()) remaining.setText(r);
        if (info.isShown()) refreshInfo();
        if (player.isPlaying() && player.ended()) {
            // Defer: switching files from inside a draw would re-enter layout.
            app.post([&] {
                if (!player.ended()) return;
                if (looping) {
                    player.seek(0);
                } else if (!playlist.empty()) {
                    std::string next = playlist.front();
                    playlist.pop_front();
                    openFile(next);
                } else {
                    player.pause();
                    updatePlayButton();
                    showChrome(true);
                }
            });
        }
    };

    // Settings window (Ctrl+,) ------------------------------------------------------------------------
    Window* settingsWindow = nullptr;
    auto openSettings = [&] {
        if (settingsWindow) {
            settingsWindow->raise();
            return;
        }
        auto& w = app.createWindow({.title = "Video Player Settings", .width = 540, .height = 480, .minWidth = 460, .minHeight = 320});
        auto resume = Toggle("", prefs.getBool("resumePlayback", true)).onChange([&](bool on) {
            prefs.set("resumePlayback", on);
            if (!on) prefs.remove("positions");  // forget where files were left
        });
        auto hardware = Toggle("", prefs.getBool("hardwareDecoding", true)).onChange([&](bool on) {
            prefs.set("hardwareDecoding", on);
            player.setHardwareDecoding(on);
        });
        auto form = Form({
            AppearanceSettings(app),
            FormSection("Playback",
                        {FormRow("Resume where you left off", resume, "Reopened videos continue from the last position"),
                         FormRow("Hardware decoding", hardware, "Takes effect with the next video")}),
        });
        w.setContent(ScrollView(VStack({form}).padding(Insets(12, 24, 24, 24))));
        w.onKey([&w](const KeyEvent& e) {
            if (e.key == Key::Escape || (e.primary() && e.key == Key::W)) {
                w.close();
                return true;
            }
            return false;
        });
        w.onClosed([&] { settingsWindow = nullptr; });
        settingsWindow = &w;
    };
    settingsButton->onClick = openSettings;
    settingsLink.onClick(openSettings);

    window.onFilesDropped(openPaths);
    window.onKey([&](const KeyEvent& e) {
        poke();
        if (e.primary() && e.key == Key::O) {
            chooseFile();
            return true;
        }
        if (e.primary() && e.key == Key::Comma) {
            openSettings();
            return true;
        }
        switch (e.key) {
            case Key::Space:
            case Key::K: togglePlay(); return true;
            case Key::Left: seekBy(-5); return true;
            case Key::Right: seekBy(5); return true;
            case Key::J: seekBy(-10); return true;
            case Key::L: seekBy(10); return true;
            case Key::Home: seekBy(-1e9); return true;
            case Key::Up:
                volume = std::min(1.0f, volume + 0.1f);
                muted = false;
                applyVolume();
                return true;
            case Key::Down:
                volume = std::max(0.0f, volume - 0.1f);
                applyVolume();
                return true;
            case Key::M:
                muted = !muted;
                applyVolume();
                return true;
            case Key::F: toggleFullscreen(); return true;
            case Key::Escape:
                if (isFull) toggleFullscreen();
                return true;
            default: return false;
        }
    });

    hud->setHidden(true);
    applyVolume();
    window.setContent(Element(page));

    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty()) openPaths(args);

    // MAYO_DEMO=seek:<seconds> jumps there and pauses (screenshots); MAYO_DEMO=settings opens the Settings window.
    if (const char* env = std::getenv("MAYO_DEMO")) {
        std::string demo = env;
        if (demo == "settings") app.after(0.3, openSettings);
        if (demo.rfind("seek:", 0) == 0) {
            double t = std::atof(demo.c_str() + 5);
            app.after(0.3, [&, t] {
                player.pause();
                player.seek(t);
                updatePlayButton();
                surface->invalidate();
            });
        }
    }
    const int code = app.run();
    rememberPosition();  // saved with the other settings when the App goes away
    return code;
}
