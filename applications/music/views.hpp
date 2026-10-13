// Custom widgets of the music app: the album grid, the song list (with sortable columns), the artist list,
// the "now playing" display in the toolbar and the page container that runs under the toolbar.
#pragma once

#include <mayonnaise/mayonnaise.hpp>

#include <functional>
#include <string>
#include <vector>

#include "library.hpp"

namespace music {

/// Cover art (aspect fill) or a tinted placeholder with a note.
void drawArtwork(mayo::Painter& p, const mayo::Rect& r, const Album* album, float radius);
/// Cuts `text` with an ellipsis so it fits `width`.
std::string fit(mayo::Painter& p, const std::string& text, const mayo::Font& font, float width);

// Album grid -----------------------------------------------------------------------------------------------

class AlbumGridView : public mayo::Widget {
public:
    std::function<void(const AlbumPtr&)> onOpen;
    std::function<void(const AlbumPtr&)> onPlay;
    std::function<void(const AlbumPtr&, mayo::Point)> onContextMenu;
    /// Asked for albums whose covers are about to be drawn (loads them lazily).
    std::function<void(const AlbumPtr&)> needCover;

    void setScroller(mayo::ScrollViewView* s) { m_scroller = s; }
    void setAlbums(std::vector<AlbumPtr> albums, std::string title, std::string subtitle = {});
    [[nodiscard]] bool isEmpty() const { return m_albums.empty(); }

    [[nodiscard]] bool acceptsFocus() const override { return true; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    [[nodiscard]] bool expandsContent(mayo::Axis axis) const override { return axis == mayo::Axis::Horizontal; }
    mayo::Size measureContent(mayo::Size available) override;
    void arrangeContent(const mayo::Rect& content) override;
    void draw(mayo::Painter& p) override;
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerLeave() override;
    bool onKeyDown(const mayo::KeyEvent& e) override;

private:
    float layout(float width);
    [[nodiscard]] mayo::Rect cardRect(int i) const;
    [[nodiscard]] mayo::Rect playButtonRect(int i) const;
    [[nodiscard]] int cardAt(mayo::Point p) const;
    void select(int i);

    std::vector<AlbumPtr> m_albums;
    std::vector<mayo::Rect> m_rects;  // relative to the content origin
    std::string m_title;
    std::string m_subtitle;
    float m_layoutWidth = -1;
    float m_height = 0;
    float m_cover = 0;
    int m_hover = -1;
    bool m_hoverPlay = false;
    int m_selected = -1;
    mayo::ScrollViewView* m_scroller = nullptr;
};

// Song list ------------------------------------------------------------------------------------------------

class TrackListView : public mayo::Widget {
public:
    enum class Mode {
        Songs,  ///< title, artist, album, time; a header row sorts by column
        Album,  ///< track numbers and disc headings, artist only when it differs from the album's
    };
    /// Play `tracks` (the list as shown) from `index`.
    std::function<void(const std::vector<TrackPtr>& tracks, size_t index)> onPlay;
    std::function<void(const TrackPtr&, mayo::Point)> onContextMenu;
    std::function<bool(const Track&)> isFavorite;
    std::function<void(const TrackPtr&)> onToggleFavorite;

    void setScroller(mayo::ScrollViewView* s) { m_scroller = s; }
    void setTracks(std::vector<TrackPtr> tracks, Mode mode, std::string albumArtist = {});
    [[nodiscard]] const std::vector<TrackPtr>& tracks() const { return m_tracks; }
    /// The track being played (drawn with a speaker) and whether it is playing.
    void setCurrent(const Track* track, bool playing);
    /// Title drawn above the list (Songs and search pages).
    void setHeading(std::string title, std::string subtitle);
    /// Sort column for Songs mode: 0 title, 1 artist, 2 album, 3 time, 4 date added
    void setSort(int column, bool ascending);

    [[nodiscard]] bool acceptsFocus() const override { return true; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    [[nodiscard]] bool expandsContent(mayo::Axis axis) const override { return axis == mayo::Axis::Horizontal; }
    mayo::Size measureContent(mayo::Size available) override;
    void draw(mayo::Painter& p) override;
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerLeave() override;
    bool onKeyDown(const mayo::KeyEvent& e) override;
    void onFocusChanged(bool /*focused*/) override { invalidate(); }

private:
    struct Row {
        int track = -1;     ///< index into m_tracks, or -1 for a disc heading
        std::string label;  ///< disc heading text
    };
    struct Columns {
        float number, title, artist, album, heart, time, right;
    };
    void rebuildRows();
    void sortTracks();
    [[nodiscard]] float top() const;
    [[nodiscard]] Columns columns() const;
    [[nodiscard]] int rowAt(mayo::Point p) const;
    [[nodiscard]] mayo::Rect rowRect(int row) const;
    void select(int row);
    void play(int row);

    std::vector<TrackPtr> m_tracks;
    std::vector<Row> m_rows;
    Mode m_mode = Mode::Songs;
    std::string m_albumArtist;
    std::string m_heading;
    std::string m_subheading;
    const Track* m_current = nullptr;
    bool m_playing = false;
    int m_sort = 1;  // artist, like Music's Songs view
    bool m_ascending = true;
    int m_hover = -1;
    bool m_hoverHeart = false;
    int m_selected = -1;
    mayo::ScrollViewView* m_scroller = nullptr;
};

// Artists ---------------------------------------------------------------------------------------------------

class ArtistListView : public mayo::Widget {
public:
    std::function<void(int index)> onSelect;
    void setArtists(std::vector<std::string> names, std::vector<std::string> details);
    void setSelected(int index);
    [[nodiscard]] int selected() const { return m_selected; }

    [[nodiscard]] bool acceptsFocus() const override { return true; }
    [[nodiscard]] bool isInteractive() const override { return true; }
    [[nodiscard]] bool expandsContent(mayo::Axis axis) const override { return axis == mayo::Axis::Horizontal; }
    mayo::Size measureContent(mayo::Size available) override;
    void draw(mayo::Painter& p) override;
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerLeave() override;
    bool onKeyDown(const mayo::KeyEvent& e) override;

private:
    [[nodiscard]] mayo::Rect rowRect(int i) const;
    std::vector<std::string> m_names;
    std::vector<std::string> m_details;
    int m_selected = -1;
    int m_hover = -1;
};

// Now playing ------------------------------------------------------------------------------------------------

/// The toolbar's centre display: artwork, title, artist — album and a thin timeline you can drag.
class NowPlayingView : public mayo::Widget {
public:
    std::function<void(double seconds)> onSeek;
    std::function<void()> onArtworkClick;

    void setTrack(const Track* track, const Album* album);
    void setTime(double position, double duration);

    /// Width kept free for the controls after it in the toolbar.
    float reserve = 190;

    [[nodiscard]] bool isInteractive() const override { return true; }
    mayo::Size measureContent(mayo::Size available) override;
    void draw(mayo::Painter& p) override;
    bool onPointerDown(const mayo::PointerEvent& e) override;
    void onPointerMove(const mayo::PointerEvent& e) override;
    void onPointerUp(const mayo::PointerEvent& e) override;
    void onHoverChanged(bool hovered) override;

private:
    [[nodiscard]] mayo::Rect artRect() const;
    [[nodiscard]] mayo::Rect barRect() const;
    [[nodiscard]] double timeAt(float x) const;

    const Track* m_track = nullptr;
    const Album* m_album = nullptr;
    double m_position = 0;
    double m_duration = 0;
    bool m_dragging = false;
    double m_dragTime = 0;
    mayo::Animated<float> m_hoverAnim{0};
};

// Pages -------------------------------------------------------------------------------------------------------

/// Root of the detail side: runs under the toolbar (the content scrolls beneath its glass) and stacks a
/// scroll view with an empty-state message.
class PageView : public mayo::Widget {
public:
    std::shared_ptr<mayo::ScrollViewView> scroll;
    std::shared_ptr<mayo::Widget> empty;
    /// Artists page: a fixed-width list on the leading side.
    std::shared_ptr<mayo::Widget> leading;
    float leadingWidth = 250;

    [[nodiscard]] bool extendsIntoTitleBar() const override { return true; }
    [[nodiscard]] bool expandsContent(mayo::Axis /*axis*/) const override { return true; }
    void arrangeContent(const mayo::Rect& c) override;
    void draw(mayo::Painter& p) override;
};

}  // namespace music
