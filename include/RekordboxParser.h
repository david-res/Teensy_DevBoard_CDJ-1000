/*
 * RekordboxParser.h — Lazy/on-demand PDB parser for Teensy 4.x
 *
 * MEMORY MODEL:
 *   All persistent and temporary data lives in a single caller-supplied arena
 *   (typically a chunk of EXTMEM). No heap allocation (malloc/extmem_malloc)
 *   anywhere in this file.
 *
 *   Arena layout (set by begin()):
 *     [0]              PlaylistInfo[MAX_PLAYLISTS]   — filled once at begin()
 *     [playlist_bytes] uint16_t track_id_pool[]      — scratch, refilled per load
 *     [pool_bytes]     Track[]                        — filled per getTracksForPlaylist()
 *
 *   The track_id_pool is written first, then immediately consumed to fill
 *   Track[], so they can share the same region if the caller wants to be
 *   tight on space (pool is uint16_t, tracks are Track — no type conflict).
 *   Here we keep them separate for clarity.
 *
 * USAGE:
 *   EXTMEM static uint8_t rbArena[RB_ARENA_BYTES];
 *   rbParser->begin("/PIONEER/rekordbox/export.pdb", rbArena, sizeof(rbArena));
 *   // Playlist list is now populated in arena.
 *
 *   // On playlist click:
 *   uint16_t n = rbParser->getTracksForPlaylist(pl_id);
 *   // Tracks are now in arena, accessible via getTrack(i).
 */

#ifndef REKORDBOX_PARSER_H
#define REKORDBOX_PARSER_H

#include <Arduino.h>
#include <FS.h>

// ============================================================================
// Tunables — adjust to your library size
// ============================================================================
#define RB_MAX_PLAYLISTS      20
#define RB_MAX_PLAYLIST_TRACKS 1000   // max tracks in a single playlist
#define RB_MAX_PLAYLIST_NAME  48
#define RB_MAX_TITLE_LEN      55
#define RB_MAX_PATH_LEN       128
#define RB_PAGE_SIZE          4096

// Convenience macro: minimum arena size in bytes
// Pass this to your EXTMEM buffer declaration.
#define RB_ARENA_BYTES ( \
    /* PlaylistInfo list  */ (RB_MAX_PLAYLISTS * (4 + RB_MAX_PLAYLIST_NAME)) + \
    /* Track ID pool      */ (RB_MAX_PLAYLIST_TRACKS * 2) + \
    /* Track array        */ (RB_MAX_PLAYLIST_TRACKS * sizeof(Track)) + \
    /* 64-byte alignment  */ 64 \
)

// ============================================================================
// Structs
// ============================================================================

struct Track {
    uint16_t id;
    char     title[RB_MAX_TITLE_LEN];
    char     artist[RB_MAX_TITLE_LEN];
    char     album[RB_MAX_TITLE_LEN];
    char     genre[RB_MAX_TITLE_LEN];
    char     label[RB_MAX_TITLE_LEN];
    char     anlz_path[RB_MAX_PATH_LEN];
    char     anlz_ext_path[RB_MAX_PATH_LEN];
    char     anlz_ex2_path[RB_MAX_PATH_LEN];
    char     audio_path[RB_MAX_PATH_LEN];
    char     thumbnail_path[RB_MAX_PATH_LEN];
    uint32_t artwork_id;
    uint32_t file_size;     // audio file size in bytes
    uint32_t sample_rate;   // playback sample rate in samples/sec
    uint32_t bitrate;       // playback bit rate in bits/sec
    uint8_t  file_type;     // 0x01=MP3, 0x04=M4A, 0x05=FLAC, 0x0B=WAV, 0x0C=AIFF
    float    bpm;
    uint16_t duration;
    uint8_t  key_id;
    uint16_t rating;
    bool     valid;
};

struct PlaylistInfo {
    uint32_t id;
    char     name[RB_MAX_PLAYLIST_NAME];
};

inline const char* fileTypeString(uint8_t ft) {
    switch (ft) {
        case 0x01: return "MP3";
        case 0x04: return "M4A";
        case 0x05: return "FLAC";
        case 0x0B: return "WAV";
        case 0x0C: return "AIFF";
        default:   return "Unknown";
    }
}

// ============================================================================
// RekordboxParser
// ============================================================================
class RekordboxParser {
public:
    RekordboxParser();

    // Supply the filesystem before calling begin().
    // Pass myFS.mscFS (from USBFilesystem) or SD.sdfs, or any FS& you like.
    void setFS(FS& fs) { _fs = &fs; }

    // Open the PDB and populate the arena with the playlist list.
    // 'arena' must point to at least RB_ARENA_BYTES of EXTMEM.
    // setFS() must be called before begin().
    // File stays open for subsequent on-demand reads.
    bool begin(const char* filename, uint8_t* arena, uint32_t arena_size);

    void close();
    bool isOpen() const { return _file.operator bool(); }

    // ── Playlists ───────────────────────────────────────────────────────────
    uint16_t          getPlaylistCount()  const { return _playlist_count; }
    // Returns pointer into arena — valid until next begin() call
    const PlaylistInfo* getPlaylistInfo(uint16_t index) const;
    uint16_t getPlaylistInfoList(PlaylistInfo* out, uint16_t max_count) const;

    // ── On-demand track loading ─────────────────────────────────────────────
    // Scans entry pages then track pages. Fills the Track[] region of the
    // arena. Returns number of tracks loaded.
    uint16_t getTracksForPlaylist(uint32_t playlist_id,
                                  uint16_t max_count = RB_MAX_PLAYLIST_TRACKS);

    // Access tracks loaded by the last getTracksForPlaylist() call.
    uint16_t     getLoadedTrackCount() const { return _loaded_track_count; }
    const Track* getTrack(uint16_t index) const;

    // Read a single track by ID — writes into *out, no arena involvement.
    bool getTrackById(uint16_t track_id, Track* out);

    // Key name lookup (built at begin(), tiny table in internal RAM)
    const char* getKeyName(uint8_t key_id) const;

private:
    // ── File ────────────────────────────────────────────────────────────────
    FS*          _fs;           // set via setFS() — not owned
    mutable File _file;
    uint8_t _buf[RB_PAGE_SIZE];   // page read buffer — internal RAM (fast)

    // ── Table directory ──────────────────────────────────────────────────────
    uint32_t _tbl_first[20];
    uint32_t _tbl_last[20];

    // ── Key table (internal RAM, tiny) ───────────────────────────────────────
    struct KeyEntry { uint8_t id; char name[5]; };
    KeyEntry _keys[24];
    uint8_t  _key_count;

    // ── Arena ────────────────────────────────────────────────────────────────
    uint8_t*  _arena;
    uint32_t  _arena_size;

    // Arena layout pointers (set in begin())
    PlaylistInfo* _playlist_info;    // → arena[0]
    uint16_t*     _track_id_pool;    // → arena[playlist_bytes]
    Track*        _tracks;           // → arena[playlist_bytes + pool_bytes]

    uint16_t _playlist_count;
    uint16_t _loaded_track_count;

    // ── Helpers ──────────────────────────────────────────────────────────────
    bool     readPage(uint32_t page_num);
    uint32_t u32(uint32_t off) const;
    uint16_t u16(uint32_t off) const;
    uint16_t readDeviceSQL(uint16_t pos, char* out, uint16_t max_len) const;

    void parseTableDirectory();
    void parsePlaylistTree();
    void parseKeysTable();

    uint16_t collectPlaylistTrackIds(uint32_t playlist_id, uint16_t max_count);
    bool     findTrackRow(uint16_t track_id, Track* out);
    bool     parseTrackRow(uint16_t row_start, Track* out);
    bool     lookupString(uint8_t table_type, uint32_t row_id,
                          char* out, uint16_t max_len);
};

// ============================================================================
// Implementation
// ============================================================================

inline RekordboxParser::RekordboxParser() {
    _fs                 = nullptr;
    _arena              = nullptr;
    _arena_size         = 0;
    _playlist_info      = nullptr;
    _track_id_pool      = nullptr;
    _tracks             = nullptr;
    _playlist_count     = 0;
    _loaded_track_count = 0;
    _key_count          = 0;
    memset(_tbl_first, 0, sizeof(_tbl_first));
    memset(_tbl_last,  0, sizeof(_tbl_last));
    memset(_keys,      0, sizeof(_keys));
}

inline void RekordboxParser::close() {
    if (_file) _file.close();
}

inline bool RekordboxParser::readPage(uint32_t page_num) {
    if (!_file) return false;
    if (!_file.seek((uint32_t)page_num * RB_PAGE_SIZE)) return false;
    return _file.read(_buf, RB_PAGE_SIZE) == RB_PAGE_SIZE;
}

inline uint32_t RekordboxParser::u32(uint32_t off) const {
    return (uint32_t)_buf[off] | ((uint32_t)_buf[off+1]<<8)
         | ((uint32_t)_buf[off+2]<<16) | ((uint32_t)_buf[off+3]<<24);
}
inline uint16_t RekordboxParser::u16(uint32_t off) const {
    return (uint16_t)_buf[off] | ((uint16_t)_buf[off+1]<<8);
}

inline uint16_t RekordboxParser::readDeviceSQL(uint16_t pos, char* out, uint16_t max_len) const {
    out[0] = '\0';
    if (pos >= RB_PAGE_SIZE || max_len == 0) return 0;
    uint8_t lk = _buf[pos];
    if (lk & 0x01) {
        uint8_t n = (lk >> 1) - 1;
        if (n == 0 || n >= max_len || pos + 1 + n > RB_PAGE_SIZE) return 0;
        memcpy(out, &_buf[pos+1], n); out[n] = '\0'; return n;
    }
    if (pos + 3 >= RB_PAGE_SIZE) return 0;
    uint16_t field_len  = _buf[pos+1] | ((uint16_t)_buf[pos+2]<<8);
    if (field_len < 4) return 0;
    uint16_t data_len   = field_len - 4;
    uint16_t data_start = pos + 4;
    if (lk == 0x40) {
        uint16_t copy = (data_len < max_len-1) ? data_len : max_len-1;
        if (data_start + copy > RB_PAGE_SIZE) return 0;
        memcpy(out, &_buf[data_start], copy); out[copy] = '\0'; return copy;
    }
    if (lk == 0x90) {
        uint16_t chars = data_len / 2;
        uint16_t copy  = (chars < max_len-1) ? chars : max_len-1;
        if (data_start + copy*2 > RB_PAGE_SIZE) return 0;
        for (uint16_t i = 0; i < copy; i++) out[i] = _buf[data_start + i*2];
        out[copy] = '\0'; return copy;
    }
    return 0;
}

inline void RekordboxParser::parseTableDirectory() {
    uint32_t num_tables = u32(8);
    if (num_tables > 20) num_tables = 20;
    uint16_t cursor = 28;
    for (uint32_t i = 0; i < num_tables; i++, cursor += 16) {
        uint32_t type = u32(cursor);
        if (type < 20) {
            _tbl_first[type] = u32(cursor + 8);
            _tbl_last[type]  = u32(cursor + 12);
        }
    }
}

inline void RekordboxParser::parseKeysTable() {
    uint32_t pg = _tbl_first[5], last = _tbl_last[5];
    while (pg && pg <= last && _key_count < 24) {
        if (!readPage(pg)) break;
        uint32_t next = u32(12);
        if (_buf[8]==5 && !(_buf[0x1b]&0x40) && _buf[24]) {
            uint8_t num=_buf[24]; uint16_t cur=40;
            for (uint8_t r=0; r<num && _key_count<24; r++) {
                uint8_t kid=_buf[cur];
                if (kid<1||kid>24) break;
                KeyEntry* k=&_keys[_key_count++];
                k->id=kid;
                k->name[0]=_buf[cur+9]; k->name[1]=_buf[cur+10];
                k->name[2]=_buf[cur+11]; k->name[3]=_buf[cur+12]; k->name[4]='\0';
                if (k->name[3]<33||k->name[3]>125) k->name[3]='\0';
                uint8_t nl=_buf[cur+8];
                if      (nl==5){k->name[1]='\0';cur+=12;}
                else if (nl==7){k->name[2]='\0';cur+=12;}
                else if (nl==9){k->name[3]='\0';cur+=12;}
                else            {cur+=12+4*((((nl-1)/2)-1)/4);}
            }
        }
        pg = next;
    }
}

inline void RekordboxParser::parsePlaylistTree() {
    uint32_t pg = _tbl_first[7], last = _tbl_last[7];
    while (pg && pg <= last) {
        if (!readPage(pg)) break;
        uint32_t next = u32(12);
        if (_buf[8]==7 && !(_buf[0x1b]&0x40) && !_buf[36] && !_buf[37] && _buf[24]) {
            uint8_t  row_count = _buf[24];
            uint16_t cursor    = 40;
            for (uint8_t row=0; row<=row_count && cursor+24<RB_PAGE_SIZE; row++) {
                uint8_t lk       = _buf[cursor+0x14];
                uint8_t name_len = (lk>=3) ? ((lk-1)/2)-1 : 0;
                bool    is_folder= _buf[cursor+16]||_buf[cursor+17]||_buf[cursor+18]||_buf[cursor+19];
                uint8_t pl_id    = _buf[cursor+12];
                if (!is_folder && pl_id>0 && _playlist_info) {
                    int16_t slot=-1;
                    for (uint16_t x=0; x<_playlist_count; x++)
                        if ((uint8_t)_playlist_info[x].id==pl_id){slot=x;break;}
                    if (slot<0 && _playlist_count<RB_MAX_PLAYLISTS) {
                        slot=_playlist_count++;
                        _playlist_info[slot].id=pl_id;
                    }
                    if (slot>=0) {
                        uint8_t copy=(name_len<RB_MAX_PLAYLIST_NAME-1)?name_len:RB_MAX_PLAYLIST_NAME-1;
                        for (uint8_t i=0;i<copy;i++)
                            _playlist_info[slot].name[i]=(cursor+0x15+i<RB_PAGE_SIZE)?_buf[cursor+0x15+i]:0;
                        _playlist_info[slot].name[copy]='\0';
                    }
                }
                cursor += 24 + 4*(name_len/4);
            }
        }
        pg = next;
    }
}

inline bool RekordboxParser::begin(const char* filename, uint8_t* arena, uint32_t arena_size) {
    _arena      = arena;
    _arena_size = arena_size;

    // ── Lay out the arena ───────────────────────────────────────────────────
    // Align each section to 4 bytes
    uint32_t playlist_bytes = ((RB_MAX_PLAYLISTS * sizeof(PlaylistInfo)) + 3) & ~3u;
    uint32_t pool_bytes     = ((RB_MAX_PLAYLIST_TRACKS * sizeof(uint16_t)) + 3) & ~3u;
    uint32_t tracks_bytes   = RB_MAX_PLAYLIST_TRACKS * sizeof(Track);
    uint32_t needed         = playlist_bytes + pool_bytes + tracks_bytes;

    if (arena_size < needed) {
        Serial.printf("RB: arena too small. Have %u, need %u bytes\n", arena_size, needed);
        return false;
    }

    _playlist_info  = reinterpret_cast<PlaylistInfo*>(arena);
    _track_id_pool  = reinterpret_cast<uint16_t*>(arena + playlist_bytes);
    _tracks         = reinterpret_cast<Track*>(arena + playlist_bytes + pool_bytes);

    memset(_playlist_info, 0, playlist_bytes);
    memset(_track_id_pool, 0, pool_bytes);
    memset(_tracks,        0, tracks_bytes);

    _playlist_count      = 0;
    _loaded_track_count  = 0;

    Serial.printf("RB arena layout:\n");
    Serial.printf("  PlaylistInfo @ +0       (%u bytes)\n", playlist_bytes);
    Serial.printf("  Track ID pool @ +%-6u (%u bytes)\n", playlist_bytes, pool_bytes);
    Serial.printf("  Tracks        @ +%-6u (%u bytes)\n", playlist_bytes+pool_bytes, tracks_bytes);
    Serial.printf("  Total: %u / %u bytes used\n", needed, arena_size);

    // ── Open file and parse startup data ────────────────────────────────────
    if (!_fs) { Serial.println("RB: no filesystem set — call setFS() first"); return false; }
    _file = _fs->open(filename, FILE_READ);
    if (!_file) { Serial.println("RB: file open failed"); return false; }

    if (!readPage(0)) { Serial.println("RB: page 0 failed"); return false; }
    if (u32(4) != RB_PAGE_SIZE) { Serial.println("RB: bad page size"); return false; }

    parseTableDirectory();
    parseKeysTable();
    parsePlaylistTree();

    Serial.printf("RB ready: %u playlists\n", _playlist_count);
    return true;
}

inline const PlaylistInfo* RekordboxParser::getPlaylistInfo(uint16_t index) const {
    if (!_playlist_info || index >= _playlist_count) return nullptr;
    return &_playlist_info[index];
}

inline uint16_t RekordboxParser::getPlaylistInfoList(PlaylistInfo* out, uint16_t max_count) const {
    if (!_playlist_info) return 0;
    uint16_t n = (_playlist_count < max_count) ? _playlist_count : max_count;
    for (uint16_t i=0; i<n; i++) out[i] = _playlist_info[i];
    return n;
}

inline const char* RekordboxParser::getKeyName(uint8_t key_id) const {
    for (uint8_t i=0; i<_key_count; i++)
        if (_keys[i].id==key_id) return _keys[i].name;
    return "";
}

inline const Track* RekordboxParser::getTrack(uint16_t index) const {
    if (!_tracks || index >= _loaded_track_count) return nullptr;
    return &_tracks[index];
}

// Scan entry pages and fill _track_id_pool[] with ordered track IDs.
inline uint16_t RekordboxParser::collectPlaylistTrackIds(uint32_t playlist_id,
                                                          uint16_t max_count) {
    memset(_track_id_pool, 0, max_count * sizeof(uint16_t));
    uint16_t highest_ei = 0;

    uint32_t pg=_tbl_first[8], last=_tbl_last[8];
    while (pg && pg<=last) {
        if (!readPage(pg)) break;
        uint32_t next=u32(12);
        if (_buf[8]==8 && !(_buf[0x1b]&0x40) && (_buf[24]||_buf[25])) {
            uint16_t row_count=_buf[34]|((uint16_t)_buf[35]<<8);
            uint16_t cursor=40;
            for (uint16_t row=0; row<=row_count && cursor+12<=RB_PAGE_SIZE; row++) {
                uint16_t ei   =_buf[cursor]  |((uint16_t)_buf[cursor+1]<<8);
                uint16_t tid  =_buf[cursor+4]|((uint16_t)_buf[cursor+5]<<8);
                uint8_t  plid =_buf[cursor+8];
                if (plid==(uint8_t)playlist_id && ei>0 && tid>0) {
                    uint16_t slot=ei-1;
                    if (slot<max_count) {
                        _track_id_pool[slot]=tid;
                        if (ei>highest_ei) highest_ei=ei;
                    }
                }
                cursor+=12;
            }
        }
        pg=next;
    }

    // Compact in-place
    uint16_t write=0;
    for (uint16_t i=0; i<highest_ei && i<max_count; i++)
        if (_track_id_pool[i]) _track_id_pool[write++]=_track_id_pool[i];
    return write;
}

inline bool RekordboxParser::lookupString(uint8_t table_type, uint32_t row_id,
                                           char* out, uint16_t max_len) {
    out[0]='\0';
    if (table_type>=20) return false;
    uint32_t pg=_tbl_first[table_type], last=_tbl_last[table_type];
    while (pg && pg<=last) {
        if (!readPage(pg)) break;
        uint32_t next=u32(12);
        if (!(_buf[0x1b]&0x40)) {
            uint32_t packed=_buf[0x18]|((uint32_t)_buf[0x19]<<8)|((uint32_t)_buf[0x1a]<<16);
            uint16_t nro=packed>>11;
            uint16_t seen[32]; uint8_t seen_n=0;
            for (uint16_t g=0; g<(nro+15)/16; g++) {
                uint16_t gb=RB_PAGE_SIZE-36*(g+1);
                uint16_t slots=((nro-g*16)<16)?(nro-g*16):16;
                for (uint16_t r=0; r<slots; r++) {
                    uint16_t row_off=_buf[gb+r*2]|((uint16_t)_buf[gb+r*2+1]<<8);
                    bool dup=false;
                    for (uint8_t s=0;s<seen_n;s++) if(seen[s]==row_off){dup=true;break;}
                    if (dup) continue;
                    if (seen_n<32) seen[seen_n++]=row_off;
                    uint16_t rs=0x28+row_off;
                    if (rs+10>=RB_PAGE_SIZE) continue;
                    uint16_t sub=_buf[rs]|((uint16_t)_buf[rs+1]<<8);
                    uint32_t found_id=0; uint16_t name_pos=0;
                    if (table_type==2) { // Artist: subtypes 0x0060, 0x0064
                        if (sub!=0x0060&&sub!=0x0064) continue;
                        found_id=u32(rs+4);
                        uint16_t noff=(sub==0x0060)?_buf[rs+9]:(_buf[rs+10]|((uint16_t)_buf[rs+11]<<8));
                        name_pos=rs+noff;
                    } else {
                        found_id=_buf[rs]|((uint32_t)_buf[rs+1]<<8); // u16 id
                        name_pos=rs+8;
                    }
                    if (found_id==row_id && name_pos<RB_PAGE_SIZE) {
                        readDeviceSQL(name_pos, out, max_len);
                        return true;
                    }
                }
            }
        }
        pg=next;
    }
    return false;
}

inline bool RekordboxParser::parseTrackRow(uint16_t rs, Track* out) {
    if ((u16(rs)!=0x0024) || (rs+0x88>RB_PAGE_SIZE)) return false;
    memset(out, 0, sizeof(Track));
    out->id          = u16(rs+0x48);
    out->key_id      = _buf[rs+0x20];
    out->rating      = _buf[rs+0x59];
    out->artwork_id  = u32(rs+0x1c);
    out->sample_rate = u32(rs+0x08);   // bytes 0x08–0x0b: samples per second
    out->file_size   = u32(rs+0x10);   // bytes 0x10–0x13: audio file size in bytes
    out->bitrate     = u32(rs+0x30);   // bytes 0x30–0x33: bits per second
    out->file_type   = _buf[rs+0x5a];  // confirmed byte offset; 0x01=MP3,0x04=M4A,0x05=FLAC,0x0B=WAV,0x0C=AIFF
    out->bpm         = u32(rs+0x38) / 100.0f;
    out->duration    = u16(rs+0x54);
    out->valid     = true;
    uint16_t title_off=u16(rs+0x80); if(title_off) readDeviceSQL(rs+title_off,out->title,RB_MAX_TITLE_LEN);
    uint16_t anlz_off =u16(rs+0x7a);
    if (anlz_off) {
        char tmp[RB_MAX_PATH_LEN]; tmp[0]='\0';
        readDeviceSQL(rs+anlz_off, tmp, RB_MAX_PATH_LEN);
        const char* src=(tmp[0]=='Y'&&tmp[1]=='/')?tmp+2:tmp;
        strncpy(out->anlz_path, src, RB_MAX_PATH_LEN-1); out->anlz_path[RB_MAX_PATH_LEN-1]='\0';
        size_t plen=strlen(out->anlz_path);
        strncpy(out->anlz_ext_path,out->anlz_path,RB_MAX_PATH_LEN-1);
        if(plen>=4){out->anlz_ext_path[plen-3]='E';out->anlz_ext_path[plen-2]='X';out->anlz_ext_path[plen-1]='T';}
        strncpy(out->anlz_ex2_path,out->anlz_path,RB_MAX_PATH_LEN-1);
        if(plen>=4){out->anlz_ex2_path[plen-3]='2';out->anlz_ex2_path[plen-2]='E';out->anlz_ex2_path[plen-1]='X';}
    }
    uint16_t audio_off=u16(rs+0x86);
    if (audio_off) {
        char tmp[RB_MAX_PATH_LEN]; tmp[0]='\0';
        readDeviceSQL(rs+audio_off,tmp,RB_MAX_PATH_LEN);
        const char* src=(tmp[0]=='Y'&&tmp[1]=='/')?tmp+2:tmp;
        strncpy(out->audio_path,src,RB_MAX_PATH_LEN-1); out->audio_path[RB_MAX_PATH_LEN-1]='\0';
    }
    uint32_t artist_id=u32(rs+0x44); if(artist_id) lookupString(2,artist_id,out->artist,RB_MAX_TITLE_LEN);
    uint32_t album_id =u32(rs+0x40); if(album_id)  lookupString(3,album_id, out->album, RB_MAX_TITLE_LEN);
    uint32_t genre_id =u32(rs+0x3c); if(genre_id)  lookupString(1,genre_id, out->genre, RB_MAX_TITLE_LEN);
    uint32_t label_id =u32(rs+0x28); if(label_id)  lookupString(4,label_id, out->label, RB_MAX_TITLE_LEN);
    return true;
}

inline bool RekordboxParser::findTrackRow(uint16_t track_id, Track* out) {
    uint32_t pg=_tbl_first[0], last=_tbl_last[0];
    while (pg && pg<=last) {
        if (!readPage(pg)) break;
        uint32_t next=u32(12);
        if (!(_buf[0x1b]&0x40)) {
            uint32_t packed=_buf[0x18]|((uint32_t)_buf[0x19]<<8)|((uint32_t)_buf[0x1a]<<16);
            uint16_t nro=packed>>11;
            uint16_t seen[64]; uint8_t seen_n=0;
            for (uint16_t g=0;g<(nro+15)/16;g++) {
                uint16_t gb=RB_PAGE_SIZE-36*(g+1);
                uint16_t slots=((nro-g*16)<16)?(nro-g*16):16;
                for (uint16_t r=0;r<slots;r++) {
                    uint16_t row_off=_buf[gb+r*2]|((uint16_t)_buf[gb+r*2+1]<<8);
                    bool dup=false;
                    for(uint8_t s=0;s<seen_n;s++) if(seen[s]==row_off){dup=true;break;}
                    if(dup) continue;
                    if(seen_n<64) seen[seen_n++]=row_off;
                    uint16_t rs=0x28+row_off;
                    if(rs+0x88>=RB_PAGE_SIZE) continue;
                    if(u16(rs)!=0x0024) continue;
                    if(u16(rs+0x48)==track_id) return parseTrackRow(rs,out);
                }
            }
        }
        pg=next;
    }
    return false;
}

inline bool RekordboxParser::getTrackById(uint16_t track_id, Track* out) {
    if (!_file) return false;
    memset(out,0,sizeof(Track));
    return findTrackRow(track_id, out);
}

inline uint16_t RekordboxParser::getTracksForPlaylist(uint32_t playlist_id,
                                                       uint16_t max_count) {
    _loaded_track_count = 0;
    if (!_file || !_tracks || !_track_id_pool) return 0;
    if (max_count > RB_MAX_PLAYLIST_TRACKS) max_count = RB_MAX_PLAYLIST_TRACKS;

    // Step 1: fill _track_id_pool[] from entry pages (all in arena)
    uint16_t id_count = collectPlaylistTrackIds(playlist_id, max_count);

    // Step 2: for each ID, read track row into _tracks[] (all in arena)
    for (uint16_t i=0; i<id_count && _loaded_track_count<max_count; i++) {
        if (_track_id_pool[i]==0) continue;
        if (findTrackRow(_track_id_pool[i], &_tracks[_loaded_track_count]))
            _loaded_track_count++;
    }

    return _loaded_track_count;
}

#endif // REKORDBOX_PARSER_H