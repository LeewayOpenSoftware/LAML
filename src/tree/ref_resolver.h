// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML reference resolver (M3): turns the type and property references of a decoded XBF node stream
// into catalog entries, and says precisely why a reference could not be resolved. It looks at metadata
// only: the type catalog (M2), the file's own tables and, for "trusted" references, the optional stable
// index tables. It creates no objects.
//
// A stable slot that the pinned table lists as retired is not an error by itself: the table does not say which
// XAML compiler wrote a file, so an older file may still use a slot that was retired later. The name the slot had
// before it was retired is then used (and `retiredSlot` is set); if that name cannot be recovered the reference
// is InvalidReference.
//
// Four outcomes are kept apart on purpose, because they mean different things to the caller:
//   InvalidReference  the reference points outside a table (or at a retired slot with no recoverable name): the file is wrong
//   UnknownType /
//   UnknownProperty   the reference is valid but the catalog does not describe it: the catalog is incomplete
//   Unimplemented     LAML does not support what is needed (a trusted reference without a stable table, an
//                     unsupported format version, a catalog that was not resolved, a feature not modelled)
//   Directive         a markup directive (x:Name, x:Key, ...), which is not a catalog type or property
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types/stable_index.h"
#include "types/type_catalog.h"
#include "types/xbf_bridge.h"
#include "xbf/xbf_nodes.h"
#include "xbf/xbf_reader.h"

namespace laml::tree {

enum class RefStatus : uint8_t
{
    Absent,             // the node has no such operand
    Resolved,
    InvalidReference,
    UnknownType,
    UnknownProperty,
    Directive,
    Unimplemented,
};
const char *RefStatusName(RefStatus status);

struct TypeResolution
{
    RefStatus status = RefStatus::Absent;
    types::TypeId type = types::kNoType;
    std::string name;       // best known full name ("" when the reference is invalid)
    std::string detail;     // why, when not Resolved
    bool stable = false;    // the reference was a trusted (stable-index) one
    bool retiredSlot = false;   // the stable slot is retired in the table; the name is the slot's original one
    uint16_t id = 0;        // the reference id as stored
};

struct PropertyResolution
{
    RefStatus status = RefStatus::Absent;
    types::TypeId owner = types::kNoType;       // owner type, when known
    types::TypeId declaring = types::kNoType;   // type that declares it (owner or a base)
    std::string ownerName;
    std::string name;
    std::string detail;
    bool attached = false;
    bool stable = false;
    bool retiredSlot = false;
    uint16_t id = 0;
};

struct ResolverOptions
{
    // Stable type names are short names; they are looked up through this xmlns URI of the catalog.
    std::string stableXmlns = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
};

class RefResolver
{
public:
    // `stable` may be null: trusted references are then Unimplemented. Everything passed in must outlive the
    // resolver. The catalog must be resolved; if it is not, every reference is Unimplemented.
    RefResolver(const uint8_t *data, size_t size, const xbf::File &file, const types::Catalog &catalog,
                const types::StableTable *stable, const ResolverOptions &options = ResolverOptions());

    TypeResolution ResolveType(const xbf::Ref &ref) const;
    PropertyResolution ResolveProperty(const xbf::Ref &ref) const;

    // True when trusted references can be resolved: format 2.x, a loaded stable table, a resolved catalog.
    bool StableSupported() const { return stableSupported_; }
    const std::string &StableUnsupportedReason() const { return stableReason_; }

    // The file's xml namespace entry as its URI (UTF-8); empty if out of range.
    std::string XmlNamespaceUri(uint32_t index) const;

private:
    const uint8_t *data_;
    size_t size_;
    const xbf::File &file_;
    const types::Catalog &catalog_;
    const types::StableTable *stable_;
    ResolverOptions options_;
    types::XbfBinding binding_;
    bool bound_ = false;
    bool stableSupported_ = false;
    std::string stableReason_;
};

}  // namespace laml::tree
