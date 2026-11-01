// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the XBF node-stream decoder: the MIT fixtures, hand-built streams for every rejection,
// truncation at every byte, and seeded mutation. Usage: xbf_nodes_tests <fixture-dir>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/xbf/xbf_nodes.h"
#include "../src/xbf/xbf_reader.h"

using namespace laml::xbf;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                   \
    do                                                                                \
    {                                                                                 \
        g_checks++;                                                                   \
        if (!(cond))                                                                  \
        {                                                                             \
            g_failed++;                                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                             \
    } while (0)

static std::vector<uint8_t> Load(const std::string &path)
{
    std::vector<uint8_t> v;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return v;
    uint8_t b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0)
        v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}

static size_t Count(const Decoded &d, NodeType t)
{
    size_t n = 0;
    for (const auto &s : d.streams)
        for (const auto &x : s.nodes)
            n += x.type == t;
    return n;
}

static void TestCorpus(const std::string &dir)
{
    struct Case { const char *name; size_t streams, nodes0, opaque; };
    static const Case cases[] = {
        {"SampleWithChecksum.xbf", 1, 6, 0},
        {"CustomTypesPage.xbf", 1, 53, 0},
        {"PageWithConditionalXaml.xbf", 1, 47, 0},
        {"SampleXbf2.xbf", 1, 449, 0},
        {"XbfVsmTest.v2.1.xbf", 7, 8, 3},
    };
    for (const Case &c : cases)
    {
        std::vector<uint8_t> d = Load(dir + "/" + c.name);
        CHECK(!d.empty());
        File f;
        CHECK(Parse(d.data(), d.size(), f).ok());
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, dec);
        CHECK(dec.ok());
        CHECK(dec.streams.size() == c.streams);
        CHECK(!dec.streams.empty() && dec.streams[0].nodes.size() == c.nodes0);
        CHECK(dec.opaqueStreams == c.opaque);
        // Sub-streams of the multi-stream file are fragments that continue a scope of their parent
        // stream, so they may end with a scope open (reported as a warning); every other file is balanced.
        bool fragments = c.streams > 1;
        for (const auto &s : dec.streams)
            if (s.status == StreamStatus::Ok && !fragments)
                CHECK(s.finalDepth == 0 && s.finalConditionalDepth == 0);
        CHECK(dec.warnings.empty() == !fragments);
        std::string text = DumpNodes(d.data(), d.size(), f, dec);
        CHECK(text.find("node streams:") == 0);
    }

    // spot checks of decoded content
    {
        std::vector<uint8_t> d = Load(dir + "/SampleWithChecksum.xbf");
        File f;
        Parse(d.data(), d.size(), f);
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, dec);
        const auto &n = dec.streams[0].nodes;
        CHECK(n.size() == 6);
        CHECK(n[0].type == NodeType::PushScopeAddNamespace && n[0].text.empty());
        CHECK(n[1].type == NodeType::AddNamespace && n[1].text == u"x");
        CHECK(n[3].type == NodeType::SetName && n[3].constant.kind == ConstKind::SharedString);
        CHECK(n[4].type == NodeType::SetValueConstant && n[4].constant.kind == ConstKind::Color &&
              n[4].constant.integer == 0xFF008000);
        CHECK(n[5].type == NodeType::EndInitPopScope && n[5].depth == 1);
    }
    {
        std::vector<uint8_t> d = Load(dir + "/PageWithConditionalXaml.xbf");
        File f;
        Parse(d.data(), d.size(), f);
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, dec);
        CHECK(Count(dec, NodeType::BeginConditionalScope) == 4);
        CHECK(Count(dec, NodeType::EndConditionalScope) == 4);
        CHECK(Count(dec, NodeType::PushConstant) >= 2);
    }
    {
        std::vector<uint8_t> d = Load(dir + "/XbfVsmTest.v2.1.xbf");
        File f;
        Parse(d.data(), d.size(), f);
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, dec);
        CHECK(Count(dec, NodeType::SetCustomRuntimeData) == 3);
        CHECK(Count(dec, NodeType::SetDeferredProperty) >= 2);
        bool sawOpaque = false;
        for (const auto &s : dec.streams)
            if (s.status == StreamStatus::OpaqueData)
            {
                sawOpaque = true;
                CHECK(!s.nodes.empty() && s.nodes.back().type == NodeType::SetCustomRuntimeData);
            }
        CHECK(sawOpaque);
    }
    {
        // grid lengths: unit first, then value (derived from the corpus: a 5.0 star/pixel length)
        std::vector<uint8_t> d = Load(dir + "/SampleXbf2.xbf");
        File f;
        Parse(d.data(), d.size(), f);
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, dec);
        bool saw = false;
        for (const auto &n : dec.streams[0].nodes)
            if (n.constant.kind == ConstKind::GridLength)
            {
                saw = true;
                CHECK(n.constant.real[0] >= 0.0f && n.constant.real[0] < 1e6f);
            }
        CHECK(saw);
        std::string text = DumpNodes(d.data(), d.size(), f, dec);
        CHECK(text.find("gridlength(") != std::string::npos);
    }
}

// A host file whose first node stream is replaced by `bytes`.
struct Host
{
    std::vector<uint8_t> data;
    File file;
    bool ok = false;
    uint64_t nodeStart = 0;
    uint32_t room = 0;

    explicit Host(const std::string &dir, const char *name = "SampleXbf2.xbf", size_t streams = 1)
    {
        data = Load(dir + "/" + name);
        ok = Parse(data.data(), data.size(), file).ok() && file.subStreams.size() == streams;
        if (ok)
        {
            nodeStart = file.nodeStreamFileOffset + file.streamTableBytes + file.subStreams[0].nodeOffset;
            room = file.subStreams[0].lineOffset - file.subStreams[0].nodeOffset;
        }
    }

    Decoded Run(const std::vector<uint8_t> &bytes, const DecodeLimits &lim = DecodeLimits())
    {
        std::vector<uint8_t> d = data;
        File f = file;
        size_t n = bytes.size() < room ? bytes.size() : room;
        std::memcpy(d.data() + nodeStart, bytes.data(), n);
        f.subStreams[0].lineOffset = f.subStreams[0].nodeOffset + (uint32_t)n;
        Decoded dec;
        DecodeNodes(d.data(), d.size(), f, lim, dec);
        return dec;
    }
};

typedef std::vector<uint8_t> Bytes;
static Bytes Cat(Bytes a, const Bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static void TestMalformed(const std::string &dir)
{
    Host h(dir);
    CHECK(h.ok);
    if (!h.ok)
        return;
    CHECK(h.file.types.size() == 2 && h.file.properties.size() == 1 && h.file.strings.size() == 90);

    auto status = [&](const Bytes &b) { return h.Run(b).streams[0].status; };
    const uint8_t PUSH = 1, POP = 2, ADDNS = 3, PUSHC = 4, SETVAL = 7, CHECKPEER = 11, DEFER = 17, CUSTOM = 15,
                  PSCTBI = 20, PSCTCBI = 21, SVC = 26, ENDINIT = 33, BEGINCOND = 38, ENDCOND = 39;

    // valid hand-built streams
    CHECK(status({PUSH, POP}) == StreamStatus::Ok);
    CHECK(status({PUSH, PUSH, ENDINIT, POP}) == StreamStatus::Ok);
    CHECK(status({PSCTBI, 1, 0, ENDINIT}) == StreamStatus::Ok);                 // type 1 exists
    CHECK(status({PSCTBI, 0x00, 0x80, ENDINIT}) == StreamStatus::Ok);           // trusted well-known type 0
    CHECK(status({PUSHC, 1}) == StreamStatus::Ok);                              // BoolFalse
    CHECK(status({PUSHC, 3, 0, 0, 0x80, 0x3F}) == StreamStatus::Ok);            // float 1.0
    CHECK(status({PUSHC, 10}) == StreamStatus::Ok);                             // NullString
    CHECK(status({PUSHC, 9, 2, 0, 0, 0, 'h', 0, 'i', 0}) == StreamStatus::Ok);  // unique string "hi"
    CHECK(status({PUSHC, 5, 3, 0}) == StreamStatus::Ok);                        // shared string 3
    CHECK(status({PUSHC, 11, 0x10, 0x01, 7, 0, 0, 0}) == StreamStatus::Ok);     // enum
    CHECK(status({}) == StreamStatus::Ok);                                      // empty stream
    {
        Decoded d = h.Run({PUSHC, 9, 2, 0, 0, 0, 'h', 0, 'i', 0});
        CHECK(d.streams[0].nodes.size() == 1 && d.streams[0].nodes[0].constant.text == u"hi");
        Decoded e = h.Run({PUSHC, 3, 0, 0, 0xC0, 0x3F});
        CHECK(e.streams[0].nodes[0].constant.real[0] == 1.5f);
    }

    // opcodes
    CHECK(status({0}) == StreamStatus::BadOpcode);
    CHECK(status({5}) == StreamStatus::BadOpcode);
    CHECK(status({37}) == StreamStatus::BadOpcode);     // EndOfStream is never serialized
    CHECK(status({128}) == StreamStatus::BadOpcode);    // optimiser temporaries are not valid in a file
    CHECK(status({255}) == StreamStatus::BadOpcode);
    CHECK(status({41}) == StreamStatus::BadOpcode);
    CHECK(h.Run({0}).failedStreams == 1 && !h.Run({0}).ok());

    // scopes
    CHECK(status({POP}) == StreamStatus::BadScope);
    CHECK(status({PUSH, POP, POP}) == StreamStatus::BadScope);
    CHECK(status({ENDINIT}) == StreamStatus::BadScope);
    CHECK(status({ENDCOND}) == StreamStatus::BadScope);
    {
        Decoded d = h.Run({PUSH, PUSH, POP});   // left open: a warning, not an error
        CHECK(d.ok() && d.streams[0].finalDepth == 1 && !d.warnings.empty());
    }
    {
        DecodeLimits lim;
        lim.maxDepth = 3;
        CHECK(h.Run({PUSH, PUSH, PUSH, PUSH}, lim).streams[0].status == StreamStatus::LimitExceeded);
        CHECK(h.Run({PUSH, PUSH, PUSH}, lim).streams[0].status == StreamStatus::Ok);
        DecodeLimits few;
        few.maxNodesPerStream = 2;
        CHECK(h.Run({PUSH, POP, PUSH}, few).streams[0].status == StreamStatus::LimitExceeded);
        DecodeLimits tot;
        tot.maxTotalNodes = 1;
        CHECK(h.Run({PUSH, POP}, tot).streams[0].status == StreamStatus::LimitExceeded);
    }

    // references outside their tables
    CHECK(status({PSCTBI, 2, 0}) == StreamStatus::BadReference);          // 2 types only
    CHECK(status({PSCTBI, 0xFF, 0x7F}) == StreamStatus::BadReference);
    CHECK(status({PSCTBI, 0xFF, 0xFF}) == StreamStatus::BadReference);    // trusted but >= 0x2000
    CHECK(status({SETVAL, 1, 0}) == StreamStatus::BadReference);          // 1 property only
    CHECK(status({ADDNS, 8, 0, 0, 0, 0, 0}) == StreamStatus::BadReference);   // 8 xml namespaces only
    CHECK(status({ADDNS, 0, 0x80, 0, 0, 0, 0}) == StreamStatus::BadReference);   // trusted flag on a namespace
    CHECK(status({PUSHC, 5, 90, 0}) == StreamStatus::BadReference);       // 90 strings only
    CHECK(status({PUSHC, 0}) == StreamStatus::BadReference);              // constant kind None
    CHECK(status({PUSHC, 12}) == StreamStatus::BadReference);             // beyond Enum
    CHECK(status({PUSHC, 11, 0, 0, 0, 0, 0, 0}) == StreamStatus::BadReference);   // enum type 0
    CHECK(status({BEGINCOND, 0, 0, 200, 0}) == StreamStatus::BadReference);
    CHECK(status({DEFER, 0, 0x80, 0, 0, 0}) == StreamStatus::BadReference);  // stream 0 is the stream itself
    CHECK(status({DEFER, 0, 0x80, 5, 0, 0}) == StreamStatus::BadReference);  // no stream 5
    CHECK(status({CUSTOM, 0, 0, 0}) == StreamStatus::BadReference);
    CHECK(status({CUSTOM, 9, 0, 0}) == StreamStatus::BadReference);

    // truncated operands
    CHECK(status({PSCTBI}) == StreamStatus::Truncated);
    CHECK(status({PSCTBI, 1}) == StreamStatus::Truncated);
    CHECK(status({SVC, 0, 0}) == StreamStatus::Truncated);
    CHECK(status({PUSHC}) == StreamStatus::Truncated);
    CHECK(status({PUSHC, 3, 0, 0}) == StreamStatus::Truncated);
    CHECK(status({PUSHC, 6, 0, 0, 0, 0}) == StreamStatus::Truncated);     // thickness needs 16 bytes
    CHECK(status({PUSHC, 9, 4, 0, 0, 0, 'a', 0}) == StreamStatus::Truncated);   // 4 chars announced, 1 present
    CHECK(status({CHECKPEER, 1, 0}) == StreamStatus::Truncated);
    CHECK(status({DEFER, 0, 0}) == StreamStatus::Truncated);

    // strings: absurd lengths are limited or truncated, never allocated
    CHECK(status({PUSHC, 9, 0xFF, 0xFF, 0xFF, 0x7F}) == StreamStatus::LimitExceeded);
    CHECK(status({CHECKPEER, 0xFF, 0xFF, 0xFF, 0xFF}) == StreamStatus::LimitExceeded);
    {
        DecodeLimits lim;
        lim.maxStringChars = 2;
        CHECK(h.Run({CHECKPEER, 3, 0, 0, 0, 'a', 0, 'b', 0, 'c', 0}, lim).streams[0].status == StreamStatus::LimitExceeded);
        CHECK(h.Run({CHECKPEER, 2, 0, 0, 0, 'a', 0, 'b', 0}, lim).streams[0].status == StreamStatus::Ok);
    }

    // 7-bit integers: overlong and overflowing encodings are rejected
    CHECK(status({DEFER, 0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00}) == StreamStatus::BadReference);   // 6 bytes
    CHECK(status({DEFER, 0, 0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F}) == StreamStatus::BadReference);   // > 32 bits
    CHECK(status({DEFER, 0, 0x80, 0x80}) == StreamStatus::Truncated);
    // deferred names: a count larger than the bytes left is a truncation, a huge count is limited
    {
        Host multi(dir, "XbfVsmTest.v2.1.xbf", 7);   // has other streams to refer to
        CHECK(multi.ok);
        auto mstatus = [&](const Bytes &b) { return multi.Run(b).streams[0].status; };
        CHECK(multi.ok && mstatus({DEFER, 0, 0x80, 1, 0, 0}) == StreamStatus::Ok);
        CHECK(multi.ok && mstatus({DEFER, 0, 0x80, 1, 1, 0, 3, 0}) == StreamStatus::Ok);   // one static name: string 3
        CHECK(multi.ok && mstatus({DEFER, 0, 0x80, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0}) == StreamStatus::LimitExceeded);
        CHECK(multi.ok && mstatus({DEFER, 0, 0x80, 1, 3, 0}) == StreamStatus::Truncated);
        CHECK(multi.ok && mstatus({DEFER, 0, 0x80, 7, 0, 0}) == StreamStatus::BadReference);   // 7 streams: 0..6
        // custom runtime data: counted as a stop at the payload, with the type index recorded
        Decoded d = multi.Run({CUSTOM, 1, 0, 0, 12, 0xAA, 0xBB});
        CHECK(multi.ok && d.streams[0].status == StreamStatus::OpaqueData && d.ok() && d.opaqueStreams == 3);
        CHECK(multi.ok && d.streams[0].nodes.size() == 1 && d.streams[0].nodes[0].runtimeDataType == 12 &&
              d.streams[0].nodes[0].targetStream == 1);
        CHECK(multi.ok && mstatus({CUSTOM, 1, 0, 0}) == StreamStatus::Truncated);   // the type index is missing
    }
    {
        Decoded d = h.Run({CUSTOM, 0, 0, 0, 12, 0xAA, 0xBB});
        CHECK(d.streams[0].status == StreamStatus::BadReference);   // stream 0 refers to itself
    }

    (void)PSCTCBI;
}

// Truncating the node bytes at every length must give a status for the cut, never a crash, and
// the nodes decoded before the cut stay a prefix of the full decode.
static void TestTruncation(const std::string &dir)
{
    static const char *names[] = {"SampleXbf2.xbf", "CustomTypesPage.xbf", "PageWithConditionalXaml.xbf",
                                  "SampleWithChecksum.xbf", "XbfVsmTest.v2.1.xbf"};
    for (const char *name : names)
    {
        std::vector<uint8_t> d = Load(dir + "/" + name);
        File f;
        if (!Parse(d.data(), d.size(), f).ok())
        {
            CHECK(false);
            continue;
        }
        Decoded full;
        DecodeNodes(d.data(), d.size(), f, full);
        const SubStream s0 = f.subStreams[0];
        uint32_t len = full.streams[0].nodeBytes;
        for (uint32_t cut = 0; cut <= len; cut++)
        {
            File g = f;
            g.subStreams[0].lineOffset = s0.nodeOffset + cut;
            Decoded part;
            DecodeNodes(d.data(), d.size(), g, part);
            const DecodedStream &p = part.streams[0];
            // the decoded prefix matches the full decode node for node
            CHECK(p.nodes.size() <= full.streams[0].nodes.size());
            bool same = true;
            for (size_t i = 0; i < p.nodes.size() && same; i++)
                same = p.nodes[i].type == full.streams[0].nodes[i].type && p.nodes[i].offset == full.streams[0].nodes[i].offset;
            CHECK(same);
            if (cut == len)
                CHECK(p.status == full.streams[0].status);
            else if (p.status == StreamStatus::Ok)
            {
                // a cut that lands between two nodes is a valid shorter stream
                CHECK(p.nodes.empty() || p.nodes.back().offset < cut);
            }
            else
                CHECK(p.status == StreamStatus::Truncated || p.status == StreamStatus::OpaqueData ||
                      p.status == StreamStatus::BadReference || p.status == StreamStatus::LimitExceeded ||
                      p.status == StreamStatus::BadScope);
        }
    }
}

// Seeded byte mutation inside the node streams: the decoder must always return, keep every node
// inside its stream, and keep depth bookkeeping consistent.
static void TestMutation(const std::string &dir)
{
    static const char *names[] = {"SampleXbf2.xbf", "CustomTypesPage.xbf", "PageWithConditionalXaml.xbf",
                                  "SampleWithChecksum.xbf", "XbfVsmTest.v2.1.xbf"};
    uint32_t seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    size_t rounds = 0, failedRounds = 0;
    for (const char *name : names)
    {
        std::vector<uint8_t> base = Load(dir + "/" + name);
        File f;
        if (!Parse(base.data(), base.size(), f).ok())
        {
            CHECK(false);
            continue;
        }
        uint64_t lo = f.nodeStreamFileOffset + f.streamTableBytes;
        for (int i = 0; i < 4000; i++)
        {
            std::vector<uint8_t> d = base;
            int flips = 1 + (int)(rnd() % 4);
            for (int k = 0; k < flips; k++)
            {
                size_t at = lo + rnd() % (d.size() - lo);
                switch (rnd() % 3)
                {
                case 0: d[at] = (uint8_t)rnd(); break;
                case 1: d[at] ^= (uint8_t)(1u << (rnd() % 8)); break;
                default: d[at] = (rnd() & 1) ? 0xFF : 0x00; break;
                }
            }
            Decoded dec;
            DecodeNodes(d.data(), d.size(), f, dec);   // must return
            rounds++;
            bool sane = dec.streams.size() == f.subStreams.size();
            for (const auto &s : dec.streams)
                for (const auto &n : s.nodes)
                    sane = sane && n.offset < s.nodeBytes;
            if (!sane)
                failedRounds++;
            if (!dec.ok())
                (void)DumpNodes(d.data(), d.size(), f, dec);
        }
    }
    CHECK(failedRounds == 0);
    CHECK(rounds == 20000);
}

// Stream-index damage reaches the decoder only through a file that Parse accepted, but the
// decoder must still cope with offsets that are not usable.
static void TestRanges(const std::string &dir)
{
    std::vector<uint8_t> d = Load(dir + "/XbfVsmTest.v2.1.xbf");
    File f;
    CHECK(Parse(d.data(), d.size(), f).ok());
    {
        File g = f;
        g.subStreams[1].nodeOffset = g.subStreams[0].lineOffset - 1;   // out of order
        Decoded dec;
        DecodeNodes(d.data(), d.size(), g, dec);
        CHECK(!dec.ok());
        CHECK(dec.streams[0].status == StreamStatus::BadRange || dec.streams[1].status == StreamStatus::BadRange);
    }
    {
        File g = f;
        g.subStreams.back().lineOffset = 0x7FFFFFFF;
        Decoded dec;
        DecodeNodes(d.data(), d.size(), g, dec);
        CHECK(dec.streams.back().status == StreamStatus::BadRange);
    }
    {
        File g = f;
        g.subStreams.clear();
        Decoded dec;
        DecodeNodes(d.data(), d.size(), g, dec);
        CHECK(dec.ok() && dec.streams.empty());
    }
    {
        // the data buffer is shorter than the file claims
        Decoded dec;
        DecodeNodes(d.data(), d.size() / 2, f, dec);
        CHECK(!dec.ok());
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::fputs("usage: xbf_nodes_tests <fixture-dir>\n", stderr);
        return 2;
    }
    std::string dir = argv[1];
    TestCorpus(dir);
    TestMalformed(dir);
    TestTruncation(dir);
    TestMutation(dir);
    TestRanges(dir);
    std::printf("xbf_nodes_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
