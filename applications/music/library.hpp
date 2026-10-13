// The music library: finding audio files, reading their tags with FFmpeg, grouping them into albums and
// artists, and finding cover art (embedded pictures first, then cover.jpg & co. next to the files). Tags are
// cached on disk, so a second start shows the library at once and only changed files are read again.
#pragma once

#include <mayonnaise/widgets/image.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Track {
    std::string path;
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtist;
    std::string genre;
    int year = 0;
    int number = 0;  ///< track number on its disc
    int disc = 0;
    double duration = 0;
    std::uintmax_t size = 0;
    std::int64_t modified = 0;  ///< seconds since the epoch
    bool embeddedArt = false;

    /// Album artist, else artist, else "Unknown Artist".
    [[nodiscard]] const std::string& displayArtist() const;
};
using TrackPtr = std::shared_ptr<Track>;

struct Album {
    std::string key;  ///< album artist + album title, lower case
    std::string title;
    std::string artist;
    std::string genre;
    int year = 0;
    std::int64_t added = 0;  ///< newest file
    std::vector<TrackPtr> tracks;  ///< in disc / track order
    std::shared_ptr<mayo::ImageData> cover;
    bool coverRequested = false;
    /// File the cover came from (an image file, or the extracted embedded picture), for MPRIS.
    std::string coverFile;

    [[nodiscard]] double duration() const;
};
using AlbumPtr = std::shared_ptr<Album>;

struct Artist {
    std::string name;
    std::vector<AlbumPtr> albums;  ///< newest first
    [[nodiscard]] size_t trackCount() const;
};

[[nodiscard]] bool isAudioFile(const std::string& path);
/// $XDG_MUSIC_DIR (from user-dirs.dirs), or ~/Music.
[[nodiscard]] std::string musicDirectory();

/// Tags of one file (no cover). Any thread.
TrackPtr readTrack(const std::string& path);

/// Scans `folders` (cached tags are reused for unchanged files) and saves the cache. Worker thread only;
/// `progress` gets the number of files found so far.
std::vector<TrackPtr> scanLibrary(const std::vector<std::string>& folders, const std::function<void(size_t)>& progress = {});
/// The cached library from the last scan, without touching the folders (instant start).
std::vector<TrackPtr> loadCachedLibrary();

/// Groups tracks into albums (sorted by artist, then title) and artists (by name).
struct Collection {
    std::vector<AlbumPtr> albums;
    std::vector<Artist> artists;
};
Collection groupLibrary(const std::vector<TrackPtr>& tracks);

/// Cover art for an album, at most `maxSize` pixels: embedded in its first track that has one, else an
/// image in the album's folder. Also writes the picture to the cache when it was embedded and sets
/// `file` to an image path usable by other programs. Worker thread only.
std::shared_ptr<mayo::ImageData> loadCover(const Album& album, int maxSize, std::string& file);

/// Case-insensitive (ASCII) "contains".
[[nodiscard]] bool matches(const std::string& text, const std::string& lowerQuery);
[[nodiscard]] std::string lowerAscii(std::string s);
/// "3:07" / "1:02:03".
[[nodiscard]] std::string formatTime(double seconds);
