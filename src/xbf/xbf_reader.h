// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML XBF structure reader: parses and validates the container structure of a binary XAML (.xbf)
// file and describes it. It does NOT decode the node stream and does NOT activate anything.
//
// The layout (magic, version words, offset table, string table, assembly / type namespace / type /
// property / xml namespace lists, node-stream index) and the field and flag names come from the
// MIT-licensed Microsoft.UI.Xaml sources listed in PROVENANCE.md. The code is written for LAML:
// every read is bounds-checked, every count is limited, nothing is trusted before it is checked.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace laml::xbf {

enum class Status
{
    Ok,
    TooSmall,            // fewer bytes than the fixed preamble and header
    BadMagic,            // does not start with "XBF\0"
    UnsupportedVersion,  // version recognised or not, but not one this reader describes (see File::version)
    SizeMismatch,        // 12 + metadata size + node stream size is not the file size
    OffsetError,         // an offset in the header is out of range, out of order or not contiguous
    Truncated,           // a table or string runs past the end of its region
    LimitExceeded,       // a count or length is larger than the configured limit
    BadStreamIndex,      // the node-stream index is inconsistent
};

const char *StatusName(Status status);

struct Version
{
    uint32_t major = 0;
    uint32_t minor = 0;
};

struct Limits
{
    uint64_t maxFileSize = 256ull * 1024 * 1024;
    uint32_t maxStrings = 1u << 20;
    uint32_t maxStringChars = 1u << 20;
    uint32_t maxListItems = 1u << 20;
    uint32_t maxSubStreams = 1u << 20;
};

// Header fields, as stored. Offsets are relative to the start of the metadata stream (file offset 12).
struct Header
{
    uint32_t metadataSize = 0;
    uint32_t nodeStreamSize = 0;
    Version version;
    uint64_t stringTableOffset = 0;
    uint64_t assemblyListOffset = 0;
    uint64_t typeNamespaceListOffset = 0;
    uint64_t typeListOffset = 0;
    uint64_t propertyListOffset = 0;
    uint64_t xmlNamespaceListOffset = 0;
    uint8_t hash[64] = {};
};

struct StringRef
{
    uint32_t fileOffset = 0;  // offset of the first UTF-16 code unit in the file
    uint32_t length = 0;      // code units, without the terminator
};

struct Assembly { uint32_t providerKind = 0; uint32_t nameIndex = 0; };
struct TypeNamespace { uint32_t assemblyIndex = 0; uint32_t nameIndex = 0; };
struct Type { uint32_t flags = 0; uint32_t namespaceIndex = 0; uint32_t nameIndex = 0; };
struct Property { uint32_t flags = 0; uint32_t typeIndex = 0; uint32_t nameIndex = 0; };
struct XmlNamespace { uint32_t uriIndex = 0; };
struct SubStream { uint32_t nodeOffset = 0; uint32_t lineOffset = 0; };

// Flag values of PersistedXamlType::m_TypeFlags and PersistedXamlProperty::m_PropertyFlags.
constexpr uint32_t kTypeIsMarkupDirective = 0x01;
constexpr uint32_t kTypeIsUnknown = 0x02;
constexpr uint32_t kPropIsXmlProperty = 0x01;
constexpr uint32_t kPropIsMarkupDirective = 0x02;
constexpr uint32_t kPropIsImplicitProperty = 0x04;
constexpr uint32_t kPropIsCustomDependencyProperty = 0x08;
constexpr uint32_t kPropIsUnknown = 0x10;

struct Diagnostic
{
    std::string message;
    uint64_t fileOffset = 0;
};

struct File
{
    Header header;
    Version version;
    bool stringsNullTerminated = false;  // 2.1 and later
    std::vector<StringRef> strings;
    std::vector<Assembly> assemblies;
    std::vector<TypeNamespace> typeNamespaces;
    std::vector<Type> types;
    std::vector<Property> properties;
    std::vector<XmlNamespace> xmlNamespaces;
    uint64_t nodeStreamFileOffset = 0;   // start of the node stream (the stream index table)
    uint64_t streamTableBytes = 0;       // size of the node-stream index table
    std::vector<SubStream> subStreams;
    std::vector<Diagnostic> warnings;    // things that are odd but do not make the file unreadable
};

struct Result
{
    Status status = Status::Ok;
    std::string message;
    uint64_t errorOffset = 0;
    bool ok() const { return status == Status::Ok; }
};

// Parse and validate. `out` is fully reset first; on failure it holds what was read before the error
// (the version is filled as soon as it is known, including for UnsupportedVersion).
Result Parse(const uint8_t *data, size_t size, const Limits &limits, File &out);
inline Result Parse(const uint8_t *data, size_t size, File &out) { return Parse(data, size, Limits(), out); }

// The string at `index` as UTF-16 code units; empty if the index is out of range.
std::u16string GetString(const uint8_t *data, size_t size, const File &file, uint32_t index);

// Human-readable structural description (UTF-8). Never decodes nodes.
std::string Dump(const uint8_t *data, size_t size, const File &file);

}  // namespace laml::xbf
