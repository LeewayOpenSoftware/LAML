// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// See xbf_reader.h and PROVENANCE.md for what this reader is derived from.
#include "xbf_reader.h"

#include <cstdio>
#include <cstring>

namespace laml::xbf {

namespace {

constexpr uint64_t kPreambleBytes = 12;   // magic, metadata size, node stream size
constexpr uint64_t kVersionBytes = 8;     // major, minor (first bytes of the metadata stream)
constexpr uint64_t kHeaderBytes = 6 * 8 + 64;
constexpr uint64_t kFirstListOffset = kVersionBytes + kHeaderBytes;  // 120: the string table starts here
constexpr size_t kMaxWarnings = 64;

uint32_t Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t Le64(const uint8_t *p)
{
    return (uint64_t)Le32(p) | ((uint64_t)Le32(p + 4) << 32);
}

struct Ctx
{
    const uint8_t *data;
    uint64_t size;
    const Limits &limits;
    File &out;
    Result result;

    bool Fail(Status status, uint64_t offset, const char *message)
    {
        result.status = status;
        result.errorOffset = offset;
        result.message = message;
        return false;
    }

    void Warn(uint64_t offset, std::string message)
    {
        if (out.warnings.size() == kMaxWarnings)
        {
            out.warnings.push_back({"further warnings suppressed", offset});
            return;
        }
        if (out.warnings.size() < kMaxWarnings)
            out.warnings.push_back({std::move(message), offset});
    }
};

// A list is [u32 count][count items of itemSize bytes] and must fill its region exactly: the
// regions are contiguous in a well-formed file.
bool ListRegion(Ctx &c, const char *name, uint64_t start, uint64_t end, uint32_t itemSize, uint32_t &count)
{
    if (end < start || end - start < 4)
        return c.Fail(Status::Truncated, start, name);
    count = Le32(c.data + start);
    if (count > c.limits.maxListItems)
        return c.Fail(Status::LimitExceeded, start, name);
    uint64_t need = 4 + (uint64_t)count * itemSize;
    if (need > end - start)
        return c.Fail(Status::Truncated, start, name);
    if (need < end - start)
        return c.Fail(Status::OffsetError, start + need, name);
    return true;
}

void CheckReferences(Ctx &c, uint64_t metaStart)
{
    File &f = c.out;
    const uint64_t strings = f.strings.size();
    auto str = [&](uint32_t index, const char *what) {
        if (index >= strings)
            c.Warn(metaStart, std::string(what) + " refers to string " + std::to_string(index) + " but there are " + std::to_string(strings));
    };
    for (const Assembly &a : f.assemblies)
        str(a.nameIndex, "assembly name");
    for (const TypeNamespace &n : f.typeNamespaces)
    {
        str(n.nameIndex, "type namespace name");
        if (n.assemblyIndex >= f.assemblies.size())
            c.Warn(metaStart, "type namespace refers to assembly " + std::to_string(n.assemblyIndex));
    }
    for (const Type &t : f.types)
    {
        str(t.nameIndex, "type name");
        if (t.flags & kTypeIsUnknown)
        {
            if (t.namespaceIndex >= f.xmlNamespaces.size())
                c.Warn(metaStart, "unknown type refers to xml namespace " + std::to_string(t.namespaceIndex));
        }
        else if (t.namespaceIndex >= f.typeNamespaces.size())
            c.Warn(metaStart, "type refers to type namespace " + std::to_string(t.namespaceIndex));
    }
    for (const Property &p : f.properties)
        str(p.nameIndex, "property name");
    for (const XmlNamespace &x : f.xmlNamespaces)
        str(x.uriIndex, "xml namespace uri");
}

}  // namespace

const char *StatusName(Status status)
{
    switch (status)
    {
    case Status::Ok: return "Ok";
    case Status::TooSmall: return "TooSmall";
    case Status::BadMagic: return "BadMagic";
    case Status::UnsupportedVersion: return "UnsupportedVersion";
    case Status::SizeMismatch: return "SizeMismatch";
    case Status::OffsetError: return "OffsetError";
    case Status::Truncated: return "Truncated";
    case Status::LimitExceeded: return "LimitExceeded";
    case Status::BadStreamIndex: return "BadStreamIndex";
    }
    return "?";
}

Result Parse(const uint8_t *data, size_t size, const Limits &limits, File &out)
{
    out = File();
    Ctx c{data, (uint64_t)size, limits, out, Result()};

    if (size > limits.maxFileSize)
    {
        c.Fail(Status::LimitExceeded, 0, "file is larger than the configured limit");
        return c.result;
    }
    if (!data || size < kPreambleBytes + kVersionBytes)
    {
        c.Fail(Status::TooSmall, 0, "smaller than the fixed preamble");
        return c.result;
    }
    if (memcmp(data, "XBF\0", 4) != 0)
    {
        c.Fail(Status::BadMagic, 0, "does not start with XBF");
        return c.result;
    }

    Header &h = out.header;
    h.metadataSize = Le32(data + 4);
    h.nodeStreamSize = Le32(data + 8);
    h.version.major = Le32(data + 12);
    h.version.minor = Le32(data + 16);
    out.version = h.version;

    bool known = (h.version.major == 2 && (h.version.minor == 0 || h.version.minor == 1));
    if (!known)
    {
        c.Fail(Status::UnsupportedVersion, 12,
               h.version.major == 1 ? "XBF version 1 is recognised but not described by this reader"
                                    : "unknown XBF version");
        return c.result;
    }
    out.stringsNullTerminated = (h.version.major == 2 && h.version.minor >= 1);

    uint64_t total = kPreambleBytes + (uint64_t)h.metadataSize + (uint64_t)h.nodeStreamSize;
    if (total != c.size)
    {
        c.Fail(Status::SizeMismatch, 4, "preamble sizes do not add up to the file size");
        return c.result;
    }
    if (h.metadataSize < kFirstListOffset)
    {
        c.Fail(Status::Truncated, 4, "metadata stream is smaller than its header");
        return c.result;
    }

    const uint8_t *hp = data + kPreambleBytes + kVersionBytes;
    uint64_t *offs[6] = {&h.stringTableOffset, &h.assemblyListOffset, &h.typeNamespaceListOffset,
                         &h.typeListOffset,    &h.propertyListOffset, &h.xmlNamespaceListOffset};
    for (int i = 0; i < 6; i++)
        *offs[i] = Le64(hp + 8 * i);
    memcpy(h.hash, hp + 48, sizeof(h.hash));

    // Offsets are relative to the metadata stream (file offset 12); the lists follow each other
    // without gaps, starting right after the header and ending at the metadata size.
    if (h.stringTableOffset != kFirstListOffset)
        return c.Fail(Status::OffsetError, kPreambleBytes + kVersionBytes, "string table does not start after the header"), c.result;
    for (int i = 1; i < 6; i++)
    {
        if (*offs[i] < *offs[i - 1] || *offs[i] > h.metadataSize)
            return c.Fail(Status::OffsetError, kPreambleBytes + kVersionBytes + 8 * i, "list offsets out of order or past the metadata"), c.result;
    }

    const uint64_t base = kPreambleBytes;

    // String table: [u32 count] then per string [u32 length][length UTF-16 units][NUL unit from 2.1].
    {
        uint64_t pos = base + h.stringTableOffset;
        uint64_t end = base + h.assemblyListOffset;
        if (end - pos < 4)
            return c.Fail(Status::Truncated, pos, "string table has no count"), c.result;
        uint32_t count = Le32(data + pos);
        pos += 4;
        if (count > limits.maxStrings)
            return c.Fail(Status::LimitExceeded, pos - 4, "string count exceeds the limit"), c.result;
        out.strings.reserve(count);
        uint64_t term = out.stringsNullTerminated ? 1 : 0;
        for (uint32_t i = 0; i < count; i++)
        {
            if (end - pos < 4)
                return c.Fail(Status::Truncated, pos, "string table ends inside a string header"), c.result;
            uint32_t len = Le32(data + pos);
            pos += 4;
            if (len > limits.maxStringChars)
                return c.Fail(Status::LimitExceeded, pos - 4, "string length exceeds the limit"), c.result;
            uint64_t bytes = ((uint64_t)len + term) * 2;
            if (bytes > end - pos)
                return c.Fail(Status::Truncated, pos, "string runs past the string table"), c.result;
            if (term && (data[pos + (uint64_t)len * 2] | data[pos + (uint64_t)len * 2 + 1]) != 0)
                c.Warn(pos, "string " + std::to_string(i) + " has no terminating NUL");
            StringRef ref;
            ref.fileOffset = (uint32_t)pos;
            ref.length = len;
            out.strings.push_back(ref);
            pos += bytes;
        }
        if (pos != end)
            return c.Fail(Status::OffsetError, pos, "string table does not end where the assembly list starts"), c.result;
    }

    uint32_t count = 0;
    if (!ListRegion(c, "assembly list", base + h.assemblyListOffset, base + h.typeNamespaceListOffset, 8, count))
        return c.result;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = data + base + h.assemblyListOffset + 4 + (uint64_t)i * 8;
        out.assemblies.push_back({Le32(p), Le32(p + 4)});
    }
    if (!ListRegion(c, "type namespace list", base + h.typeNamespaceListOffset, base + h.typeListOffset, 8, count))
        return c.result;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = data + base + h.typeNamespaceListOffset + 4 + (uint64_t)i * 8;
        out.typeNamespaces.push_back({Le32(p), Le32(p + 4)});
    }
    if (!ListRegion(c, "type list", base + h.typeListOffset, base + h.propertyListOffset, 12, count))
        return c.result;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = data + base + h.typeListOffset + 4 + (uint64_t)i * 12;
        out.types.push_back({Le32(p), Le32(p + 4), Le32(p + 8)});
    }
    if (!ListRegion(c, "property list", base + h.propertyListOffset, base + h.xmlNamespaceListOffset, 12, count))
        return c.result;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = data + base + h.propertyListOffset + 4 + (uint64_t)i * 12;
        out.properties.push_back({Le32(p), Le32(p + 4), Le32(p + 8)});
    }
    if (!ListRegion(c, "xml namespace list", base + h.xmlNamespaceListOffset, base + h.metadataSize, 4, count))
        return c.result;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *p = data + base + h.xmlNamespaceListOffset + 4 + (uint64_t)i * 4;
        out.xmlNamespaces.push_back({Le32(p)});
    }
    CheckReferences(c, base);

    // Node stream: an index of sub-streams, then the sub-streams (node data, then line data) which
    // are not decoded here. Offsets in the index are relative to the end of the index table.
    out.nodeStreamFileOffset = base + h.metadataSize;
    if (h.nodeStreamSize < 4)
        return c.Fail(Status::BadStreamIndex, out.nodeStreamFileOffset, "node stream has no index"), c.result;
    uint32_t streams = Le32(data + out.nodeStreamFileOffset);
    if (streams > limits.maxSubStreams)
        return c.Fail(Status::LimitExceeded, out.nodeStreamFileOffset, "sub-stream count exceeds the limit"), c.result;
    uint64_t table = 4 + (uint64_t)streams * 8;
    if (table > h.nodeStreamSize)
        return c.Fail(Status::Truncated, out.nodeStreamFileOffset, "stream index runs past the node stream"), c.result;
    out.streamTableBytes = table;
    uint64_t dataLen = h.nodeStreamSize - table;
    uint64_t previousLineEnd = 0;
    for (uint32_t i = 0; i < streams; i++)
    {
        const uint8_t *p = data + out.nodeStreamFileOffset + 4 + (uint64_t)i * 8;
        SubStream s{Le32(p), Le32(p + 4)};
        uint64_t at = out.nodeStreamFileOffset + 4 + (uint64_t)i * 8;
        if (s.nodeOffset > s.lineOffset || s.lineOffset > dataLen)
            return c.Fail(Status::BadStreamIndex, at, "sub-stream offsets out of order or past the node stream"), c.result;
        if (s.nodeOffset < previousLineEnd)
            return c.Fail(Status::BadStreamIndex, at, "sub-streams overlap"), c.result;
        if (i == 0 && s.nodeOffset != 0)
            c.Warn(at, "first sub-stream does not start at offset 0");
        previousLineEnd = s.lineOffset;
        out.subStreams.push_back(s);
    }
    if (streams == 0)
        c.Warn(out.nodeStreamFileOffset, "no sub-streams");
    return c.result;
}

std::u16string GetString(const uint8_t *data, size_t size, const File &file, uint32_t index)
{
    std::u16string s;
    if (index >= file.strings.size())
        return s;
    const StringRef &r = file.strings[index];
    if ((uint64_t)r.fileOffset + (uint64_t)r.length * 2 > size)
        return s;
    for (uint32_t i = 0; i < r.length; i++)
        s.push_back((char16_t)(data[r.fileOffset + i * 2] | (data[r.fileOffset + i * 2 + 1] << 8)));
    return s;
}

namespace {

void AppendUtf8(std::string &out, const std::u16string &s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        if (cp < 0x20 || cp == 0x7F)
        {
            char b[8];
            snprintf(b, sizeof(b), "\\x%02x", cp);
            out += b;
        }
        else if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800)
        {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
}

std::string Name(const uint8_t *data, size_t size, const File &f, uint32_t index)
{
    if (index >= f.strings.size())
        return "<bad string " + std::to_string(index) + ">";
    std::string s = "\"";
    AppendUtf8(s, GetString(data, size, f, index));
    s += "\"";
    return s;
}

}  // namespace

std::string Dump(const uint8_t *data, size_t size, const File &f)
{
    std::string o;
    char b[256];
    auto line = [&](const char *fmt, auto... a) {
        snprintf(b, sizeof(b), fmt, a...);
        o += b;
        o += "\n";
    };
    line("XBF version %u.%u, %s strings", f.version.major, f.version.minor,
         f.stringsNullTerminated ? "NUL-terminated" : "not NUL-terminated");
    line("metadata stream %u bytes, node stream %u bytes", f.header.metadataSize, f.header.nodeStreamSize);
    line("offsets (relative to file offset 12): strings 0x%llx assemblies 0x%llx type-namespaces 0x%llx types 0x%llx properties 0x%llx xml-namespaces 0x%llx",
         (unsigned long long)f.header.stringTableOffset, (unsigned long long)f.header.assemblyListOffset,
         (unsigned long long)f.header.typeNamespaceListOffset, (unsigned long long)f.header.typeListOffset,
         (unsigned long long)f.header.propertyListOffset, (unsigned long long)f.header.xmlNamespaceListOffset);
    std::string hash;
    bool any = false;
    for (uint8_t v : f.header.hash)
    {
        snprintf(b, sizeof(b), "%02x", v);
        hash += b;
        any = any || v;
    }
    line("hash field: %s", any ? hash.c_str() : "(all zero)");
    line("strings: %zu", f.strings.size());
    for (size_t i = 0; i < f.strings.size(); i++)
        line("  [%zu] %s", i, Name(data, size, f, (uint32_t)i).c_str());
    line("assemblies: %zu", f.assemblies.size());
    for (size_t i = 0; i < f.assemblies.size(); i++)
        line("  [%zu] provider-kind %u name %s", i, f.assemblies[i].providerKind, Name(data, size, f, f.assemblies[i].nameIndex).c_str());
    line("type namespaces: %zu", f.typeNamespaces.size());
    for (size_t i = 0; i < f.typeNamespaces.size(); i++)
        line("  [%zu] assembly %u name %s", i, f.typeNamespaces[i].assemblyIndex, Name(data, size, f, f.typeNamespaces[i].nameIndex).c_str());
    line("types: %zu", f.types.size());
    for (size_t i = 0; i < f.types.size(); i++)
        line("  [%zu] flags 0x%x namespace %u name %s", i, f.types[i].flags, f.types[i].namespaceIndex, Name(data, size, f, f.types[i].nameIndex).c_str());
    line("properties: %zu", f.properties.size());
    for (size_t i = 0; i < f.properties.size(); i++)
        line("  [%zu] flags 0x%x type %u name %s", i, f.properties[i].flags, f.properties[i].typeIndex, Name(data, size, f, f.properties[i].nameIndex).c_str());
    line("xml namespaces: %zu", f.xmlNamespaces.size());
    for (size_t i = 0; i < f.xmlNamespaces.size(); i++)
        line("  [%zu] %s", i, Name(data, size, f, f.xmlNamespaces[i].uriIndex).c_str());
    line("node stream at file offset 0x%llx, index table %llu bytes, %zu sub-streams",
         (unsigned long long)f.nodeStreamFileOffset, (unsigned long long)f.streamTableBytes, f.subStreams.size());
    for (size_t i = 0; i < f.subStreams.size(); i++)
        line("  [%zu] nodes at +%u, lines at +%u", i, f.subStreams[i].nodeOffset, f.subStreams[i].lineOffset);
    for (const Diagnostic &w : f.warnings)
        line("warning at 0x%llx: %s", (unsigned long long)w.fileOffset, w.message.c_str());
    return o;
}

}  // namespace laml::xbf
