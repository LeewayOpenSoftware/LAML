// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Bridge between a parsed XBF (M1 structure reader) and the type catalog: for every type and
// property the XBF names, say whether the catalog knows it. Read-only on both sides; it never
// changes the XBF reader and creates no objects. Unknown names are reported, and are recorded in the
// catalog as Unknown placeholders only when the caller asks for it.
#pragma once

#include <string>
#include <vector>

#include "types/type_catalog.h"
#include "xbf/xbf_reader.h"

namespace laml::types {

enum class Binding : uint8_t
{
    Resolved,      // declared in the catalog
    Unknown,       // not declared (the XBF may also flag it as unknown itself)
    Directive,     // markup directive (x:Key, x:Name ...): not a catalog type or property
    Malformed,     // an index inside the XBF points outside its list or string table
};

struct BoundType
{
    std::string fullName;        // namespace + "." + name, from the XBF strings (UTF-8)
    uint32_t xbfFlags = 0;
    Binding binding = Binding::Unknown;
    TypeId type = kNoType;
};

struct BoundProperty
{
    std::string name;
    std::string ownerFullName;   // empty when the owner index is bad
    uint32_t xbfFlags = 0;
    Binding binding = Binding::Unknown;
    TypeId owner = kNoType;
    TypeId declaring = kNoType;  // type that declares it (owner or a base)
    const Property *property = nullptr;   // pointer into the catalog; valid while the catalog is unchanged
};

struct XbfBinding
{
    std::vector<BoundType> types;            // same order and size as File::types
    std::vector<BoundProperty> properties;   // same order and size as File::properties
    size_t unknownTypes = 0, unknownProperties = 0, malformed = 0;
};

// The catalog must be resolved. Returns false (and an empty binding) if it is not. With recordUnknown the
// names that are not declared are added to the catalog as Unknown placeholders (catalog is non-const then).
bool BindXbf(const uint8_t *data, size_t size, const xbf::File &file, const Catalog &catalog, XbfBinding &out);
bool BindXbfRecording(const uint8_t *data, size_t size, const xbf::File &file, Catalog &catalog, XbfBinding &out);

// UTF-16 (code units) to UTF-8; unpaired surrogates become U+FFFD.
std::string Utf16ToUtf8(const std::u16string &s);

}  // namespace laml::types
