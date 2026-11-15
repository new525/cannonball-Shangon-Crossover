/***************************************************************************
    ROM files in zips. See romzip.hpp.

    Part of the OutRun / Super Hang-On / Hang-On crossover built on Cannonball.
***************************************************************************/
#include "crossover/romzip.hpp"
#include "crossover/miniz/miniz.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <tuple>

namespace fs = std::filesystem;

static std::string lower(std::string s)
{
    for (char& c : s) c = (char) std::tolower((unsigned char) c);
    return s;
}

static bool has_zip_extension(const fs::path& p)
{
    return lower(p.extension().string()) == ".zip";
}

// A zip opened for reading. miniz reads it through an ifstream (built from the fs::path, as the
// loose files are), so its file names are handled the same way as the loose files'.
namespace
{
struct ZipReader
{
    std::ifstream f;
    mz_zip_archive zip;
    bool open = false;

    ZipReader() { mz_zip_zero_struct(&zip); }
    ~ZipReader() { if (open) mz_zip_reader_end(&zip); }
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    static size_t read(void* opaque, mz_uint64 ofs, void* buf, size_t n)
    {
        std::ifstream& f = static_cast<ZipReader*>(opaque)->f;
        f.clear();
        f.seekg((std::streamoff) ofs);
        if (!f) return 0;
        f.read(static_cast<char*>(buf), (std::streamsize) n);
        return (size_t) f.gcount();
    }

    bool init(const fs::path& p)
    {
        std::error_code ec;
        uintmax_t size = fs::file_size(p, ec);
        if (ec) return false;
        f.open(p, std::ios::binary);
        if (!f) return false;
        zip.m_pRead = read;
        zip.m_pIO_opaque = this;
        open = mz_zip_reader_init(&zip, size, 0) != 0;
        return open;
    }
};
}

std::string RomFile::where() const
{
    return in_zip() ? path.string() + " (" + entry + ")" : path.string();
}

bool read_loose_file(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    // the file's size, then one read; a file whose size cannot be asked for is read to its end
    std::error_code ec;
    const uintmax_t size = fs::file_size(p, ec);
    if (ec)
    {
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    out.resize((size_t) size);
    f.read(reinterpret_cast<char*>(out.data()), (std::streamsize) size);
    out.resize((size_t) f.gcount());
    return true;
}

// Eight table lookups for eight bytes ("slicing by 8"); the same sum as the bytewise table
uint32_t rom_crc32(const uint8_t* p, size_t n)
{
    struct Tables
    {
        uint32_t t[8][256];
        Tables()
        {
            for (uint32_t i = 0; i < 256; i++)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[0][i] = c;
            }
            for (uint32_t i = 0; i < 256; i++)
                for (int k = 1; k < 8; k++) t[k][i] = (t[k - 1][i] >> 8) ^ t[0][t[k - 1][i] & 0xFF];
        }
    };
    static const Tables tab;
    const uint32_t (*t)[256] = tab.t;
    uint32_t c = 0xFFFFFFFFu;
    for (; n >= 8; p += 8, n -= 8)
    {
        const uint32_t a = c ^ ((uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24);
        const uint32_t b = (uint32_t) p[4] | (uint32_t) p[5] << 8 | (uint32_t) p[6] << 16 | (uint32_t) p[7] << 24;
        c = t[7][a & 0xFF] ^ t[6][(a >> 8) & 0xFF] ^ t[5][(a >> 16) & 0xFF] ^ t[4][a >> 24] ^
            t[3][b & 0xFF] ^ t[2][(b >> 8) & 0xFF] ^ t[1][(b >> 16) & 0xFF] ^ t[0][b >> 24];
    }
    for (; n; p++, n--) c = t[0][(c ^ *p) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool loose_file_crc32(const fs::path& p, uint32_t& crc)
{
    typedef std::tuple<fs::path::string_type, uintmax_t, fs::file_time_type> Key;
    static std::map<Key, uint32_t> known;
    std::error_code es, et;
    const uintmax_t size = fs::file_size(p, es);
    const fs::file_time_type time = fs::last_write_time(p, et);
    const bool keyed = !es && !et;
    const Key key(p.native(), size, time);
    if (keyed)
    {
        auto it = known.find(key);
        if (it != known.end()) { crc = it->second; return true; }
    }
    std::vector<uint8_t> d;
    if (!read_loose_file(p, d)) return false;
    crc = rom_crc32(d.data(), d.size());
    if (keyed && d.size() == size) known[key] = crc;
    return true;
}

bool read_rom_file(const RomFile& rf, std::vector<uint8_t>& out)
{
    if (!rf.in_zip())
        return read_loose_file(rf.path, out);
    ZipReader z;
    if (!z.init(rf.path)) return false;
    // The entry must still be the one indexed
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z.zip, rf.index, &st) || st.m_uncomp_size != rf.size ||
        st.m_crc32 != rf.crc || rf.entry != st.m_filename)
        return false;
    out.assign(rf.size, 0);
    // miniz compares the decompressed data's CRC32 with the entry's
    if (!mz_zip_reader_extract_to_mem(&z.zip, rf.index, out.data(), out.size(), 0))
    {
        out.clear();
        return false;
    }
    return true;
}

bool is_zip_file(const std::string& path)
{
    std::error_code ec;
    fs::path p(path);
    return fs::is_regular_file(p, ec) && has_zip_extension(p);
}

void RomZips::index(const std::string& path)
{
    zips.clear();
    entries.clear();

    if (is_zip_file(path))
        zips.push_back(fs::path(path));
    else
    {
        std::error_code ec;
        fs::directory_iterator it(fs::path(path), ec), end;
        for (; !ec && it != end; it.increment(ec))
        {
            std::error_code fe;
            if (it->is_regular_file(fe) && has_zip_extension(it->path()))
                zips.push_back(it->path());
        }
        std::sort(zips.begin(), zips.end(),
                  [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
    }

    std::vector<fs::path> ok;
    for (const fs::path& zp : zips)
    {
        ZipReader z;
        if (!z.init(zp))
        {
            std::cerr << "warning: cannot read zip " << zp.string() << std::endl;
            continue;
        }
        ok.push_back(zp);
        mz_uint n = mz_zip_reader_get_num_files(&z.zip);
        for (mz_uint i = 0; i < n; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&z.zip, i, &st)) continue;
            if (st.m_is_directory || !st.m_is_supported || st.m_is_encrypted) continue;
            if (st.m_uncomp_size == 0 || st.m_uncomp_size > 0xffffffffu) continue;
            RomFile rf(zp);
            rf.entry = st.m_filename;
            rf.index = i;
            rf.size  = (uint32_t) st.m_uncomp_size;
            rf.crc   = (uint32_t) st.m_crc32;
            entries.push_back(rf);
        }
    }
    zips = ok;
}

const RomFile* RomZips::find(uint32_t crc, uint32_t size) const
{
    for (const RomFile& e : entries)
        if (e.crc == crc && (size == 0 || e.size == size)) return &e;
    return nullptr;
}

const RomFile* RomZips::find(const std::string& name, uint32_t crc) const
{
    const std::string want = lower(name);
    const RomFile* first = nullptr;
    for (const RomFile& e : entries)
    {
        size_t slash = e.entry.find_last_of("/\\");
        std::string base = slash == std::string::npos ? e.entry : e.entry.substr(slash + 1);
        if (lower(base) != want) continue;
        if (e.crc == crc) return &e;
        if (!first) first = &e;
    }
    return first;
}

std::string RomZips::zip_list() const
{
    std::string s;
    for (const fs::path& z : zips)
    {
        if (!s.empty()) s += ", ";
        s += z.filename().string();
    }
    return s;
}
