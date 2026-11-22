#include "MovFix.h"

#include <QFile>
#include <cstdint>
#include <vector>

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint64_t be64(const uint8_t* p) { return (uint64_t(be32(p)) << 32) | be32(p + 4); }
void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
void put64(uint8_t* p, uint64_t v) { put32(p, uint32_t(v >> 32)); put32(p + 4, uint32_t(v)); }

struct Box { uint32_t type; uint64_t start, size, body; };  // body = offset of the payload

uint32_t fourcc(const char* s) { return (uint32_t(s[0]) << 24) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 8) | uint32_t(s[3]); }

std::vector<Box> children(const std::vector<uint8_t>& d, uint64_t from, uint64_t to) {
    std::vector<Box> out;
    uint64_t off = from;
    while (off + 8 <= to) {
        uint64_t size = be32(&d[off]);
        const uint32_t type = be32(&d[off + 4]);
        uint64_t hdr = 8;
        if (size == 1) { size = be64(&d[off + 8]); hdr = 16; }
        else if (size == 0) size = to - off;
        if (size < hdr || off + size > to) break;
        out.push_back({type, off, size, off + hdr});
        off += size;
    }
    return out;
}

const Box* find(const std::vector<Box>& v, const char* t) {
    for (const auto& b : v)
        if (b.type == fourcc(t)) return &b;
    return nullptr;
}

// Version 0: 32-bit times; version 1: 64-bit.
uint64_t readDur(const std::vector<uint8_t>& d, uint64_t at, bool v1) { return v1 ? be64(&d[at]) : be32(&d[at]); }
void writeDur(std::vector<uint8_t>& d, uint64_t at, bool v1, uint64_t v) { if (v1) put64(&d[at], v); else put32(&d[at], uint32_t(v)); }
}  // namespace

namespace MovFix {

QString fixLastVideoSampleDuration(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return "cannot open " + path;
    // Only the moov box is read and rewritten (it is small); mdat is never touched.
    const qint64 fileSize = f.size();
    std::vector<uint8_t> head(16);
    qint64 off = 0;
    qint64 moovAt = -1, moovSize = 0;
    while (off + 8 <= fileSize) {
        f.seek(off);
        if (f.read(reinterpret_cast<char*>(head.data()), 16) < 8) break;
        uint64_t size = be32(head.data());
        if (size == 1) size = be64(head.data() + 8);
        else if (size == 0) size = uint64_t(fileSize - off);
        if (be32(head.data() + 4) == fourcc("moov")) { moovAt = off; moovSize = qint64(size); break; }
        if (size < 8) break;
        off += qint64(size);
    }
    if (moovAt < 0) return "no moov box";
    f.seek(moovAt);
    std::vector<uint8_t> d(static_cast<size_t>(moovSize));
    if (f.read(reinterpret_cast<char*>(d.data()), moovSize) != moovSize) return "short moov";

    const auto top = children(d, 0, d.size());
    if (top.empty()) return "bad moov";
    const Box& moov = top[0];
    const auto mv = children(d, moov.body, moov.start + moov.size);
    const Box* mvhd = find(mv, "mvhd");
    if (!mvhd) return "no mvhd";
    const bool mv1 = d[mvhd->body] == 1;
    const uint32_t movieTs = be32(&d[mvhd->body + (mv1 ? 20 : 12)]);
    const uint64_t mvhdDurAt = mvhd->body + (mv1 ? 24 : 16);

    bool changed = false;
    uint64_t longest = 0;
    for (const Box& trak : mv) {
        if (trak.type != fourcc("trak")) continue;
        const auto tk = children(d, trak.body, trak.start + trak.size);
        const Box* tkhd = find(tk, "tkhd");
        const Box* mdia = find(tk, "mdia");
        if (!tkhd || !mdia) continue;
        const bool tk1 = d[tkhd->body] == 1;
        const uint64_t tkhdDurAt = tkhd->body + (tk1 ? 28 : 20);
        uint64_t tkDur = readDur(d, tkhdDurAt, tk1);

        const auto md = children(d, mdia->body, mdia->start + mdia->size);
        const Box* mdhd = find(md, "mdhd");
        const Box* hdlr = find(md, "hdlr");
        const Box* minf = find(md, "minf");
        const bool video = hdlr && be32(&d[hdlr->body + 8]) == fourcc("vide");
        if (video && mdhd && minf) {
            const Box* stbl = find(children(d, minf->body, minf->start + minf->size), "stbl");
            const Box* stts = stbl ? find(children(d, stbl->body, stbl->start + stbl->size), "stts") : nullptr;
            if (stts) {
                const uint32_t n = be32(&d[stts->body + 4]);
                const uint64_t last = stts->body + 8 + uint64_t(n - 1) * 8;
                if (n >= 2 && be32(&d[last]) == 1 && be32(&d[last + 4]) == 0) {
                    const uint32_t delta = be32(&d[last - 8 + 4]);  // the frame duration before it
                    put32(&d[last + 4], delta);
                    const bool md1 = d[mdhd->body] == 1;
                    const uint32_t mediaTs = be32(&d[mdhd->body + (md1 ? 20 : 12)]);
                    const uint64_t mdhdDurAt = mdhd->body + (md1 ? 24 : 16);
                    writeDur(d, mdhdDurAt, md1, readDur(d, mdhdDurAt, md1) + delta);
                    const uint64_t add = mediaTs ? (uint64_t(delta) * movieTs + mediaTs / 2) / mediaTs : 0;
                    tkDur += add;
                    writeDur(d, tkhdDurAt, tk1, tkDur);
                    // The edit list (if any) shows the track for its segment duration.
                    if (const Box* edts = find(tk, "edts")) {
                        if (const Box* elst = find(children(d, edts->body, edts->start + edts->size), "elst")) {
                            const bool el1 = d[elst->body] == 1;
                            const uint32_t entries = be32(&d[elst->body + 4]);
                            const uint64_t entrySize = el1 ? 20 : 12;
                            const uint64_t lastEntry = elst->body + 8 + uint64_t(entries - 1) * entrySize;
                            if (entries >= 1) writeDur(d, lastEntry, el1, readDur(d, lastEntry, el1) + add);
                        }
                    }
                    changed = true;
                }
            }
        }
        longest = std::max(longest, tkDur);
    }
    if (!changed) return {};
    if (longest > readDur(d, mvhdDurAt, mv1)) writeDur(d, mvhdDurAt, mv1, longest);
    f.seek(moovAt);
    if (f.write(reinterpret_cast<const char*>(d.data()), moovSize) != moovSize) return "write failed";
    return {};
}

}  // namespace MovFix
