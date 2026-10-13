#include "library.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr const char* kCacheHeader = "mayo-music-library 1";

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h ? h : "/";
}

std::string cacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return ((x && *x) ? std::string(x) : homeDir() + "/.cache") + "/mayonnaise/music";
}

/// Tag values may hold anything; the cache is one line per track with tab-separated fields.
std::string clean(std::string s) {
    for (auto& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

std::string tag(AVFormatContext* f, int stream, std::initializer_list<const char*> keys) {
    for (const char* k : keys) {
        if (const AVDictionaryEntry* e = av_dict_get(f->metadata, k, nullptr, 0); e && e->value && *e->value) return clean(e->value);
        if (stream >= 0) {
            if (const AVDictionaryEntry* e = av_dict_get(f->streams[stream]->metadata, k, nullptr, 0); e && e->value && *e->value)
                return clean(e->value);
        }
    }
    return {};
}

bool statFile(const std::string& path, std::uintmax_t& size, std::int64_t& modified) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return false;
    size = static_cast<std::uintmax_t>(st.st_size);
    modified = static_cast<std::int64_t>(st.st_mtime);
    return true;
}

void collect(const fs::path& dir, int depth, std::vector<std::string>& out, std::set<std::string>& seen,
             const std::function<void(size_t)>& progress) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string name = p.filename().string();
        if (name.empty() || name[0] == '.') continue;
        if (it->is_directory(ec)) {
            if (depth > 0) collect(p, depth - 1, out, seen, progress);
        } else if (isAudioFile(name)) {
            std::string canonical = fs::weakly_canonical(p, ec).string();
            if (canonical.empty()) canonical = p.string();
            if (seen.insert(canonical).second) {
                out.push_back(p.string());
                if (progress && out.size() % 50 == 0) progress(out.size());
            }
        }
    }
}

std::string cachePath() { return cacheDir() + "/library.tsv"; }

void saveCache(const std::vector<TrackPtr>& tracks) {
    std::error_code ec;
    fs::create_directories(cacheDir(), ec);
    const std::string tmp = cachePath() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << kCacheHeader << "\n";
        for (const auto& t : tracks) {
            out << t->path << '\t' << t->size << '\t' << t->modified << '\t' << t->duration << '\t' << t->title << '\t' << t->artist
                << '\t' << t->album << '\t' << t->albumArtist << '\t' << t->genre << '\t' << t->year << '\t' << t->number << '\t'
                << t->disc << '\t' << (t->embeddedArt ? 1 : 0) << '\n';
        }
    }
    fs::rename(tmp, cachePath(), ec);
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == '\t') {
            f.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return f;
}

/// Picture files commonly put next to an album, best names first.
std::string folderImage(const fs::path& dir) {
    std::error_code ec;
    std::vector<fs::path> images;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string ext = lowerAscii(e.path().extension().string());
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") images.push_back(e.path());
    }
    if (images.empty()) return {};
    std::sort(images.begin(), images.end());
    for (const char* name : {"cover", "folder", "front", "album", "albumart", "albumartlarge", "albumartsmall"}) {
        for (const auto& p : images) {
            if (lowerAscii(p.stem().string()) == name) return p.string();
        }
    }
    return images.front().string();
}

}  // namespace

const std::string& Track::displayArtist() const {
    static const std::string unknown = "Unknown Artist";
    if (!albumArtist.empty()) return albumArtist;
    if (!artist.empty()) return artist;
    return unknown;
}

double Album::duration() const {
    double d = 0;
    for (const auto& t : tracks) d += t->duration;
    return d;
}

size_t Artist::trackCount() const {
    size_t n = 0;
    for (const auto& a : albums) n += a->tracks.size();
    return n;
}

bool isAudioFile(const std::string& path) {
    static const std::set<std::string> kExt = {".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".m4b", ".aac", ".wav", ".wma",
                                               ".alac", ".ape", ".wv", ".aif", ".aiff", ".mka", ".mpc", ".dsf", ".tta"};
    return kExt.contains(lowerAscii(fs::path(path).extension().string()));
}

std::string musicDirectory() {
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    const std::string config = (cfg && *cfg) ? cfg : homeDir() + "/.config";
    std::ifstream in(config + "/user-dirs.dirs");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("XDG_MUSIC_DIR=", 0) != 0) continue;
        std::string v = line.substr(14);
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        if (v.rfind("$HOME", 0) == 0) v = homeDir() + v.substr(5);
        return v;
    }
    return homeDir() + "/Music";
}

TrackPtr readTrack(const std::string& path) {
    auto t = std::make_shared<Track>();
    t->path = path;
    statFile(path, t->size, t->modified);
    AVFormatContext* f = nullptr;
    if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) < 0) return nullptr;
    if (f->duration <= 0 || f->nb_streams == 0) avformat_find_stream_info(f, nullptr);
    int audio = -1;
    for (unsigned i = 0; i < f->nb_streams; ++i) {
        const AVStream* s = f->streams[i];
        if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) t->embeddedArt = true;
        else if (audio < 0 && s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) audio = static_cast<int>(i);
    }
    if (audio < 0) {
        avformat_close_input(&f);
        return nullptr;  // a video file or something else that happened to have an audio extension
    }
    t->title = tag(f, audio, {"title"});
    t->artist = tag(f, audio, {"artist", "performer"});
    t->album = tag(f, audio, {"album"});
    t->albumArtist = tag(f, audio, {"album_artist", "albumartist", "album artist"});
    t->genre = tag(f, audio, {"genre"});
    t->year = std::atoi(tag(f, audio, {"date", "year", "originaldate"}).substr(0, 4).c_str());
    t->number = std::atoi(tag(f, audio, {"track", "tracknumber"}).c_str());
    t->disc = std::atoi(tag(f, audio, {"disc", "discnumber"}).c_str());
    t->duration = f->duration > 0 ? static_cast<double>(f->duration) / AV_TIME_BASE : 0;
    avformat_close_input(&f);
    if (t->title.empty()) t->title = fs::path(path).stem().string();
    return t;
}

std::vector<TrackPtr> loadCachedLibrary() {
    std::vector<TrackPtr> out;
    std::ifstream in(cachePath());
    std::string line;
    if (!std::getline(in, line) || line != kCacheHeader) return out;
    while (std::getline(in, line)) {
        auto f = fields(line);
        if (f.size() < 13) continue;
        auto t = std::make_shared<Track>();
        t->path = f[0];
        t->size = std::strtoull(f[1].c_str(), nullptr, 10);
        t->modified = std::strtoll(f[2].c_str(), nullptr, 10);
        t->duration = std::atof(f[3].c_str());
        t->title = f[4];
        t->artist = f[5];
        t->album = f[6];
        t->albumArtist = f[7];
        t->genre = f[8];
        t->year = std::atoi(f[9].c_str());
        t->number = std::atoi(f[10].c_str());
        t->disc = std::atoi(f[11].c_str());
        t->embeddedArt = f[12] == "1";
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<TrackPtr> scanLibrary(const std::vector<std::string>& folders, const std::function<void(size_t)>& progress) {
    std::map<std::string, TrackPtr> cached;
    for (auto& t : loadCachedLibrary()) cached.emplace(t->path, t);
    std::vector<std::string> paths;
    std::set<std::string> seen;
    for (const auto& dir : folders) collect(dir, 8, paths, seen, progress);
    std::vector<TrackPtr> out;
    out.reserve(paths.size());
    for (const auto& p : paths) {
        std::uintmax_t size = 0;
        std::int64_t modified = 0;
        if (!statFile(p, size, modified)) continue;
        auto it = cached.find(p);
        if (it != cached.end() && it->second->size == size && it->second->modified == modified) {
            out.push_back(it->second);
            continue;
        }
        if (auto t = readTrack(p)) out.push_back(std::move(t));
    }
    if (progress) progress(out.size());
    saveCache(out);
    return out;
}

Collection groupLibrary(const std::vector<TrackPtr>& tracks) {
    Collection c;
    std::map<std::string, AlbumPtr> albums;
    for (const auto& t : tracks) {
        // Without an album artist, the folder keeps compilations together.
        const std::string title = t->album.empty() ? "Unknown Album" : t->album;
        const std::string owner = !t->albumArtist.empty() ? lowerAscii(t->albumArtist) : fs::path(t->path).parent_path().string();
        const std::string key = owner + '\x1f' + lowerAscii(title);
        auto& a = albums[key];
        if (!a) {
            a = std::make_shared<Album>();
            a->key = key;
            a->title = title;
        }
        a->tracks.push_back(t);
        a->added = std::max(a->added, t->modified);
        if (a->year == 0) a->year = t->year;
        if (a->genre.empty()) a->genre = t->genre;
    }
    std::map<std::string, Artist> artists;
    for (auto& [_, a] : albums) {
        std::sort(a->tracks.begin(), a->tracks.end(), [](const TrackPtr& x, const TrackPtr& y) {
            if (x->disc != y->disc) return x->disc < y->disc;
            if (x->number != y->number) return x->number < y->number;
            return x->path < y->path;
        });
        std::set<std::string> names;
        for (const auto& t : a->tracks) names.insert(t->displayArtist());
        a->artist = names.size() == 1 ? *names.begin() : "Various Artists";
        c.albums.push_back(a);
        auto& artist = artists[lowerAscii(a->artist)];
        if (artist.name.empty()) artist.name = a->artist;
        artist.albums.push_back(a);
    }
    std::sort(c.albums.begin(), c.albums.end(), [](const AlbumPtr& x, const AlbumPtr& y) {
        const auto ax = lowerAscii(x->artist);
        const auto ay = lowerAscii(y->artist);
        return ax != ay ? ax < ay : lowerAscii(x->title) < lowerAscii(y->title);
    });
    for (auto& [_, artist] : artists) {
        std::sort(artist.albums.begin(), artist.albums.end(), [](const AlbumPtr& x, const AlbumPtr& y) { return x->year > y->year; });
        c.artists.push_back(std::move(artist));
    }
    return c;
}

std::shared_ptr<mayo::ImageData> loadCover(const Album& album, int maxSize, std::string& file) {
    std::error_code ec;
    const std::string dir = cacheDir() + "/covers";
    const std::string base = dir + "/" + std::to_string(std::hash<std::string>{}(album.key));
    for (const char* ext : {".jpg", ".png"}) {
        if (fs::exists(base + ext, ec)) {
            file = base + ext;
            return mayo::ImageData::load(file, maxSize);
        }
    }
    for (const auto& t : album.tracks) {
        if (!t->embeddedArt) continue;
        AVFormatContext* f = nullptr;
        if (avformat_open_input(&f, t->path.c_str(), nullptr, nullptr) < 0) continue;
        std::string bytes;
        for (unsigned i = 0; i < f->nb_streams; ++i) {
            const AVStream* s = f->streams[i];
            if ((s->disposition & AV_DISPOSITION_ATTACHED_PIC) && s->attached_pic.size > 0) {
                bytes.assign(reinterpret_cast<const char*>(s->attached_pic.data), static_cast<size_t>(s->attached_pic.size));
                break;
            }
        }
        avformat_close_input(&f);
        if (bytes.empty()) continue;
        // The picture is already a JPEG or PNG: store it as it is.
        const bool png = bytes.size() > 4 && bytes.compare(0, 4, "\x89PNG") == 0;
        fs::create_directories(dir, ec);
        file = base + (png ? ".png" : ".jpg");
        std::ofstream(file, std::ios::binary) << bytes;
        if (auto img = mayo::ImageData::load(file, maxSize)) return img;
    }
    if (!album.tracks.empty()) {
        const std::string image = folderImage(fs::path(album.tracks.front()->path).parent_path());
        if (!image.empty()) {
            file = image;
            return mayo::ImageData::load(image, maxSize);
        }
    }
    file.clear();
    return nullptr;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matches(const std::string& text, const std::string& lowerQuery) {
    return lowerQuery.empty() || lowerAscii(text).find(lowerQuery) != std::string::npos;
}

std::string formatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    const auto total = static_cast<long>(seconds);
    char buf[32];
    if (total >= 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld", total / 60, total % 60);
    return buf;
}
