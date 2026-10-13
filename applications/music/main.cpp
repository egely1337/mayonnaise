// Music — an Apple Music-style player on Mayonnaise, decoding with FFmpeg.
//
//   music [files or folders...]
//
// The library is ~/Music (and any folders added in Settings): Recently Added, Artists, Albums, Songs and
// Favorite Songs, with cover art from the files or the album folders. Files given on the command line or
// dropped on the window play at once. MPRIS makes media keys and the desktop's media controls work.
//
// Space play/pause · Ctrl+→ / Ctrl+← next / previous · Ctrl+↑ / Ctrl+↓ volume · Ctrl+L current album ·
// Ctrl+F search · Ctrl+O open · Ctrl+1…5 sections · Esc back · Ctrl+, settings.
//
// MAYO_DEMO=<steps>, comma separated, drives it for screenshots: recent | artists | albums | songs | favorites,
// album:<n> (open the n-th album), play:<n> (play it), search:<text>, queue, settings, pause, seek:<s>.
#include <mayonnaise/mayonnaise.hpp>

extern "C" {
#include <libavutil/log.h>
}

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>

#include "library.hpp"
#include "mpris.hpp"
#include "player.hpp"
#include "views.hpp"

using namespace mayo;
using namespace music;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& l : v) s += (s.empty() ? "" : "\n") + l;
    return s;
}

std::string countText(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

std::string totalTime(double seconds) {
    const auto minutes = static_cast<long>(seconds / 60 + 0.5);
    if (minutes < 60) return countText(static_cast<size_t>(minutes), "minute", "minutes");
    const long h = minutes / 60;
    const long m = minutes % 60;
    return std::to_string(h) + (h == 1 ? " hour" : " hours") + (m ? ", " + countText(static_cast<size_t>(m), "minute", "minutes") : "");
}

/// Anchor for context menus at the pointer (menus open from a widget's frame).
class PointAnchor : public Widget {
public:
    Size measureContent(Size /*available*/) override { return {0, 0}; }
};

/// Message shown when a page has nothing to list.
struct EmptyState {
    Label title = Label("").font(TextStyle::Title2);
    Label text = Label("").secondary().textAlign(TextAlign::Center).maxWidth(420);
    Button action = Button(Icon::FolderPlus, "Add Folder…").style(ButtonStyle::Prominent);
    VStack root = VStack({});

    EmptyState() {
        auto icon = Canvas([](Painter& p, const Rect& r) { p.drawIcon(Icon::MusicNote, r, theme().colors.tertiaryLabel, 0.8f); });
        icon.frame(60, 60);
        root = VStack({icon, title, text, action}).spacing(10).align(Alignment::Center);
    }
    void set(const std::string& t, const std::string& body, bool showAction) {
        title.setText(t);
        text.setText(body);
        action.setHidden(!showAction);
    }
};

/// One page of the detail side: a PageView around a scroll view and an empty state.
struct Page {
    std::shared_ptr<PageView> view = std::make_shared<PageView>();
    ScrollView scroll;
    EmptyState empty;

    explicit Page(const Element& content) : scroll(content) {
        view->scroll = scroll.node();
        view->empty = empty.root.node();
        view->addChild(scroll.node());
        view->addChild(empty.root.node());
        empty.root.setHidden(true);
    }
    void showEmpty(bool empty_) {
        scroll.setHidden(empty_);
        empty.root.setHidden(!empty_);
    }
};

class MusicApp {
public:
    MusicApp(App& app, Window& window) : m_app(app), m_window(window), m_prefs(app.settings()) {
        m_demo = std::getenv("MAYO_DEMO") != nullptr;
        m_volume = std::clamp(m_prefs.getFloat("volume", 0.8f), 0.0f, 1.0f);
        m_shuffle = m_prefs.getBool("shuffle");
        m_repeat = std::clamp(m_prefs.getInt("repeat"), 0, 2);
        m_folders = splitLines(m_prefs.getString("folders"));
        if (!m_prefs.contains("folders")) m_folders = {musicDirectory()};
        for (const auto& f : splitLines(m_prefs.getString("favorites"))) m_favorites.insert(f);
        m_section = m_prefs.getString("section", "albums");
        if (m_section.rfind("album:", 0) == 0) m_section = "albums";
        m_player.setVolume(m_volume);
        buildUi();
        startCoverWorker();
        setupMpris();
    }

    ~MusicApp() {
        {
            std::scoped_lock lock(m_coverMutex);
            m_coverStop = true;
        }
        m_coverCv.notify_all();
        if (m_coverThread.joinable()) m_coverThread.join();
        if (m_scanner.joinable()) m_scanner.join();
        m_mpris.reset();
    }

    /// Cached library at once, then a fresh scan in the background.
    void loadLibrary() {
        setLibrary(loadCachedLibrary());
        rescan();
    }

    void openPaths(const std::vector<std::string>& paths) {
        std::vector<TrackPtr> found;
        for (const auto& p : paths) {
            std::error_code ec;
            if (fs::is_directory(p, ec)) {
                std::vector<std::string> files;
                for (const auto& e : fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec)) {
                    if (e.is_regular_file(ec) && isAudioFile(e.path().string())) files.push_back(e.path().string());
                }
                std::sort(files.begin(), files.end());
                for (const auto& f : files) {
                    if (auto t = trackFor(f)) found.push_back(t);
                }
            } else if (auto t = trackFor(p)) {
                found.push_back(t);
            }
        }
        if (found.empty()) return;
        playContext(found, 0);
    }

    void restoreLastTrack() {
        const std::string last = m_prefs.getString("lastTrack");
        if (last.empty() || m_current) return;
        for (const auto& t : m_tracks) {
            if (t->path != last) continue;
            const AlbumPtr album = albumFor(*t);
            std::vector<TrackPtr> ctx = album ? album->tracks : std::vector<TrackPtr>{t};
            const auto it = std::find(ctx.begin(), ctx.end(), t);
            setContext(ctx, static_cast<size_t>(it - ctx.begin()));
            load(t, false);
            const double pos = m_prefs.getDouble("lastPosition");
            if (pos > 1 && pos < t->duration - 2) m_player.seek(pos);
            refreshNowPlaying();
            return;
        }
    }

    void runDemo(const std::string& script) {
        double at = 0.3;
        size_t start = 0;
        while (start <= script.size()) {
            const size_t comma = script.find(',', start);
            const std::string step = script.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            m_app.after(at, [this, step] { demoStep(step); });
            at += 0.35;
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }

    void saveState() {
        m_prefs.set("volume", m_volume);
        if (m_current) {
            m_prefs.set("lastTrack", m_current->path);
            m_prefs.set("lastPosition", m_player.position());
        }
    }

private:
    // UI ---------------------------------------------------------------------------------------------

    void buildUi() {
        m_search.padding(Insets(0, 10, 8, 10));
        m_sidebar.header(m_search);
        m_search.onChange([this](const std::string& q) {
            m_query = lowerAscii(q);
            if (m_query.empty()) showSection(m_section == "search" ? "albums" : m_section, false);
            else showSearch();
        });
        m_sidebar.onSelect([this](const std::string& id) { showSection(id, true); });

        m_grid->setScroller(m_gridPage.scroll.node().get());
        m_songs->setScroller(m_songsPage.scroll.node().get());
        m_albumTracks->setScroller(m_albumPage.scroll.node().get());
        m_artistGrid->setScroller(m_artistsPage.scroll.node().get());
        m_searchSongs->setScroller(m_searchPage.scroll.node().get());
        m_searchAlbums->setScroller(m_searchPage.scroll.node().get());

        for (auto* g : {m_grid.get(), m_artistGrid.get(), m_searchAlbums.get()}) {
            g->onOpen = [this](const AlbumPtr& a) { openAlbum(a); };
            g->onPlay = [this](const AlbumPtr& a) { playContext(a->tracks, 0); };
            g->needCover = [this](const AlbumPtr& a) { requestCover(a); };
            g->onContextMenu = [this](const AlbumPtr& a, Point at) { albumMenu(a, at); };
        }
        for (auto* l : {m_songs.get(), m_albumTracks.get(), m_searchSongs.get()}) {
            l->onPlay = [this](const std::vector<TrackPtr>& list, size_t i) { playContext(list, i); };
            l->isFavorite = [this](const Track& t) { return m_favorites.contains(t.path); };
            l->onToggleFavorite = [this](const TrackPtr& t) { toggleFavorite(t); };
            l->onContextMenu = [this](const TrackPtr& t, Point at) { trackMenu(t, at); };
        }

        // Artists: a list on the leading side, their albums on the right.
        m_artistScroll = ScrollView(Element(m_artistList));
        m_artistsPage.view->leading = m_artistScroll.node();
        m_artistsPage.view->addChild(m_artistScroll.node());
        m_artistList->onSelect = [this](int i) {
            m_artistIndex = i;
            showArtist();
        };

        for (Page* p : {&m_gridPage, &m_songsPage, &m_albumPage, &m_artistsPage, &m_searchPage}) {
            p->empty.action.onClick([this] { addFolder(); });
            p->view->addChild(m_menuAnchor);
        }
        m_split.sidebarWidth(220).sidebarLimits(180, 320);

        // Toolbar: transport, the now-playing display, volume and the queue.
        m_back = Button(Icon::ChevronLeft).style(ButtonStyle::Borderless).tooltip("Back (Esc)");
        m_back.onClick([this] { goBack(); });
        m_back.setHidden(true);
        m_shuffleButton = Button(Icon::Shuffle).style(ButtonStyle::Borderless).tooltip("Shuffle");
        m_shuffleButton.onClick([this] { setShuffle(!m_shuffle); });
        auto prev = Button(Icon::Backward).style(ButtonStyle::Borderless).tooltip("Previous (Ctrl+←)");
        prev.onClick([this] { previous(); });
        m_playButton = Button(Icon::Play).style(ButtonStyle::Borderless).tooltip("Play (Space)");
        m_playButton.onClick([this] { togglePlay(); });
        auto next = Button(Icon::Forward).style(ButtonStyle::Borderless).tooltip("Next (Ctrl+→)");
        next.onClick([this] { advance(false); });
        m_repeatButton = Button(Icon::Repeat).style(ButtonStyle::Borderless).tooltip("Repeat");
        m_repeatButton.onClick([this] { setRepeat((m_repeat + 1) % 3); });
        m_lcd->onSeek = [this](double s) { seek(s); };
        m_lcd->onArtworkClick = [this] { showCurrentAlbum(); };
        m_volumeSlider = Slider(m_volume, 0, 1).width(90).onChange([this](float v) { setVolume(v); });
        m_volumeButton = Button(Icon::Speaker).style(ButtonStyle::Borderless).tooltip("Mute");
        m_volumeButton.onClick([this] {
            if (m_volume > 0) {
                m_unmutedVolume = m_volume;
                setVolume(0);
            } else {
                setVolume(m_unmutedVolume > 0 ? m_unmutedVolume : 0.8f);
            }
        });
        m_queueButton = Button(Icon::List).style(ButtonStyle::Borderless).tooltip("Playing Next");
        m_queueButton.onClick([this] { showQueue(); });
        m_window.setToolbar(Toolbar({m_back, ToolbarGroup({m_shuffleButton, prev, m_playButton, next, m_repeatButton}), Spacer(),
                                     Element(m_lcd), Spacer(), m_volumeButton, m_volumeSlider, m_queueButton}));
        m_window.setContent(m_split);
        updateModeButtons();
        refreshNowPlaying();
    }

    void rebuildSidebar() {
        auto badge = [](size_t n) { return n ? std::to_string(n) : std::string(); };
        std::vector<SidebarItem> items{
            SidebarItem::header("Library"),
            SidebarItem("recent", Icon::Clock, "Recently Added"),
            SidebarItem("artists", Icon::Person, "Artists").withBadge(badge(m_lib.artists.size())),
            SidebarItem("albums", Icon::Album, "Albums").withBadge(badge(m_lib.albums.size())),
            SidebarItem("songs", Icon::MusicNote, "Songs").withBadge(badge(m_tracks.size())),
            SidebarItem::header("Playlists"),
            SidebarItem("favorites", Icon::Heart, "Favorite Songs").withBadge(badge(favoriteTracks().size())),
        };
        m_sidebar.setItems(std::move(items));
        m_sidebar.selection(m_query.empty() && m_section != "album" ? m_section : "");
    }

    std::vector<TrackPtr> favoriteTracks() const {
        std::vector<TrackPtr> out;
        for (const auto& t : m_tracks) {
            if (m_favorites.contains(t->path)) out.push_back(t);
        }
        return out;
    }

    void setDetail(Page& page) {
        m_split.setDetail(Element(page.view));
        m_shownPage = &page;
        m_back.setHidden(&page != &m_albumPage);
    }

    void showSection(const std::string& id, bool user) {
        if (id.empty() || id == "search") return;
        if (user && !m_query.empty()) {
            m_query.clear();
            m_search->setText("");
        }
        m_section = id;
        m_prefs.set("section", id);
        const bool empty = m_tracks.empty();
        const std::string emptyText = m_scanning ? "Looking for music in your library folders…"
                                                 : "Add a folder with music, or drop files here. Your Music folder is included automatically.";
        if (id == "albums" || id == "recent") {
            auto albums = m_lib.albums;
            if (id == "recent") {
                std::stable_sort(albums.begin(), albums.end(), [](const AlbumPtr& a, const AlbumPtr& b) { return a->added > b->added; });
                if (albums.size() > 60) albums.resize(60);
            }
            m_grid->setAlbums(albums, id == "recent" ? "Recently Added" : "Albums",
                              id == "albums" && !albums.empty() ? countText(albums.size(), "album", "albums") : "");
            m_gridPage.empty.set(m_scanning ? "Looking for Music…" : "No Music", emptyText, !m_scanning);
            m_gridPage.showEmpty(empty);
            setDetail(m_gridPage);
            focus(m_grid.get());
        } else if (id == "songs" || id == "favorites") {
            const auto list = id == "songs" ? m_tracks : favoriteTracks();
            double total = 0;
            for (const auto& t : list) total += t->duration;
            m_songs->setTracks(list, TrackListView::Mode::Songs);
            m_songs->setHeading(id == "songs" ? "Songs" : "Favorite Songs",
                                list.empty() ? "" : countText(list.size(), "song", "songs") + ", " + totalTime(total));
            if (id == "favorites")
                m_songsPage.empty.set("No Favorite Songs", "Click the heart next to a song to add it here.", false);
            else
                m_songsPage.empty.set(m_scanning ? "Looking for Music…" : "No Music", emptyText, !m_scanning);
            m_songsPage.showEmpty(list.empty());
            setDetail(m_songsPage);
            focus(m_songs.get());
        } else if (id == "artists") {
            std::vector<std::string> names;
            std::vector<std::string> details;
            for (const auto& a : m_lib.artists) {
                names.push_back(a.name);
                details.push_back(countText(a.albums.size(), "album", "albums") + " · " + countText(a.trackCount(), "song", "songs"));
            }
            m_artistList->setArtists(names, details);
            if (m_artistIndex >= static_cast<int>(names.size()) || m_artistIndex < 0) m_artistIndex = names.empty() ? -1 : 0;
            m_artistList->setSelected(m_artistIndex);
            m_artistsPage.empty.set(m_scanning ? "Looking for Music…" : "No Music", emptyText, !m_scanning);
            m_artistsPage.showEmpty(empty);
            m_artistScroll.setHidden(empty);
            showArtist();
            setDetail(m_artistsPage);
            focus(m_artistList.get());
        } else {
            return;
        }
        m_shownAlbum.reset();
        m_window.setTitle("Music");
        rebuildSidebar();
        refreshCurrentMarks();
    }

    void showArtist() {
        if (m_artistIndex < 0 || m_artistIndex >= static_cast<int>(m_lib.artists.size())) {
            m_artistGrid->setAlbums({}, "");
            return;
        }
        const Artist& a = m_lib.artists[static_cast<size_t>(m_artistIndex)];
        m_artistGrid->setAlbums(a.albums, a.name, countText(a.albums.size(), "album", "albums") + ", " + countText(a.trackCount(), "song", "songs"));
        m_artistsPage.scroll->scrollTo({0, 0}, false);
    }

    void showSearch() {
        std::vector<AlbumPtr> albums;
        for (const auto& a : m_lib.albums) {
            if (matches(a->title, m_query) || matches(a->artist, m_query)) albums.push_back(a);
        }
        std::vector<TrackPtr> songs;
        for (const auto& t : m_tracks) {
            if (matches(t->title, m_query) || matches(t->artist, m_query) || matches(t->album, m_query)) songs.push_back(t);
        }
        if (albums.size() > 12) albums.resize(12);
        m_searchAlbums->setAlbums(albums, albums.empty() ? "" : "Albums");
        m_searchAlbums->setHidden(albums.empty());
        m_searchSongs->setTracks(songs, TrackListView::Mode::Songs);
        m_searchSongs->setHeading(songs.empty() ? "" : "Songs", "");
        m_searchSongs->setHidden(songs.empty());
        m_searchPage.empty.set("No Results", "Nothing in your library matches your search.", false);
        m_searchPage.showEmpty(albums.empty() && songs.empty());
        setDetail(m_searchPage);
        m_searchPage.scroll->scrollTo({0, 0}, false);
        m_sidebar.selection("");
        refreshCurrentMarks();
    }

    void openAlbum(const AlbumPtr& album) {
        if (!album) return;
        if (m_shownPage != &m_albumPage) m_returnPage = m_shownPage;
        m_shownAlbum = album;
        requestCover(album);
        auto cover = Canvas([album](Painter& p, const Rect& r) {
            p.drawShadow(r, 10, Color::black(0.28f), 18, {0, 6});
            drawArtwork(p, r, album.get(), 10);
        });
        cover.frame(230, 230);
        m_albumCover = cover.node();
        std::string info;
        if (!album->genre.empty()) info = album->genre;
        if (album->year > 0) info += (info.empty() ? "" : " · ") + std::to_string(album->year);
        auto play = Button(Icon::Play, "Play").style(ButtonStyle::Prominent);
        play.onClick([this, a = std::weak_ptr<Album>(album)] {
            if (auto al = a.lock()) playContext(al->tracks, 0);
        });
        auto shuffle = Button(Icon::Shuffle, "Shuffle");
        shuffle.onClick([this, a = std::weak_ptr<Album>(album)] {
            if (auto al = a.lock()) {
                setShuffle(true);
                playContext(al->tracks, std::uniform_int_distribution<size_t>(0, al->tracks.size() - 1)(m_rng));
            }
        });
        auto header = HStack({cover,
                              VStack({Label(album->title).font(TextStyle::Title1).weight(FontWeight::Bold).lineLimit(2),
                                      Label(album->artist).font(TextStyle::Title3).foreground(theme().colors.accent).lineLimit(1),
                                      Label(info).secondary().font(TextStyle::Subheadline), HStack({play, shuffle}).spacing(10).padding(Insets(10, 0, 0, 0))})
                                  .spacing(4)})
                          .spacing(24)
                          .align(Alignment::BottomLeading)
                          .padding(Insets(24, 28, 12, 28));
        m_albumTracks->setTracks(album->tracks, TrackListView::Mode::Album, album->artist);
        m_albumTracks->setHeading("", "");
        auto footer = Label(countText(album->tracks.size(), "song", "songs") + ", " + totalTime(album->duration()))
                          .secondary()
                          .font(TextStyle::Subheadline)
                          .padding(Insets(0, 28, 28, 28));
        m_albumPage.scroll.node()->setContent(VStack({header, Element(m_albumTracks), footer}).node());
        m_albumPage.scroll->scrollTo({0, 0}, false);
        m_albumPage.showEmpty(false);
        setDetail(m_albumPage);
        m_sidebar.selection("");
        m_window.setTitle(album->title + " — Music");
        refreshCurrentMarks();
        focus(m_albumTracks.get());
    }

    void goBack() {
        if (m_shownPage != &m_albumPage) return;
        if (!m_query.empty() && m_returnPage == &m_searchPage) showSearch();
        else showSection(m_section, false);
    }

    void showCurrentAlbum() {
        if (!m_current) return;
        if (auto a = albumFor(*m_current)) openAlbum(a);
    }

    void focus(Widget* w) {
        if (auto* h = w->host()) h->setFocus(w, false);
    }

    void albumMenu(const AlbumPtr& a, Point at) {
        std::vector<MenuItem> items{
            MenuItem("Play", [this, a] { playContext(a->tracks, 0); }).withIcon(Icon::Play),
            MenuItem("Shuffle", [this, a] {
                setShuffle(true);
                playContext(a->tracks, 0);
            }).withIcon(Icon::Shuffle),
            MenuItem::separator(),
            MenuItem("Play Next", [this, a] { for (auto it = a->tracks.rbegin(); it != a->tracks.rend(); ++it) m_upNext.push_front(*it); updateModeButtons(); }),
            MenuItem("Add to Queue", [this, a] { for (const auto& t : a->tracks) m_upNext.push_back(t); updateModeButtons(); }),
            MenuItem::separator(),
            MenuItem("Show in Files", [this, a] {
                if (!a->tracks.empty() && !m_demo) showInFiles(fs::path(a->tracks.front()->path).parent_path().string());
            }).withIcon(Icon::Folder),
        };
        showMenu(std::move(items), at);
    }

    void trackMenu(const TrackPtr& t, Point at) {
        const bool fav = m_favorites.contains(t->path);
        std::vector<MenuItem> items{
            MenuItem("Play", [this, t] {
                if (m_shownPage) {
                    // Play within the list it was picked from.
                    for (auto* l : {m_songs.get(), m_albumTracks.get(), m_searchSongs.get()}) {
                        const auto& list = l->tracks();
                        auto it = std::find(list.begin(), list.end(), t);
                        if (it != list.end() && l->host()) return playContext(list, static_cast<size_t>(it - list.begin()));
                    }
                }
                playContext({t}, 0);
            }).withIcon(Icon::Play),
            MenuItem("Play Next", [this, t] {
                m_upNext.push_front(t);
                updateModeButtons();
            }),
            MenuItem("Add to Queue", [this, t] {
                m_upNext.push_back(t);
                updateModeButtons();
            }),
            MenuItem::separator(),
            MenuItem(fav ? "Remove from Favorites" : "Add to Favorites", [this, t] { toggleFavorite(t); }).withIcon(fav ? Icon::HeartFill : Icon::Heart),
            MenuItem("Show Album", [this, t] { openAlbum(albumFor(*t)); }).withIcon(Icon::Album),
            MenuItem("Show in Files", [this, t] {
                if (!m_demo) showInFiles(fs::path(t->path).parent_path().string());
            }).withIcon(Icon::Folder),
        };
        showMenu(std::move(items), at);
    }

    void showMenu(std::vector<MenuItem> items, Point at) {
        m_menu->setItems(std::move(items));
        m_menuAnchor->arrange({at.x, at.y, 1, 1});
        m_menu->showFrom(*m_menuAnchor);
    }

    static void showInFiles(const std::string& dir) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", dir.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
    }

    void showQueue() {
        std::vector<Element> rows;
        auto row = [](const TrackPtr& t, bool current) {
            return HStack({VStack({Label(t->title).lineLimit(1).weight(current ? FontWeight::Semibold : FontWeight::Regular),
                                   Label(t->displayArtist()).secondary().font(TextStyle::Subheadline).lineLimit(1)})
                               .spacing(1),
                           Spacer(), Label(formatTime(t->duration)).secondary().font(TextStyle::Subheadline)})
                .spacing(8)
                .align(Alignment::Center);
        };
        if (m_current) {
            rows.push_back(Label("Now Playing").font(TextStyle::Headline));
            rows.push_back(row(m_current, true));
        }
        const auto upcoming = upcomingTracks(40);
        if (!upcoming.empty()) {
            rows.push_back(Label("Playing Next").font(TextStyle::Headline).padding(Insets(8, 0, 0, 0)));
            for (const auto& t : upcoming) rows.push_back(row(t, false));
        }
        if (rows.empty()) rows.push_back(Label("Nothing is queued.").secondary());
        auto content = VStack(rows).spacing(6).width(320);
        std::vector<Element> body{ScrollView(content).height(std::min(460.0f, 40.0f + static_cast<float>(rows.size()) * 40.0f))};
        if (!m_upNext.empty()) {
            body.push_back(HStack({Spacer(), Button("Clear").controlSize(ControlSize::Small).onClick([this] {
                                       m_upNext.clear();
                                       updateModeButtons();
                                   })}));
        }
        Popover(VStack(body).spacing(8)).show(m_queueButton);
    }

    // Library ------------------------------------------------------------------------------------------

    void setLibrary(std::vector<TrackPtr> tracks) {
        // Albums are rebuilt; keep the covers already loaded.
        std::map<std::string, AlbumPtr> old;
        for (const auto& a : m_lib.albums) old[a->key] = a;
        m_tracks = std::move(tracks);
        m_lib = groupLibrary(m_tracks);
        m_albumOf.clear();
        for (const auto& a : m_lib.albums) {
            if (auto it = old.find(a->key); it != old.end() && it->second->coverRequested) {
                a->cover = it->second->cover;
                a->coverFile = it->second->coverFile;
                a->coverRequested = it->second->cover != nullptr;
            }
            for (const auto& t : a->tracks) m_albumOf[t->path] = a;
        }
        if (m_shownPage == &m_albumPage && m_shownAlbum) {
            // Reopen the same album from the new library (or leave it as it is if it's gone).
            for (const auto& a : m_lib.albums) {
                if (a->key == m_shownAlbum->key) openAlbum(a);
            }
        } else if (!m_query.empty()) {
            showSearch();
        } else {
            showSection(m_section, false);
        }
        rebuildSidebar();
    }

    void rescan() {
        if (m_scanner.joinable()) m_scanner.join();
        m_scanning = true;
        m_scanner = std::thread([this, folders = m_folders] {
            auto found = scanLibrary(folders);
            m_app.post([this, found = std::move(found)]() mutable {
                m_scanning = false;
                setLibrary(std::move(found));
                restoreLastTrack();
            });
        });
    }

    void addFolder() {
        m_app.openFolder([this](std::vector<std::string> paths) {
            if (paths.empty()) return;
            if (std::find(m_folders.begin(), m_folders.end(), paths[0]) == m_folders.end()) m_folders.push_back(paths[0]);
            m_prefs.set("folders", joinLines(m_folders));
            rescan();
            if (m_settingsWindow) m_settingsWindow->close();
        }, &m_window);
    }

    TrackPtr trackFor(const std::string& path) {
        std::error_code ec;
        const std::string canonical = fs::weakly_canonical(path, ec).string();
        for (const auto& t : m_tracks) {
            if (t->path == path || t->path == canonical) return t;
        }
        if (!isAudioFile(path) && !fs::is_regular_file(path, ec)) return nullptr;
        auto t = readTrack(path);
        if (!t) return nullptr;
        // Files from outside the library still get an album (and cover) for the display.
        auto extra = groupLibrary({t});
        if (!extra.albums.empty()) m_albumOf[t->path] = extra.albums.front();
        return t;
    }

    AlbumPtr albumFor(const Track& t) const {
        auto it = m_albumOf.find(t.path);
        return it != m_albumOf.end() ? it->second : nullptr;
    }

    void toggleFavorite(const TrackPtr& t) {
        if (!m_favorites.erase(t->path)) m_favorites.insert(t->path);
        m_prefs.set("favorites", joinLines(std::vector<std::string>(m_favorites.begin(), m_favorites.end())));
        rebuildSidebar();
        if (m_shownPage == &m_songsPage && m_section == "favorites") showSection("favorites", false);
        for (auto* l : {m_songs.get(), m_albumTracks.get(), m_searchSongs.get()}) l->invalidate();
    }

    // Covers: one worker loads them in the order they were first drawn.
    void startCoverWorker() {
        m_coverThread = std::thread([this] {
            for (;;) {
                AlbumPtr album;
                {
                    std::unique_lock lock(m_coverMutex);
                    m_coverCv.wait(lock, [this] { return m_coverStop || !m_coverQueue.empty(); });
                    if (m_coverStop) return;
                    album = m_coverQueue.front();
                    m_coverQueue.pop_front();
                }
                std::string file;
                auto img = loadCover(*album, 600, file);
                m_app.post([this, album, img, file] {
                    album->cover = img;
                    album->coverFile = file;
                    for (auto* g : {m_grid.get(), m_artistGrid.get(), m_searchAlbums.get()}) g->invalidate();
                    if (m_albumCover) m_albumCover->invalidate();
                    m_lcd->invalidate();
                    if (m_current && albumFor(*m_current) == album) publishMpris();
                });
            }
        });
    }

    void requestCover(const AlbumPtr& a) {
        if (!a || a->coverRequested) return;
        a->coverRequested = true;
        {
            std::scoped_lock lock(m_coverMutex);
            m_coverQueue.push_back(a);
        }
        m_coverCv.notify_one();
    }

    // Playback -------------------------------------------------------------------------------------------

    void setContext(const std::vector<TrackPtr>& tracks, size_t start) {
        m_context = tracks;
        m_order.resize(tracks.size());
        for (size_t i = 0; i < tracks.size(); ++i) m_order[i] = i;
        if (m_shuffle && !m_order.empty()) {
            std::shuffle(m_order.begin(), m_order.end(), m_rng);
            // The chosen song plays first.
            auto it = std::find(m_order.begin(), m_order.end(), start);
            std::iter_swap(m_order.begin(), it);
            m_pos = 0;
        } else {
            m_pos = std::min(start, tracks.empty() ? 0 : tracks.size() - 1);
        }
    }

    void playContext(const std::vector<TrackPtr>& tracks, size_t start) {
        if (tracks.empty() || start >= tracks.size()) return;
        setContext(tracks, start);
        load(m_context[m_order[m_pos]], true);
    }

    bool load(const TrackPtr& t, bool autoplay) {
        std::string error;
        m_current = t;
        if (!m_player.open(t->path, error)) {
            m_error = fs::path(t->path).filename().string() + ": " + error;
            m_player.close();
            refreshNowPlaying();
            showError("Can't Play “" + t->title + "”", error);
            return false;
        }
        if (auto a = albumFor(*t)) requestCover(a);
        if (autoplay) m_player.play();
        m_prefs.set("lastTrack", t->path);
        refreshNowPlaying();
        m_mpris->seeked(0);
        scheduleTick();
        return true;
    }

    void showError(const std::string& title, const std::string& text) {
        auto pop = Popover(VStack({Label(title).font(TextStyle::Headline), Label(text).secondary().maxWidth(320)}).spacing(6).width(340));
        pop.show(Element(m_lcd));
    }

    void togglePlay() {
        if (!m_player.isOpen()) {
            if (m_current) load(m_current, true);
            else if (!m_tracks.empty()) playContext(m_shownPage == &m_albumPage && m_shownAlbum ? m_shownAlbum->tracks : m_tracks, 0);
            return;
        }
        if (m_player.isPlaying()) m_player.pause();
        else m_player.play();
        refreshNowPlaying();
        scheduleTick();
    }

    /// Next song: the queue first, then the context in order (shuffled or not). At the end of the context
    /// it wraps with Repeat All, or stops at the first song.
    void advance(bool automatic) {
        if (automatic && m_repeat == 2 && m_current) {
            load(m_current, true);
            return;
        }
        if (!m_upNext.empty()) {
            TrackPtr t = m_upNext.front();
            m_upNext.pop_front();
            load(t, true);
            return;
        }
        if (m_context.empty()) return;
        if (m_pos + 1 < m_order.size()) {
            ++m_pos;
        } else if (m_repeat == 1) {
            if (m_shuffle) std::shuffle(m_order.begin(), m_order.end(), m_rng);
            m_pos = 0;
        } else {
            m_pos = 0;
            load(m_context[m_order[0]], false);
            return;
        }
        load(m_context[m_order[m_pos]], true);
    }

    void previous() {
        // Like every player: back to the start first, the previous song on a second press.
        if (m_player.isOpen() && m_player.position() > 3) return seek(0);
        if (m_context.empty()) return seek(0);
        if (m_pos > 0) --m_pos;
        else if (m_repeat == 1) m_pos = m_order.size() - 1;
        load(m_context[m_order[m_pos]], true);
    }

    void seek(double s) {
        if (!m_player.isOpen()) return;
        m_player.seek(s);
        m_mpris->seeked(s);
        m_lcd->setTime(s, m_player.duration());
    }

    void setVolume(float v) {
        m_volume = std::clamp(v, 0.0f, 1.0f);
        // Perceived loudness is closer to the cube of the slider than linear.
        m_player.setVolume(m_volume * m_volume * m_volume);
        m_volumeSlider.setValue(m_volume);
        m_volumeButton->setIcon(m_volume <= 0.001f ? Icon::SpeakerSlash : Icon::Speaker);
        m_prefs.set("volume", m_volume);
        publishMpris();
    }

    void setShuffle(bool on) {
        if (on == m_shuffle) return;
        m_shuffle = on;
        m_prefs.set("shuffle", on);
        // Reorder what's left, keeping the current song where it is.
        if (!m_context.empty()) {
            const size_t current = m_order[m_pos];
            setContext(m_context, current);
        }
        updateModeButtons();
    }

    void setRepeat(int mode) {
        m_repeat = mode;
        m_prefs.set("repeat", mode);
        updateModeButtons();
    }

    void updateModeButtons() {
        const Color accent = theme().colors.accent;
        m_shuffleButton->setForeground(m_shuffle ? std::optional(accent) : std::nullopt);
        m_repeatButton->setForeground(m_repeat ? std::optional(accent) : std::nullopt);
        m_repeatButton->setIcon(m_repeat == 2 ? Icon::RepeatOne : Icon::Repeat);
        m_repeatButton.tooltip(m_repeat == 0 ? "Repeat: Off" : m_repeat == 1 ? "Repeat: All" : "Repeat: One");
        m_shuffleButton.tooltip(m_shuffle ? "Shuffle: On" : "Shuffle: Off");
        m_volumeButton->setIcon(m_volume <= 0.001f ? Icon::SpeakerSlash : Icon::Speaker);
        publishMpris();
    }

    std::vector<TrackPtr> upcomingTracks(size_t limit) const {
        std::vector<TrackPtr> out(m_upNext.begin(), m_upNext.end());
        for (size_t i = m_pos + 1; i < m_order.size() && out.size() < limit; ++i) out.push_back(m_context[m_order[i]]);
        if (out.size() > limit) out.resize(limit);
        return out;
    }

    void refreshNowPlaying() {
        const AlbumPtr album = m_current ? albumFor(*m_current) : nullptr;
        m_lcd->setTrack(m_current.get(), album.get());
        m_lcd->setTime(m_player.position(), m_player.isOpen() ? m_player.duration() : (m_current ? m_current->duration : 0));
        const bool playing = m_player.isPlaying();
        m_playButton->setIcon(playing ? Icon::Pause : Icon::Play);
        m_playButton.tooltip(playing ? "Pause (Space)" : "Play (Space)");
        if (m_current) m_window.setTitle(m_current->title + " — " + m_current->displayArtist());
        refreshCurrentMarks();
        publishMpris();
    }

    void refreshCurrentMarks() {
        const bool playing = m_player.isPlaying();
        for (auto* l : {m_songs.get(), m_albumTracks.get(), m_searchSongs.get()}) l->setCurrent(m_current.get(), playing);
    }

    /// Progress and end-of-song checks, four times a second while something plays (nothing runs when idle).
    void scheduleTick() {
        if (m_ticking || !m_player.isPlaying()) return;
        m_ticking = true;
        m_app.after(0.25, [this] {
            m_ticking = false;
            if (!m_player.isOpen()) return;
            m_lcd->setTime(m_player.position(), m_player.duration());
            m_mpris->setPosition(m_player.position());
            if (m_player.ended()) return advance(true);
            scheduleTick();
        });
    }

    // MPRIS ------------------------------------------------------------------------------------------------

    void setupMpris() {
        Mpris::Handlers h;
        h.raise = [this] { m_window.raise(); };
        h.quit = [this] { m_app.quit(); };
        h.playPause = [this] { togglePlay(); };
        h.play = [this] {
            if (!m_player.isPlaying()) togglePlay();
        };
        h.pause = [this] {
            if (m_player.isPlaying()) togglePlay();
        };
        h.stop = [this] {
            m_player.pause();
            seek(0);
            refreshNowPlaying();
        };
        h.next = [this] { advance(false); };
        h.previous = [this] { previous(); };
        h.seekBy = [this](double d) { seek(std::max(0.0, m_player.position() + d)); };
        h.setPosition = [this](double s) { seek(s); };
        h.setVolume = [this](double v) { setVolume(static_cast<float>(v)); };
        h.setShuffle = [this](bool on) { setShuffle(on); };
        h.setLoop = [this](const std::string& s) { setRepeat(s == "Track" ? 2 : s == "Playlist" ? 1 : 0); };
        m_mpris = std::make_unique<Mpris>(m_app, MAYO_DESKTOP_ID, std::move(h));
    }

    void publishMpris() {
        if (!m_mpris) return;
        Mpris::State s;
        if (m_current) {
            const AlbumPtr album = albumFor(*m_current);
            s.status = m_player.isPlaying() ? "Playing" : "Paused";
            s.trackPath = m_current->path;
            s.title = m_current->title;
            s.artist = m_current->artist.empty() ? m_current->displayArtist() : m_current->artist;
            s.album = m_current->album;
            s.albumArtist = m_current->albumArtist;
            s.trackNumber = m_current->number;
            s.length = m_player.isOpen() ? m_player.duration() : m_current->duration;
            if (album && !album->coverFile.empty()) {
                if (char* uri = g_uri(album->coverFile)) {
                    s.artUrl = uri;
                    std::free(uri);
                }
            }
        }
        s.volume = m_volume;
        s.shuffle = m_shuffle;
        s.loop = m_repeat == 2 ? "Track" : m_repeat == 1 ? "Playlist" : "None";
        s.canNext = !m_upNext.empty() || m_pos + 1 < m_order.size() || (m_repeat == 1 && !m_context.empty());
        s.canPrevious = m_current != nullptr;
        m_mpris->update(s);
        m_mpris->setPosition(m_player.position());
    }

    /// file:// URI for a path (percent-encoding what needs it); malloc'ed.
    static char* g_uri(const std::string& path) {
        std::string out = "file://";
        static const char* hex = "0123456789ABCDEF";
        for (unsigned char c : path) {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
        return strdup(out.c_str());
    }

    // Settings ------------------------------------------------------------------------------------------

public:
    void openSettings() {
        if (m_settingsWindow) {
            m_settingsWindow->raise();
            return;
        }
        auto& w = m_app.createWindow({.title = "Music Settings", .width = 540, .height = 520, .minWidth = 440, .minHeight = 320});
        auto folderList = VStack({}).spacing(6);
        for (const auto& f : m_folders) {
            auto remove = Button(Icon::Minus).style(ButtonStyle::Borderless).tooltip("Remove from Library");
            remove.onClick([this, f] {
                std::erase(m_folders, f);
                m_prefs.set("folders", joinLines(m_folders));
                rescan();
                m_app.post([this] {
                    if (m_settingsWindow) m_settingsWindow->close();
                });
            });
            folderList.add(HStack({Label(f).lineLimit(1), Spacer(), remove}).spacing(8).align(Alignment::Center));
        }
        if (m_folders.empty()) folderList.add(Label("No folders").secondary());
        auto rescanButton = Button("Rescan").onClick([this] { rescan(); });
        auto form = Form({
            AppearanceSettings(m_app),
            FormSection("Library",
                        {FormRow(folderList), FormRow(HStack({rescanButton, Spacer(), Button(Icon::FolderPlus, "Add Folder…").onClick([this] { addFolder(); })}).spacing(8))},
                        "Folders are searched for music, including their subfolders."),
        });
        w.setContent(ScrollView(VStack({form}).padding(Insets(12, 24, 24, 24))));
        w.onKey([&w](const KeyEvent& e) {
            if (e.key == Key::Escape || (e.primary() && e.key == Key::W)) {
                w.close();
                return true;
            }
            return false;
        });
        w.onClosed([this] { m_settingsWindow = nullptr; });
        m_settingsWindow = &w;
    }

    bool onKey(const KeyEvent& e) {
        if (e.key == Key::Space && !e.primary()) return togglePlay(), true;
        if (e.key == Key::Escape) return goBack(), true;
        if (!e.primary()) return false;
        switch (e.key) {
            case Key::Right: advance(false); return true;
            case Key::Left: previous(); return true;
            case Key::Up: setVolume(m_volume + 0.1f); return true;
            case Key::Down: setVolume(m_volume - 0.1f); return true;
            case Key::L: showCurrentAlbum(); return true;
            case Key::F:
                if (auto* h = m_search->host()) h->setFocus(m_search.node().get(), true);
                return true;
            case Key::O: chooseFiles(); return true;
            case Key::Comma: openSettings(); return true;
            case Key::Q: m_app.quit(); return true;
            case Key::Num1: showSection("recent", true); return true;
            case Key::Num2: showSection("artists", true); return true;
            case Key::Num3: showSection("albums", true); return true;
            case Key::Num4: showSection("songs", true); return true;
            case Key::Num5: showSection("favorites", true); return true;
            default: return false;
        }
    }

    void chooseFiles() {
        m_app.openFiles({{"Music", "mp3;flac;ogg;opus;m4a;aac;wav;wma;aiff;ape;wv"}, {"All files", "*"}}, true,
                        [this](std::vector<std::string> paths) { openPaths(paths); }, &m_window);
    }

private:
    void demoStep(const std::string& step) {
        const size_t colon = step.find(':');
        const std::string cmd = step.substr(0, colon);
        const std::string arg = colon == std::string::npos ? "" : step.substr(colon + 1);
        if (cmd == "recent" || cmd == "artists" || cmd == "albums" || cmd == "songs" || cmd == "favorites") {
            showSection(cmd, true);
        } else if (cmd == "album" || cmd == "play") {
            const auto n = static_cast<size_t>(std::max(0, std::atoi(arg.c_str())));
            if (n >= m_lib.albums.size()) return;
            if (cmd == "album") openAlbum(m_lib.albums[n]);
            else playContext(m_lib.albums[n]->tracks, 0);
        } else if (cmd == "search") {
            m_search->setText(arg);
            m_query = lowerAscii(arg);
            showSearch();
        } else if (cmd == "queue") {
            showQueue();
        } else if (cmd == "settings") {
            openSettings();
        } else if (cmd == "pause") {
            if (m_player.isPlaying()) togglePlay();
        } else if (cmd == "seek") {
            seek(std::atof(arg.c_str()));
        } else if (cmd == "favorite" && m_current) {
            toggleFavorite(m_current);
        }
    }

    App& m_app;
    Window& m_window;
    Settings& m_prefs;
    bool m_demo = false;
    AudioPlayer m_player;
    std::unique_ptr<Mpris> m_mpris;
    std::mt19937 m_rng{std::random_device{}()};

    // Library
    std::vector<std::string> m_folders;
    std::vector<TrackPtr> m_tracks;
    Collection m_lib;
    std::map<std::string, AlbumPtr> m_albumOf;  // by track path
    std::set<std::string> m_favorites;
    bool m_scanning = false;
    std::thread m_scanner;

    // Playback
    std::vector<TrackPtr> m_context;
    std::vector<size_t> m_order;
    size_t m_pos = 0;
    std::deque<TrackPtr> m_upNext;
    TrackPtr m_current;
    std::string m_error;
    bool m_shuffle = false;
    int m_repeat = 0;  // 0 off, 1 all, 2 one
    float m_volume = 0.8f;
    float m_unmutedVolume = 0.8f;
    bool m_ticking = false;

    // Covers
    std::mutex m_coverMutex;
    std::condition_variable m_coverCv;
    std::deque<AlbumPtr> m_coverQueue;
    bool m_coverStop = false;
    std::thread m_coverThread;

    // Navigation
    std::string m_section;
    std::string m_query;
    AlbumPtr m_shownAlbum;
    int m_artistIndex = 0;

    // Widgets
    TextField m_search = SearchField("Search");
    Sidebar m_sidebar{std::vector<SidebarItem>{}};
    std::shared_ptr<AlbumGridView> m_grid = std::make_shared<AlbumGridView>();
    std::shared_ptr<TrackListView> m_songs = std::make_shared<TrackListView>();
    std::shared_ptr<TrackListView> m_albumTracks = std::make_shared<TrackListView>();
    std::shared_ptr<ArtistListView> m_artistList = std::make_shared<ArtistListView>();
    std::shared_ptr<AlbumGridView> m_artistGrid = std::make_shared<AlbumGridView>();
    std::shared_ptr<AlbumGridView> m_searchAlbums = std::make_shared<AlbumGridView>();
    std::shared_ptr<TrackListView> m_searchSongs = std::make_shared<TrackListView>();
    Page m_gridPage{Element(m_grid)};
    Page m_songsPage{Element(m_songs)};
    Page m_albumPage{VStack({})};
    Page m_artistsPage{Element(m_artistGrid)};
    Page m_searchPage{VStack({Element(m_searchAlbums), Element(m_searchSongs)})};
    ScrollView m_artistScroll{VStack({})};
    SplitView m_split{m_sidebar, Element(m_gridPage.view)};
    Page* m_shownPage = nullptr;
    Page* m_returnPage = nullptr;
    std::shared_ptr<CanvasView> m_albumCover;
    std::shared_ptr<PointAnchor> m_menuAnchor = std::make_shared<PointAnchor>();
    std::shared_ptr<MenuView> m_menu = std::make_shared<MenuView>(std::vector<MenuItem>{});
    std::shared_ptr<NowPlayingView> m_lcd = std::make_shared<NowPlayingView>();
    Button m_back{Icon::ChevronLeft};
    Button m_shuffleButton{Icon::Shuffle};
    Button m_playButton{Icon::Play};
    Button m_repeatButton{Icon::Repeat};
    Button m_volumeButton{Icon::Speaker};
    Button m_queueButton{Icon::List};
    Slider m_volumeSlider{0.8f, 0, 1};
    Window* m_settingsWindow = nullptr;
};

}  // namespace

int main(int argc, char** argv) {
    av_log_set_level(AV_LOG_QUIET);  // damaged tags in a big library would flood the terminal
    App app("music", MAYO_DESKTOP_ID);
    auto& window = app.createWindow({.title = "Music", .width = 1120, .height = 720, .minWidth = 780, .minHeight = 460, .autosaveName = "main"});
    window.setTitleVisible(false);
    MusicApp music(app, window);
    window.onKey([&](const KeyEvent& e) { return music.onKey(e); });
    window.onFilesDropped([&](const std::vector<std::string>& paths) { music.openPaths(paths); });
    music.loadLibrary();
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.emplace_back(argv[i]);
    if (!files.empty()) music.openPaths(files);
    else music.restoreLastTrack();
    if (const char* demo = std::getenv("MAYO_DEMO")) music.runDemo(demo);
    const int code = app.run();
    music.saveState();
    return code;
}
