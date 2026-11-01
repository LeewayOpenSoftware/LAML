// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Resource lookup for LAML: ResourceDictionary with merged and theme dictionaries.
//
// This is an independent implementation of the lookup rules documented publicly for XAML
// ResourceDictionary (own entries, then MergedDictionaries last-to-first, then ThemeDictionaries
// for the active theme, then the global theme resources). The MIT-licensed Microsoft.UI.Xaml
// sources were read as a behavioural reference only; no code was copied (see PROVENANCE.md).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace laml::resources {

// A resource value. Object resources (styles, brushes...) arrive with the object model; the
// lookup rules do not depend on what the value is.
using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

// Implicit resources (styles keyed by their target type) live in a separate key space.
struct Key
{
    std::string name;
    bool isType = false;
    bool operator<(const Key &o) const { return isType != o.isType ? isType < o.isType : name < o.name; }
};

enum class Theme : uint8_t { None, Light, Dark };
enum class HighContrast : uint8_t { None, Black, White, Custom };

// What the active theme is. A subtree can request a theme of its own (RequestedTheme).
struct ThemeState
{
    Theme app = Theme::Light;
    std::optional<Theme> subtree;
    HighContrast highContrast = HighContrast::None;
    Theme Effective() const { return subtree ? *subtree : app; }
};

// Which parts of the tree a lookup may visit. Combine with |.
enum Scope : uint32_t
{
    kLocalOnly = 1,
    kMerged = 2,
    kLocalTheme = 4,
    kGlobalTheme = 8,
    kAll = kLocalOnly | kMerged | kLocalTheme | kGlobalTheme,
};

constexpr size_t kMaxMergeDepth = 64;

class ResourceDictionary;
using DictionaryPtr = std::shared_ptr<ResourceDictionary>;

// Resources shared by every dictionary: the system theme dictionaries and Application.Resources.
struct GlobalResources
{
    DictionaryPtr themeDictionaries;      // keyed "Light", "Dark", "HighContrast*", "Default" -> dictionaries
    DictionaryPtr applicationResources;   // overrides values found in themeDictionaries
};

class ResourceDictionary : public std::enable_shared_from_this<ResourceDictionary>
{
public:
    static DictionaryPtr Create() { return DictionaryPtr(new ResourceDictionary()); }

    // Entries. Insert returns false when the key already exists (XAML: duplicate keys are an error).
    bool Insert(const Key &key, Value value);
    bool Remove(const Key &key);
    size_t Count() const { return m_items.size(); }

    // Child dictionaries. AddMerged fails (returns false) if it would create a cycle, if the
    // dictionary is already merged here, or if the nesting exceeds kMaxMergeDepth.
    bool AddMerged(const DictionaryPtr &dict);
    bool RemoveMerged(const DictionaryPtr &dict);
    const std::vector<DictionaryPtr> &Merged() const { return m_merged; }

    // ThemeDictionaries: the name is "Light", "Dark", "HighContrastBlack", "HighContrastWhite",
    // "HighContrastCustom", "HighContrast" or "Default".
    bool SetThemeDictionary(const std::string &name, const DictionaryPtr &dict);

    // Find a resource. `foundIn` (optional) receives the dictionary that held the value.
    std::optional<Value> Lookup(const Key &key, const ThemeState &theme, const GlobalResources *global = nullptr,
                                uint32_t scope = kAll, DictionaryPtr *foundIn = nullptr) const;
    std::optional<Value> Lookup(const std::string &name, const ThemeState &theme,
                                const GlobalResources *global = nullptr) const
    {
        return Lookup(Key{name, false}, theme, global);
    }

    // The theme dictionary that applies to `theme`, or null. Order: the high-contrast variants,
    // then "Light"/"Dark", then "Default".
    DictionaryPtr ActiveThemeDictionary(const ThemeState &theme) const;

private:
    ResourceDictionary() = default;
    bool Reaches(const ResourceDictionary *target, size_t depth) const;
    size_t Depth() const;   // longest merged chain below this dictionary, this one counted
    std::optional<Value> LookupImpl(const Key &key, const ThemeState &theme, const GlobalResources *global,
                                    uint32_t scope, DictionaryPtr *foundIn, size_t depth) const;
    std::optional<Value> FromGlobal(const Key &key, const ThemeState &theme, const GlobalResources &global,
                                    DictionaryPtr *foundIn) const;

    std::map<Key, Value> m_items;
    std::vector<DictionaryPtr> m_merged;
    std::map<std::string, DictionaryPtr> m_themes;
};

}  // namespace laml::resources
