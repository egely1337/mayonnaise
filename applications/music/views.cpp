#include "views.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace mayo;

namespace music {

namespace {

constexpr float kPad = 28;
constexpr float kGap = 22;
constexpr float kMinCard = 160;
constexpr float kCardText = 46;
constexpr float kRadius = 8;
constexpr float kRowHeight = 34;
constexpr float kHeaderRow = 26;

/// Heading block at the top of a page: large title, optional subtitle. Returns its height.
float drawHeading(Painter& p, const Rect& c, const std::string& title, const std::string& subtitle) {
    if (title.empty()) return 8;
    const Theme& t = theme();
    p.drawText(title, {c.x + kPad, c.y + 14, c.width - kPad * 2, 34}, t.font(TextStyle::Title1).withWeight(FontWeight::Bold),
               t.colors.label);
    if (!subtitle.empty())
        p.drawText(subtitle, {c.x + kPad, c.y + 48, c.width - kPad * 2, 18}, t.font(TextStyle::Subheadline), t.colors.secondaryLabel);
    return subtitle.empty() ? 62 : 80;
}

float headingHeight(const std::string& title, const std::string& subtitle) {
    if (title.empty()) return 8;
    return subtitle.empty() ? 62 : 80;
}

Color tintFor(const std::string& key) {
    const auto& c = theme().colors;
    const Color palette[] = {c.red, c.orange, c.yellow, c.green, c.teal, c.blue, c.indigo, c.purple, c.pink};
    return palette[std::hash<std::string>{}(key) % std::size(palette)];
}

}  // namespace

void drawArtwork(Painter& p, const Rect& r, const Album* album, float radius) {
    const Theme& t = theme();
    if (album && album->cover) {
        const ImageData& img = *album->cover;
        const float ia = static_cast<float>(img.width()) / static_cast<float>(std::max(1, img.height()));
        const float ra = r.width / std::max(1.0f, r.height);
        Rect src{0, 0, 1, 1};
        if (ia > ra) {
            src.width = ra / ia;
            src.x = (1 - src.width) * 0.5f;
        } else {
            src.height = ia / ra;
            src.y = (1 - src.height) * 0.5f;
        }
        p.drawImage(r, img, radius, 1, src);
    } else {
        const Color tint = album ? tintFor(album->key) : t.colors.gray;
        p.fillVerticalGradient(r, radius, tint.mix(t.colors.windowBackground, 0.55f), tint.mix(t.colors.windowBackground, 0.8f));
        const float s = std::min(r.width, r.height) * 0.36f;
        p.drawIcon(Icon::MusicNote, {r.center().x - s * 0.5f, r.center().y - s * 0.5f, s, s}, tint.mix(t.colors.label, 0.25f).withAlpha(0.7f),
                   std::max(0.8f, s / 40));
    }
    p.strokeRoundedRect(r, radius, t.colors.separator.withAlpha(0.5f), 0.5f);
}

std::string fit(Painter& p, const std::string& text, const Font& font, float width) {
    if (width <= 0) return {};
    if (p.measureText(text, font) <= width) return text;
    std::vector<size_t> cuts;
    for (size_t i = 1; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) cuts.push_back(i);
    }
    size_t lo = 0;
    size_t hi = cuts.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        if (p.measureText(text.substr(0, cuts[mid - 1]) + "…", font) <= width) lo = mid;
        else hi = mid - 1;
    }
    return lo == 0 ? "…" : text.substr(0, cuts[lo - 1]) + "…";
}

// Album grid -----------------------------------------------------------------------------------------------

void AlbumGridView::setAlbums(std::vector<AlbumPtr> albums, std::string title, std::string subtitle) {
    m_albums = std::move(albums);
    m_title = std::move(title);
    m_subtitle = std::move(subtitle);
    m_layoutWidth = -1;
    m_hover = -1;
    m_selected = std::min(m_selected, static_cast<int>(m_albums.size()) - 1);
    invalidateLayout();
}

float AlbumGridView::layout(float width) {
    if (width == m_layoutWidth) return m_height;
    m_layoutWidth = width;
    const float avail = std::max(kMinCard, width - kPad * 2);
    const int cols = std::max(1, static_cast<int>((avail + kGap) / (kMinCard + kGap)));
    const float cardW = std::floor((avail - kGap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
    m_cover = cardW;
    const float top = headingHeight(m_title, m_subtitle);
    m_rects.resize(m_albums.size());
    for (size_t i = 0; i < m_albums.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        m_rects[i] = {kPad + static_cast<float>(col) * (cardW + kGap), top + static_cast<float>(row) * (cardW + kCardText + kGap), cardW,
                      cardW + kCardText};
    }
    const auto rows = static_cast<float>((m_albums.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
    m_height = top + rows * (cardW + kCardText + kGap) + 20;
    return m_height;
}

Size AlbumGridView::measureContent(Size available) {
    const float w = std::isfinite(available.width) ? available.width : 900.0f;
    return {w, layout(w)};
}

void AlbumGridView::arrangeContent(const Rect& content) { layout(content.width); }

Rect AlbumGridView::cardRect(int i) const {
    const Rect r = m_rects[static_cast<size_t>(i)];
    const Rect c = contentRect();
    return {c.x + r.x, c.y + r.y, r.width, r.height};
}

Rect AlbumGridView::playButtonRect(int i) const {
    const Rect card = cardRect(i);
    return {card.x + 10, card.y + m_cover - 44, 34, 34};
}

int AlbumGridView::cardAt(Point p) const {
    for (int i = 0; i < static_cast<int>(m_rects.size()); ++i) {
        if (cardRect(i).contains(p)) return i;
    }
    return -1;
}

void AlbumGridView::draw(Painter& p) {
    const Theme& t = theme();
    const Rect c = contentRect();
    drawHeading(p, c, m_title, m_subtitle);
    const Rect clip = p.clipBounds();
    const Font titleFont = t.font(TextStyle::Callout).withWeight(FontWeight::Medium);
    const Font artistFont = t.font(TextStyle::Callout);
    for (int i = 0; i < static_cast<int>(m_albums.size()); ++i) {
        const Rect card = cardRect(i);
        if (card.bottom() < clip.y || card.y > clip.bottom()) continue;
        const AlbumPtr& a = m_albums[static_cast<size_t>(i)];
        if (!a->coverRequested && needCover) needCover(a);
        const Rect art{card.x, card.y, m_cover, m_cover};
        const bool hovered = i == m_hover;
        p.drawShadow(art, kRadius, Color::black(hovered ? 0.3f : 0.16f), hovered ? 14 : 6, {0, hovered ? 4.0f : 2.0f});
        drawArtwork(p, art, a.get(), kRadius);
        if (hovered) {
            p.fillRoundedRect(art, kRadius, Color::black(0.12f));
            const Rect b = playButtonRect(i);
            p.fillCircle(b.center(), b.width * 0.5f, m_hoverPlay ? t.colors.accent : Color::black(0.55f));
            const float s = 14;
            p.drawIcon(Icon::Play, {b.center().x - s * 0.5f + 1, b.center().y - s * 0.5f, s, s}, Color::white(), 1.4f);
        }
        if (i == m_selected && isFocused()) p.strokeRoundedRect(art.outset(3), kRadius + 3, t.colors.focusRing, 3);
        p.drawText(fit(p, a->title, titleFont, card.width), {card.x, art.bottom() + 6, card.width, 18}, titleFont, t.colors.label);
        p.drawText(fit(p, a->artist, artistFont, card.width), {card.x, art.bottom() + 24, card.width, 18}, artistFont,
                   t.colors.secondaryLabel);
    }
}

bool AlbumGridView::onPointerDown(const PointerEvent& e) {
    const int i = cardAt(e.position);
    if (i < 0) return false;
    if (auto* h = host()) h->setFocus(this, false);
    m_selected = i;
    invalidate();
    const AlbumPtr a = m_albums[static_cast<size_t>(i)];
    if (e.button == MouseButton::Right) {
        if (onContextMenu) onContextMenu(a, e.position);
    } else if (e.button == MouseButton::Left) {
        if (playButtonRect(i).contains(e.position)) {
            if (onPlay) onPlay(a);
        } else if (onOpen) {
            onOpen(a);
        }
    }
    return true;
}

void AlbumGridView::onPointerMove(const PointerEvent& e) {
    const int h = cardAt(e.position);
    const bool play = h >= 0 && playButtonRect(h).contains(e.position);
    if (h != m_hover || play != m_hoverPlay) {
        m_hover = h;
        m_hoverPlay = play;
        setCursor(h >= 0 ? std::optional(CursorShape::PointingHand) : std::nullopt);
        invalidate();
    }
}

void AlbumGridView::onPointerLeave() {
    m_hover = -1;
    invalidate();
}

void AlbumGridView::select(int i) {
    if (m_albums.empty()) return;
    m_selected = std::clamp(i, 0, static_cast<int>(m_albums.size()) - 1);
    invalidate();
    if (!m_scroller) return;
    const float inset = host() ? host()->titleBarHeight() : 0.0f;
    const Rect r = m_rects[static_cast<size_t>(m_selected)];
    float offset = m_scroller->offset().y;
    const float visible = m_scroller->frame().height - inset;
    if (r.y - kGap < offset) offset = r.y - kGap;
    if (r.bottom() + kGap > offset + visible) offset = r.bottom() + kGap - visible;
    if (offset != m_scroller->offset().y) m_scroller->scrollTo({0, std::max(0.0f, offset)}, true);
}

bool AlbumGridView::onKeyDown(const KeyEvent& e) {
    if (m_albums.empty()) return false;
    int cols = 1;
    while (cols < static_cast<int>(m_rects.size()) && m_rects[static_cast<size_t>(cols)].y == m_rects[0].y) ++cols;
    const int cur = std::max(0, m_selected);
    switch (e.key) {
        case Key::Left: select(cur - 1); return true;
        case Key::Right: select(m_selected < 0 ? 0 : cur + 1); return true;
        case Key::Up: select(cur - cols); return true;
        case Key::Down: select(m_selected < 0 ? 0 : cur + cols); return true;
        case Key::Home: select(0); return true;
        case Key::End: select(static_cast<int>(m_albums.size()) - 1); return true;
        case Key::Enter:
            if (m_selected >= 0 && onOpen) onOpen(m_albums[static_cast<size_t>(m_selected)]);
            return true;
        default: return false;
    }
}

// Song list ------------------------------------------------------------------------------------------------

void TrackListView::setTracks(std::vector<TrackPtr> tracks, Mode mode, std::string albumArtist) {
    m_tracks = std::move(tracks);
    m_mode = mode;
    m_albumArtist = std::move(albumArtist);
    m_hover = -1;
    m_selected = -1;
    if (m_mode == Mode::Songs) sortTracks();
    rebuildRows();
    invalidateLayout();
}

void TrackListView::setCurrent(const Track* track, bool playing) {
    if (track == m_current && playing == m_playing) return;
    m_current = track;
    m_playing = playing;
    invalidate();
}

void TrackListView::setHeading(std::string title, std::string subtitle) {
    m_heading = std::move(title);
    m_subheading = std::move(subtitle);
    invalidateLayout();
}

void TrackListView::setSort(int column, bool ascending) {
    m_sort = column;
    m_ascending = ascending;
    if (m_mode == Mode::Songs) {
        sortTracks();
        rebuildRows();
        invalidate();
    }
}

void TrackListView::sortTracks() {
    auto key = [this](const Track& t) -> std::string {
        switch (m_sort) {
            case 0: return lowerAscii(t.title);
            case 2: return lowerAscii(t.album);
            default: return lowerAscii(t.displayArtist());
        }
    };
    std::stable_sort(m_tracks.begin(), m_tracks.end(), [&](const TrackPtr& a, const TrackPtr& b) {
        bool less = false;
        bool equal = false;
        if (m_sort == 3) {
            less = a->duration < b->duration;
            equal = a->duration == b->duration;
        } else if (m_sort == 4) {
            less = a->modified < b->modified;
            equal = a->modified == b->modified;
        } else {
            const auto ka = key(*a);
            const auto kb = key(*b);
            less = ka < kb;
            equal = ka == kb;
        }
        if (equal) {
            // Artist, then album order, like Music.
            if (a->album != b->album) return lowerAscii(a->album) < lowerAscii(b->album);
            if (a->disc != b->disc) return a->disc < b->disc;
            return a->number < b->number;
        }
        return m_ascending ? less : !less;
    });
}

void TrackListView::rebuildRows() {
    m_rows.clear();
    int disc = -1;
    bool multiDisc = false;
    if (m_mode == Mode::Album) {
        for (const auto& t : m_tracks) multiDisc = multiDisc || (t->disc > 1);
    }
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (multiDisc && m_tracks[i]->disc != disc) {
            disc = m_tracks[i]->disc;
            m_rows.push_back({-1, "Disc " + std::to_string(std::max(1, disc))});
        }
        m_rows.push_back({static_cast<int>(i), {}});
    }
}

float TrackListView::top() const {
    return headingHeight(m_heading, m_subheading) + (m_mode == Mode::Songs ? kHeaderRow : 0);
}

TrackListView::Columns TrackListView::columns() const {
    const Rect c = contentRect();
    Columns col{};
    col.number = c.x + kPad;
    col.right = c.right() - kPad;
    col.time = col.right - 52;
    col.heart = col.time - 30;
    if (m_mode == Mode::Album) {
        col.title = col.number + 34;
        col.artist = col.heart;  // shown under the title when it differs
        col.album = col.heart;
    } else {
        col.title = col.number + 30;
        const float span = col.heart - col.title;
        col.artist = col.title + span * 0.42f;
        col.album = col.title + span * 0.71f;
    }
    return col;
}

Size TrackListView::measureContent(Size available) {
    const float w = std::isfinite(available.width) ? available.width : 800.0f;
    return {w, top() + static_cast<float>(m_rows.size()) * kRowHeight + 24};
}

Rect TrackListView::rowRect(int row) const {
    const Rect c = contentRect();
    return {c.x + kPad - 10, c.y + top() + static_cast<float>(row) * kRowHeight, c.width - kPad * 2 + 20, kRowHeight};
}

int TrackListView::rowAt(Point p) const {
    const Rect c = contentRect();
    const float y = p.y - c.y - top();
    if (y < 0 || p.x < c.x + kPad - 10 || p.x > c.right() - kPad + 10) return -1;
    const int row = static_cast<int>(y / kRowHeight);
    return row < static_cast<int>(m_rows.size()) ? row : -1;
}

void TrackListView::draw(Painter& p) {
    const Theme& t = theme();
    const auto& col = t.colors;
    const Rect c = contentRect();
    drawHeading(p, c, m_heading, m_subheading);
    const Columns cl = columns();
    const Font body = t.font(TextStyle::Body);
    const Font small = t.font(TextStyle::Subheadline);
    const Font headerFont = t.font(TextStyle::Subheadline).withWeight(FontWeight::Semibold);

    if (m_mode == Mode::Songs) {
        const float y = c.y + headingHeight(m_heading, m_subheading);
        const char* titles[] = {"Title", "Artist", "Album", "Time"};
        const float xs[] = {cl.title, cl.artist, cl.album, cl.time};
        for (int i = 0; i < 4; ++i) {
            const bool active = i == m_sort;
            std::string label = titles[i];
            const Rect r{xs[i], y, (i == 3 ? cl.right : (i < 2 ? xs[i + 1] : cl.heart)) - xs[i] - 8, kHeaderRow - 4};
            p.drawText(label, r, headerFont, active ? col.label : col.secondaryLabel, i == 3 ? TextAlign::Trailing : TextAlign::Leading);
            if (active) {
                const float w = p.measureText(label, headerFont);
                const float ax = i == 3 ? r.x - 12 + r.width - w : r.x + w + 4;
                p.drawIcon(m_ascending ? Icon::ChevronUp : Icon::ChevronDown, {ax, r.center().y - 4, 8, 8}, col.secondaryLabel, 1.2f);
            }
        }
        p.drawLine({c.x + kPad - 10, y + kHeaderRow - 1}, {c.right() - kPad + 10, y + kHeaderRow - 1}, col.separator, 1);
    }

    const Rect clip = p.clipBounds();
    const int first = std::max(0, static_cast<int>((clip.y - c.y - top()) / kRowHeight) - 1);
    const int last = std::min(static_cast<int>(m_rows.size()), static_cast<int>((clip.bottom() - c.y - top()) / kRowHeight) + 2);
    for (int r = first; r < last; ++r) {
        const Row& row = m_rows[static_cast<size_t>(r)];
        const Rect rr = rowRect(r);
        if (row.track < 0) {
            p.drawText(row.label, {cl.number, rr.y + 8, rr.width, rr.height - 8}, t.font(TextStyle::Headline), col.label);
            continue;
        }
        const Track& tr = *m_tracks[static_cast<size_t>(row.track)];
        const bool selected = r == m_selected;
        const bool current = m_current && tr.path == m_current->path;
        const bool emphasized = selected && isFocused();
        if (selected) p.fillRoundedRect(rr, 6, emphasized ? col.selection : col.unemphasizedSelection);
        else if (r == m_hover) p.fillRoundedRect(rr, 6, col.hover);
        else if (m_mode == Mode::Songs && r % 2 == 1) p.fillRoundedRect(rr, 6, col.fill.withAlpha(col.fill.a * 0.35f));

        const Color text = emphasized ? col.onAccent : current ? col.accent : col.label;
        const Color secondary = emphasized ? col.onAccent.withAlpha(0.8f) : col.secondaryLabel;
        // Number column: speaker for the current track, play glyph on hover, else the track number.
        const Rect num{cl.number, rr.y, 24, rr.height};
        if (current) {
            const float s = 14;
            p.drawIcon(m_playing ? Icon::Speaker : Icon::Pause, {num.x + 2, num.center().y - s * 0.5f, s, s}, emphasized ? col.onAccent : col.accent, 1.2f);
        } else if (r == m_hover && !selected) {
            p.drawIcon(Icon::Play, {num.x + 3, num.center().y - 6, 12, 12}, col.label, 1.3f);
        } else if (m_mode == Mode::Album && tr.number > 0) {
            p.drawText(std::to_string(tr.number), num, small, secondary, TextAlign::Trailing);
        }

        const float titleRight = (m_mode == Mode::Album ? cl.heart : cl.artist) - 12;
        if (m_mode == Mode::Album && !tr.artist.empty() && tr.artist != m_albumArtist) {
            p.drawText(fit(p, tr.title, body, titleRight - cl.title), {cl.title, rr.y + 2, titleRight - cl.title, 16}, body, text);
            p.drawText(fit(p, tr.artist, t.font(TextStyle::Caption), titleRight - cl.title), {cl.title, rr.y + 18, titleRight - cl.title, 13},
                       t.font(TextStyle::Caption), secondary);
        } else {
            p.drawText(fit(p, tr.title, body, titleRight - cl.title), {cl.title, rr.y, titleRight - cl.title, rr.height}, body, text);
        }
        if (m_mode == Mode::Songs) {
            p.drawText(fit(p, tr.displayArtist(), body, cl.album - cl.artist - 12), {cl.artist, rr.y, cl.album - cl.artist - 12, rr.height}, body,
                       secondary);
            p.drawText(fit(p, tr.album, body, cl.heart - cl.album - 8), {cl.album, rr.y, cl.heart - cl.album - 8, rr.height}, body, secondary);
        }
        const bool fav = isFavorite && isFavorite(tr);
        if (fav || r == m_hover) {
            const float s = 14;
            const Color heart = fav ? (emphasized ? col.onAccent : col.pink) : (m_hoverHeart ? col.label : col.tertiaryLabel);
            p.drawIcon(fav ? Icon::HeartFill : Icon::Heart, {cl.heart + 4, rr.center().y - s * 0.5f, s, s}, heart, 1.2f);
        }
        p.drawText(formatTime(tr.duration), {cl.time, rr.y, cl.right - cl.time, rr.height}, small, secondary, TextAlign::Trailing);
    }
    if (isFocusVisible() && m_selected >= 0) p.drawFocusRing(rowRect(m_selected), 6);
}

bool TrackListView::onPointerDown(const PointerEvent& e) {
    const Rect c = contentRect();
    if (m_mode == Mode::Songs) {
        const float hy = c.y + headingHeight(m_heading, m_subheading);
        if (e.position.y >= hy && e.position.y < hy + kHeaderRow && e.button == MouseButton::Left) {
            const Columns cl = columns();
            int column = e.position.x >= cl.time ? 3 : e.position.x >= cl.album ? 2 : e.position.x >= cl.artist ? 1 : 0;
            setSort(column, column == m_sort ? !m_ascending : true);
            return true;
        }
    }
    const int r = rowAt(e.position);
    if (r < 0 || m_rows[static_cast<size_t>(r)].track < 0) return false;
    if (auto* h = host()) h->setFocus(this, false);
    m_selected = r;
    invalidate();
    const TrackPtr& t = m_tracks[static_cast<size_t>(m_rows[static_cast<size_t>(r)].track)];
    if (e.button == MouseButton::Right) {
        if (onContextMenu) onContextMenu(t, e.position);
        return true;
    }
    if (e.button != MouseButton::Left) return true;
    const Columns cl = columns();
    if (e.position.x >= cl.heart && e.position.x < cl.time) {
        if (onToggleFavorite) onToggleFavorite(t);
        invalidate();
        return true;
    }
    // Double-click plays; a click on the number column (the hover play glyph) plays at once.
    if (e.clickCount >= 2 || e.position.x < cl.title) play(r);
    return true;
}

void TrackListView::onPointerMove(const PointerEvent& e) {
    const int r = rowAt(e.position);
    const int h = r >= 0 && m_rows[static_cast<size_t>(r)].track >= 0 ? r : -1;
    const Columns cl = columns();
    const bool heart = h >= 0 && e.position.x >= cl.heart && e.position.x < cl.time;
    if (h != m_hover || heart != m_hoverHeart) {
        m_hover = h;
        m_hoverHeart = heart;
        invalidate();
    }
}

void TrackListView::onPointerLeave() {
    m_hover = -1;
    invalidate();
}

void TrackListView::play(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size()) || m_rows[static_cast<size_t>(row)].track < 0 || !onPlay) return;
    onPlay(m_tracks, static_cast<size_t>(m_rows[static_cast<size_t>(row)].track));
}

void TrackListView::select(int row) {
    if (m_rows.empty()) return;
    row = std::clamp(row, 0, static_cast<int>(m_rows.size()) - 1);
    // Skip disc headings in the direction of travel.
    const int dir = row >= m_selected ? 1 : -1;
    while (row >= 0 && row < static_cast<int>(m_rows.size()) && m_rows[static_cast<size_t>(row)].track < 0) row += dir;
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    m_selected = row;
    invalidate();
    if (!m_scroller) return;
    const float inset = host() ? host()->titleBarHeight() : 0.0f;
    const float y = top() + static_cast<float>(row) * kRowHeight;
    float offset = m_scroller->offset().y;
    const float visible = m_scroller->frame().height - inset;
    if (y - kRowHeight < offset) offset = y - kRowHeight;
    if (y + kRowHeight * 2 > offset + visible) offset = y + kRowHeight * 2 - visible;
    if (offset != m_scroller->offset().y) m_scroller->scrollTo({0, std::max(0.0f, offset)}, true);
}

bool TrackListView::onKeyDown(const KeyEvent& e) {
    if (m_rows.empty()) return false;
    switch (e.key) {
        case Key::Up: select(m_selected < 0 ? 0 : m_selected - 1); return true;
        case Key::Down: select(m_selected < 0 ? 0 : m_selected + 1); return true;
        case Key::Home: select(0); return true;
        case Key::End: select(static_cast<int>(m_rows.size()) - 1); return true;
        case Key::PageUp: select(m_selected - 12); return true;
        case Key::PageDown: select(m_selected + 12); return true;
        case Key::Enter: play(m_selected); return true;
        default: return false;
    }
}

// Artists ---------------------------------------------------------------------------------------------------

void ArtistListView::setArtists(std::vector<std::string> names, std::vector<std::string> details) {
    m_names = std::move(names);
    m_details = std::move(details);
    m_selected = m_names.empty() ? -1 : std::clamp(m_selected, 0, static_cast<int>(m_names.size()) - 1);
    invalidateLayout();
}

void ArtistListView::setSelected(int index) {
    m_selected = index;
    invalidate();
}

Size ArtistListView::measureContent(Size available) {
    const float w = std::isfinite(available.width) ? available.width : 250.0f;
    return {w, 12 + static_cast<float>(m_names.size()) * 48 + 12};
}

Rect ArtistListView::rowRect(int i) const {
    const Rect c = contentRect();
    return {c.x + 10, c.y + 12 + static_cast<float>(i) * 48, c.width - 20, 46};
}

void ArtistListView::draw(Painter& p) {
    const Theme& t = theme();
    const auto& col = t.colors;
    const Rect clip = p.clipBounds();
    for (int i = 0; i < static_cast<int>(m_names.size()); ++i) {
        const Rect r = rowRect(i);
        if (r.bottom() < clip.y || r.y > clip.bottom()) continue;
        const bool selected = i == m_selected;
        if (selected) p.fillRoundedRect(r, 8, isFocused() ? col.selection : col.unemphasizedSelection);
        else if (i == m_hover) p.fillRoundedRect(r, 8, col.hover);
        const Point ctr{r.x + 24, r.center().y};
        const Color tint = tintFor(m_names[static_cast<size_t>(i)]);
        p.fillVerticalGradient({ctr.x - 17, ctr.y - 17, 34, 34}, 17, tint.lighter(0.15f), tint.lighter(-0.1f));
        std::string initial;
        const std::string& n = m_names[static_cast<size_t>(i)];
        if (!n.empty()) {
            size_t len = 1;
            while (len < n.size() && (static_cast<unsigned char>(n[len]) & 0xC0) == 0x80) ++len;
            initial = n.substr(0, len);
            if (initial.size() == 1) initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        }
        p.drawText(initial, {ctr.x - 17, ctr.y - 17, 34, 34}, t.font(TextStyle::Headline), Color::white(), TextAlign::Center);
        const bool on = selected && isFocused();
        const float tx = r.x + 50;
        p.drawText(fit(p, n, t.font(TextStyle::Body), r.right() - tx - 8), {tx, r.y + 6, r.right() - tx - 8, 18}, t.font(TextStyle::Body),
                   on ? col.onAccent : col.label);
        p.drawText(m_details[static_cast<size_t>(i)], {tx, r.y + 24, r.right() - tx - 8, 15}, t.font(TextStyle::Caption),
                   on ? col.onAccent.withAlpha(0.8f) : col.secondaryLabel);
    }
}

bool ArtistListView::onPointerDown(const PointerEvent& e) {
    for (int i = 0; i < static_cast<int>(m_names.size()); ++i) {
        if (rowRect(i).contains(e.position)) {
            if (auto* h = host()) h->setFocus(this, false);
            m_selected = i;
            invalidate();
            if (onSelect) onSelect(i);
            return true;
        }
    }
    return false;
}

void ArtistListView::onPointerMove(const PointerEvent& e) {
    int h = -1;
    for (int i = 0; i < static_cast<int>(m_names.size()); ++i) {
        if (rowRect(i).contains(e.position)) h = i;
    }
    if (h != m_hover) {
        m_hover = h;
        invalidate();
    }
}

void ArtistListView::onPointerLeave() {
    m_hover = -1;
    invalidate();
}

bool ArtistListView::onKeyDown(const KeyEvent& e) {
    if (m_names.empty()) return false;
    int next = m_selected;
    if (e.key == Key::Up) next = std::max(0, m_selected - 1);
    else if (e.key == Key::Down) next = std::min(static_cast<int>(m_names.size()) - 1, m_selected + 1);
    else return false;
    if (next != m_selected) {
        m_selected = next;
        invalidate();
        if (onSelect) onSelect(next);
    }
    return true;
}

// Now playing ------------------------------------------------------------------------------------------------

void NowPlayingView::setTrack(const Track* track, const Album* album) {
    m_track = track;
    m_album = album;
    invalidate();
}

void NowPlayingView::setTime(double position, double duration) {
    if (std::abs(position - m_position) < 0.05 && duration == m_duration) return;
    m_position = position;
    m_duration = duration;
    invalidate();
}

Size NowPlayingView::measureContent(Size available) {
    const float w = std::isfinite(available.width) ? available.width - reserve : 420.0f;
    return {std::clamp(w, 200.0f, 460.0f), 40};
}

Rect NowPlayingView::artRect() const {
    const Rect r = frame();
    return {r.x + 2, r.y + 2, r.height - 4, r.height - 4};
}

Rect NowPlayingView::barRect() const {
    const Rect r = frame();
    const Rect a = artRect();
    return {a.right() + 12, r.bottom() - 7, r.right() - a.right() - 24, 3};
}

double NowPlayingView::timeAt(float x) const {
    const Rect b = barRect();
    return std::clamp((x - b.x) / std::max(1.0f, b.width), 0.0f, 1.0f) * m_duration;
}

void NowPlayingView::draw(Painter& p) {
    const Theme& t = theme();
    const auto& col = t.colors;
    const Rect r = frame();
    p.fillRoundedRect(r, 7, col.fill.withAlpha(col.fill.a * 0.8f), col.separator.withAlpha(0.6f), 0.5f);
    if (!m_track) {
        const float s = 18;
        p.drawIcon(Icon::MusicNote, {r.center().x - s * 0.5f, r.center().y - s * 0.5f, s, s}, col.tertiaryLabel, 1);
        return;
    }
    drawArtwork(p, artRect(), m_album, 5);
    const Rect bar = barRect();
    const Font titleFont = t.font(TextStyle::Subheadline).withWeight(FontWeight::Semibold);
    const Font subFont = t.font(TextStyle::Footnote);
    const float hover = sample(m_hoverAnim);
    // Elapsed / remaining appear at the ends while hovered, squeezing the text.
    const float side = 44 * hover;
    const Rect text{bar.x + side, r.y + 3, bar.width - side * 2, 16};
    p.drawText(fit(p, m_track->title, titleFont, text.width), text, titleFont, col.label, TextAlign::Center);
    std::string sub = m_track->displayArtist();
    if (!m_track->album.empty()) sub += " — " + m_track->album;
    p.drawText(fit(p, sub, subFont, text.width), {text.x, r.y + 18, text.width, 13}, subFont, col.secondaryLabel, TextAlign::Center);
    const double shown = m_dragging ? m_dragTime : m_position;
    const float frac = m_duration > 0 ? static_cast<float>(std::clamp(shown / m_duration, 0.0, 1.0)) : 0;
    p.fillRoundedRect(bar, 1.5f, col.separator);
    p.fillRoundedRect({bar.x, bar.y, std::max(3.0f, bar.width * frac), bar.height}, 1.5f, col.secondaryLabel);
    if (hover > 0.01f) {
        const Font f = t.font(TextStyle::Caption);
        const Color c = col.secondaryLabel.withAlpha(hover);
        p.drawText(formatTime(shown), {bar.x, r.y + 6, 40, 22}, f, c);
        p.drawText("-" + formatTime(std::max(0.0, m_duration - shown)), {bar.right() - 40, r.y + 6, 40, 22}, f, c, TextAlign::Trailing);
        p.fillCircle({bar.x + bar.width * frac, bar.center().y}, 3.5f * hover, col.label);
    }
}

bool NowPlayingView::onPointerDown(const PointerEvent& e) {
    if (!m_track || e.button != MouseButton::Left) return false;
    if (artRect().contains(e.position)) {
        if (onArtworkClick) onArtworkClick();
        return true;
    }
    const Rect b = barRect();
    if (e.position.y >= b.y - 9 && m_duration > 0) {
        m_dragging = true;
        m_dragTime = timeAt(e.position.x);
        invalidate();
    }
    return true;
}

void NowPlayingView::onPointerMove(const PointerEvent& e) {
    if (!m_dragging) return;
    m_dragTime = timeAt(e.position.x);
    invalidate();
}

void NowPlayingView::onPointerUp(const PointerEvent& e) {
    if (!m_dragging) return;
    m_dragging = false;
    m_dragTime = timeAt(e.position.x);
    m_position = m_dragTime;
    if (onSeek) onSeek(m_dragTime);
    invalidate();
}

void NowPlayingView::onHoverChanged(bool hovered) {
    m_hoverAnim.animateTo(hovered ? 1.0f : 0.0f, now(), Transition::fast());
    invalidate();
}

// Pages -------------------------------------------------------------------------------------------------------

void PageView::arrangeContent(const Rect& c) {
    const float bar = host() ? host()->titleBarHeight() : 0.0f;
    Rect main = c;
    if (leading && leading->isVisible()) {
        if (auto* s = dynamic_cast<ScrollViewView*>(leading.get())) s->setContentInsetTop(bar);
        const Rect l{c.x, c.y, leadingWidth, c.height};
        leading->measure(l.size());
        leading->arrange(l);
        main = {c.x + leadingWidth + 1, c.y, c.width - leadingWidth - 1, c.height};
    }
    scroll->setContentInsetTop(bar);
    scroll->measure(main.size());
    scroll->arrange(main);
    const Rect below{main.x, main.y + bar, main.width, main.height - bar};
    empty->measure(below.size());
    empty->arrange(below);
}

void PageView::draw(Painter& p) {
    Widget::draw(p);
    if (leading && leading->isVisible()) {
        const Rect c = contentRect();
        const float bar = host() ? host()->titleBarHeight() : 0.0f;
        p.drawLine({c.x + leadingWidth + 0.5f, c.y + bar}, {c.x + leadingWidth + 0.5f, c.bottom()}, theme().colors.separator, 1);
    }
}

}  // namespace music
