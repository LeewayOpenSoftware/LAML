// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the XBF structure reader: real files (MIT fixtures from Microsoft.UI.Xaml), version
// detection, and bounded / malformed input. Usage: xbf_tests <fixture-dir>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

static void Put32(std::vector<uint8_t> &v, size_t at, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v[at + i] = (uint8_t)(x >> (8 * i));
}

struct Expect
{
    const char *name;
    uint32_t minor;
    size_t strings, assemblies, typeNs, types, props, xmlNs;
};

static void TestFixtures(const std::string &dir)
{
    static const Expect cases[] = {
        {"SampleXbf2.xbf", 1, 90, 1, 2, 2, 1, 8},
        {"CustomTypesPage.xbf", 1, 15, 0, 0, 1, 0, 6},
        {"XbfVsmTest.v2.1.xbf", 1, 34, 0, 0, 0, 0, 4},
        {"PageWithConditionalXaml.xbf", 1, 8, 0, 0, 0, 0, 2},
        {"SampleWithChecksum.xbf", 0, 3, 0, 0, 0, 0, 2},
    };
    for (const Expect &e : cases)
    {
        std::vector<uint8_t> d = Load(dir + "/" + e.name);
        CHECK(!d.empty());
        File f;
        Result r = Parse(d.data(), d.size(), f);
        if (!r.ok())
            std::printf("  %s: %s at 0x%llx: %s\n", e.name, StatusName(r.status), (unsigned long long)r.errorOffset, r.message.c_str());
        CHECK(r.ok());
        CHECK(f.version.major == 2 && f.version.minor == e.minor);
        CHECK(f.stringsNullTerminated == (e.minor >= 1));
        CHECK(f.strings.size() == e.strings);
        CHECK(f.assemblies.size() == e.assemblies);
        CHECK(f.typeNamespaces.size() == e.typeNs);
        CHECK(f.types.size() == e.types);
        CHECK(f.properties.size() == e.props);
        CHECK(f.xmlNamespaces.size() == e.xmlNs);
        CHECK(!f.subStreams.empty());
        CHECK(f.warnings.empty());
        CHECK(!Dump(d.data(), d.size(), f).empty());
    }
    // the checksum sample carries a hash field 00 01 02 ... 3f
    {
        std::vector<uint8_t> c = Load(dir + "/SampleWithChecksum.xbf");
        File cf;
        CHECK(Parse(c.data(), c.size(), cf).ok());
        bool pattern = true;
        for (int i = 0; i < 64; i++)
            pattern = pattern && cf.header.hash[i] == (uint8_t)i;
        CHECK(pattern);
    }
    // the first string of the sample is the XAML presentation namespace
    std::vector<uint8_t> d = Load(dir + "/SampleXbf2.xbf");
    File f;
    CHECK(Parse(d.data(), d.size(), f).ok());
    CHECK(GetString(d.data(), d.size(), f, 0) == u"http://schemas.microsoft.com/winfx/2006/xaml/presentation");
    CHECK(GetString(d.data(), d.size(), f, 9999).empty());
}

static void TestHeaderErrors(const std::string &dir)
{
    std::vector<uint8_t> good = Load(dir + "/SampleXbf2.xbf");
    File f;

    CHECK(Parse(nullptr, 0, f).status == Status::TooSmall);
    CHECK(Parse(good.data(), 0, f).status == Status::TooSmall);
    CHECK(Parse(good.data(), 19, f).status == Status::TooSmall);

    std::vector<uint8_t> v = good;
    v[0] = 'Y';
    CHECK(Parse(v.data(), v.size(), f).status == Status::BadMagic);

    v = good;
    Put32(v, 12, 3);  // major 3
    Result r = Parse(v.data(), v.size(), f);
    CHECK(r.status == Status::UnsupportedVersion && f.version.major == 3);

    v = good;
    Put32(v, 12, 1);
    Put32(v, 16, 0);  // version 1.0: recognised, not described
    r = Parse(v.data(), v.size(), f);
    CHECK(r.status == Status::UnsupportedVersion && f.version.major == 1 && f.version.minor == 0);

    v = good;
    Put32(v, 16, 7);  // 2.7
    CHECK(Parse(v.data(), v.size(), f).status == Status::UnsupportedVersion);

    v = good;
    Put32(v, 8, 0xFFFFFFFFu);  // node stream size
    CHECK(Parse(v.data(), v.size(), f).status == Status::SizeMismatch);

    v = good;
    v.push_back(0);
    CHECK(Parse(v.data(), v.size(), f).status == Status::SizeMismatch);

    v = good;
    Put32(v, 4, 100);  // metadata smaller than its header, sizes still wrong
    CHECK(!Parse(v.data(), v.size(), f).ok());

    v = good;
    Put32(v, 0x14, 0x80);  // string table offset must be 0x78
    CHECK(Parse(v.data(), v.size(), f).status == Status::OffsetError);

    v = good;
    Put32(v, 0x1c, 0x7fffffff);  // assembly list offset out of range
    CHECK(Parse(v.data(), v.size(), f).status == Status::OffsetError);

    v = good;
    Put32(v, 0x24, 0x100);  // type namespace list before the assembly list
    CHECK(Parse(v.data(), v.size(), f).status == Status::OffsetError);
}

static void TestCountsAndLimits(const std::string &dir)
{
    std::vector<uint8_t> good = Load(dir + "/SampleXbf2.xbf");
    File f;

    std::vector<uint8_t> v = good;
    Put32(v, 12 + 0x78, 0xFFFFFFFFu);  // string count
    CHECK(Parse(v.data(), v.size(), f).status == Status::LimitExceeded);

    v = good;
    Put32(v, 12 + 0x78, 0x000FFFFF);  // large but within the limit: runs past the table
    CHECK(Parse(v.data(), v.size(), f).status == Status::Truncated);

    v = good;
    Put32(v, 12 + 0x78 + 4, 0xFFFFFFFFu);  // first string length
    CHECK(Parse(v.data(), v.size(), f).status == Status::LimitExceeded);

    v = good;
    Put32(v, 12 + 0x78 + 4, 0x000FFFFF);  // within the limit, past the table
    CHECK(Parse(v.data(), v.size(), f).status == Status::Truncated);

    v = good;
    Put32(v, 12 + 0xb1c, 0xFFFFFFFFu);  // assembly count
    CHECK(Parse(v.data(), v.size(), f).status == Status::LimitExceeded);

    v = good;
    Put32(v, 12 + 0xb1c, 5);  // fewer bytes than five assemblies need
    CHECK(Parse(v.data(), v.size(), f).status == Status::Truncated);

    v = good;
    Put32(v, 12 + 0xb1c, 0);  // fewer items than the region holds: a gap
    CHECK(Parse(v.data(), v.size(), f).status == Status::OffsetError);

    Limits tiny;
    tiny.maxStrings = 10;
    CHECK(Parse(good.data(), good.size(), tiny, f).status == Status::LimitExceeded);
    tiny = Limits();
    tiny.maxStringChars = 8;
    CHECK(Parse(good.data(), good.size(), tiny, f).status == Status::LimitExceeded);
    tiny = Limits();
    tiny.maxFileSize = 100;
    CHECK(Parse(good.data(), good.size(), tiny, f).status == Status::LimitExceeded);
    tiny = Limits();
    tiny.maxSubStreams = 0;
    CHECK(Parse(good.data(), good.size(), tiny, f).status == Status::LimitExceeded);

    // node stream index
    size_t node = 12 + 0xb8c;
    v = good;
    Put32(v, node, 0xFFFFFFFFu);
    CHECK(Parse(v.data(), v.size(), f).status == Status::LimitExceeded);
    v = good;
    Put32(v, node, 0x0FFFFF);
    CHECK(Parse(v.data(), v.size(), f).status == Status::Truncated);
    v = good;
    Put32(v, node + 4, 0xFFFFFFF0u);  // first node offset past the data
    CHECK(Parse(v.data(), v.size(), f).status == Status::BadStreamIndex);
}

static void TestTruncation(const std::string &dir)
{
    // every proper prefix of every fixture is rejected, none crashes
    static const char *names[] = {"SampleXbf2.xbf", "CustomTypesPage.xbf", "XbfVsmTest.v2.1.xbf",
                                  "PageWithConditionalXaml.xbf", "SampleWithChecksum.xbf"};
    for (const char *n : names)
    {
        std::vector<uint8_t> d = Load(dir + "/" + n);
        File f;
        bool anyAccepted = false;
        for (size_t len = 0; len < d.size(); len++)
            if (Parse(d.data(), len, f).ok())
                anyAccepted = true;
        CHECK(!anyAccepted);
    }
}

static void TestMutations(const std::string &dir)
{
    // deterministic mutation sweep: single byte replacements and random multi-byte damage; the reader
    // must return (any status) and, when it accepts, its tables must be consistent with the file.
    std::vector<uint8_t> good = Load(dir + "/SampleXbf2.xbf");
    File f;
    uint32_t seed = 12345;
    auto next = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    size_t accepted = 0, rejected = 0;
    for (size_t at = 0; at < 0x200 && at < good.size(); at++)
    {
        for (uint8_t value : {0x00, 0x01, 0x7f, 0x80, 0xff})
        {
            std::vector<uint8_t> v = good;
            v[at] = value;
            Result r = Parse(v.data(), v.size(), f);
            if (r.ok())
            {
                accepted++;
                for (const StringRef &s : f.strings)
                    CHECK((uint64_t)s.fileOffset + (uint64_t)s.length * 2 <= v.size());
                CHECK(f.nodeStreamFileOffset + f.streamTableBytes <= v.size());
            }
            else
                rejected++;
        }
    }
    for (int i = 0; i < 20000; i++)
    {
        std::vector<uint8_t> v = good;
        int hits = 1 + next() % 6;
        for (int h = 0; h < hits; h++)
            v[next() % v.size()] = (uint8_t)next();
        if (next() % 4 == 0)
            v.resize(next() % (v.size() + 1));
        Result r = Parse(v.data(), v.size(), f);
        if (r.ok())
        {
            accepted++;
            for (const StringRef &s : f.strings)
                CHECK((uint64_t)s.fileOffset + (uint64_t)s.length * 2 <= v.size());
        }
        else
            rejected++;
    }
    std::printf("  mutation sweep: %zu accepted, %zu rejected\n", accepted, rejected);
    CHECK(rejected > 0);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::puts("usage: xbf_tests <fixture-dir>");
        return 2;
    }
    std::string dir = argv[1];
    TestFixtures(dir);
    TestHeaderErrors(dir);
    TestCountsAndLimits(dir);
    TestTruncation(dir);
    TestMutations(dir);
    std::printf("xbf_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
