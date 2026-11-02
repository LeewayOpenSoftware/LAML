// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Optional import of the WinUI "stable XBF index" tables (MIT, Microsoft.UI.Xaml): two CSV files that
// number the well-known types and properties an XBF node stream refers to by index instead of by
// name (see PROVENANCE.md). They are DATA the caller may load from a file; LAML embeds none of it and
// nothing else in LAML needs it. The tables give names only: no namespace, no base type, no value type,
// so on their own they cannot make a catalog. They tell which well-known names exist and what number
// an XBF uses for them; `Compare` reports how much of that a LAML catalog covers.
//
// File format (one row per line, no header):  <original name>,<current name>,<index>
//   * the index equals the zero-based line number; row 0 is the "Unknown" entry
//   * a retired entry keeps its slot: its current name is UnknownType (types) or
//     UnknownType_UnknownProperty (properties) while the original name carries a _DeletedN suffix
//   * a property name is <Type>_<Member>; the type is the longest live type name that is a prefix
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "types/type_catalog.h"

namespace laml::types {

struct StableType
{
    std::string name;       // current name; empty for a retired slot
    std::string original;   // name in the first column
    std::string originalBase;   // `original` without the _Deleted<N> suffix (the name the slot had before it was retired)
    bool retired = false;
};

struct StableProperty
{
    std::string name;       // current full name "<Type>_<Member>"; empty for a retired slot
    std::string original;
    std::string owner;      // type part
    std::string member;     // member part
    // For a retired slot: the owner and member its original name had, when the original name can be split
    // against the type names (live or retired) of the table; empty otherwise.
    std::string originalOwner;
    std::string originalMember;
    bool retired = false;
};

class StableTable
{
public:
    // Load the type file first; the property file is split against it. Each call replaces what was
    // loaded before. On failure the table part is left empty.
    Result LoadTypes(const char *text, size_t size);
    Result LoadProperties(const char *text, size_t size);

    size_t TypeSlots() const { return types_.size(); }
    size_t PropertySlots() const { return props_.size(); }
    // nullptr when out of range. Retired slots are returned (retired == true).
    const StableType *Type(uint32_t index) const { return index < types_.size() ? &types_[index] : nullptr; }
    const StableProperty *Property(uint32_t index) const { return index < props_.size() ? &props_[index] : nullptr; }
    // Index of a live entry by current name; -1 if there is none.
    long FindType(const std::string &name) const;
    long FindProperty(const std::string &owner, const std::string &member) const;
    size_t LiveTypes() const;
    size_t LiveProperties() const;

    const std::vector<StableType> &types() const { return types_; }
    const std::vector<StableProperty> &properties() const { return props_; }

private:
    std::vector<StableType> types_;
    std::vector<StableProperty> props_;
    std::unordered_map<std::string, uint32_t> typeByName_;
    std::unordered_map<std::string, uint32_t> typeByBase_;     // original name without _Deleted<N>, live or retired
    std::unordered_map<std::string, uint32_t> propByName_;
};

struct Coverage
{
    size_t liveTypes = 0, typesFound = 0;
    size_t liveProperties = 0, propertiesFound = 0;
    std::vector<std::string> missingTypes;        // stable names the catalog cannot resolve
    std::vector<std::string> missingProperties;   // "<Type>_<Member>" the catalog cannot resolve
};

// How much of the stable table a resolved catalog covers. Stable type names are short names: they are
// looked up through `xmlnsUri` (typically the XAML presentation namespace), so the catalog needs its
// xmlns mappings. A property counts as found when its owner type is found and the owner (or a base) has
// an instance or attached property of that name.
Coverage Compare(const StableTable &table, const Catalog &catalog, const std::string &xmlnsUri);

}  // namespace laml::types
