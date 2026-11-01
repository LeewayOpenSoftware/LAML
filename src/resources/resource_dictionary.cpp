// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "resource_dictionary.h"

#include <algorithm>

namespace laml::resources {

bool ResourceDictionary::Insert(const Key &key, Value value)
{
    return m_items.emplace(key, std::move(value)).second;
}

bool ResourceDictionary::Remove(const Key &key)
{
    return m_items.erase(key) != 0;
}

// True when `target` is this dictionary or reachable through merged/theme children.
bool ResourceDictionary::Reaches(const ResourceDictionary *target, size_t depth) const
{
    if (this == target)
        return true;
    if (depth > kMaxMergeDepth * 2)
        return true;   // treat runaway nesting as a cycle
    for (const auto &m : m_merged)
        if (m->Reaches(target, depth + 1))
            return true;
    for (const auto &t : m_themes)
        if (t.second->Reaches(target, depth + 1))
            return true;
    return false;
}

size_t ResourceDictionary::Depth() const
{
    size_t d = 0;
    for (const auto &m : m_merged)
        d = std::max(d, m->Depth());
    return d + 1;
}

bool ResourceDictionary::AddMerged(const DictionaryPtr &dict)
{
    if (!dict)
        return false;
    if (std::find(m_merged.begin(), m_merged.end(), dict) != m_merged.end())
        return false;
    if (dict->Reaches(this, 0))
        return false;   // would make this dictionary reachable from itself
    if (dict->Depth() + 1 > kMaxMergeDepth)
        return false;
    m_merged.push_back(dict);
    return true;
}

bool ResourceDictionary::RemoveMerged(const DictionaryPtr &dict)
{
    auto it = std::find(m_merged.begin(), m_merged.end(), dict);
    if (it == m_merged.end())
        return false;
    m_merged.erase(it);
    return true;
}

bool ResourceDictionary::SetThemeDictionary(const std::string &name, const DictionaryPtr &dict)
{
    if (!dict || name.empty() || dict->Reaches(this, 0))
        return false;
    m_themes[name] = dict;
    return true;
}

DictionaryPtr ResourceDictionary::ActiveThemeDictionary(const ThemeState &theme) const
{
    auto get = [this](const char *n) -> DictionaryPtr {
        auto it = m_themes.find(n);
        return it == m_themes.end() ? nullptr : it->second;
    };
    if (theme.highContrast != HighContrast::None)
    {
        DictionaryPtr d;
        if (theme.subtree)
            d = get(*theme.subtree == Theme::Light ? "HighContrastWhite" : "HighContrastBlack");
        else
            d = get(theme.highContrast == HighContrast::Black   ? "HighContrastBlack"
                    : theme.highContrast == HighContrast::White ? "HighContrastWhite"
                                                                : "HighContrastCustom");
        if (!d)
            d = get("HighContrast");
        if (d)
            return d;
    }
    DictionaryPtr d;
    switch (theme.Effective())
    {
    case Theme::Light: d = get("Light"); break;
    case Theme::Dark: d = get("Dark"); break;
    case Theme::None: break;
    }
    return d ? d : get("Default");
}

std::optional<Value> ResourceDictionary::FromGlobal(const Key &key, const ThemeState &theme,
                                                    const GlobalResources &global, DictionaryPtr *foundIn) const
{
    if (!global.themeDictionaries)
        return std::nullopt;
    DictionaryPtr active = global.themeDictionaries->ActiveThemeDictionary(theme);
    if (!active)
        return std::nullopt;
    DictionaryPtr where;
    auto v = active->LookupImpl(key, theme, nullptr, kLocalOnly | kMerged, &where, 0);
    if (!v)
        return std::nullopt;
    // Application.Resources may override a value found in the global theme dictionaries.
    if (global.applicationResources)
    {
        DictionaryPtr appWhere;
        auto o = global.applicationResources->LookupImpl(key, theme, nullptr, kLocalOnly | kMerged, &appWhere, 0);
        if (o)
        {
            if (foundIn)
                *foundIn = appWhere;
            return o;
        }
    }
    if (foundIn)
        *foundIn = where;
    return v;
}

std::optional<Value> ResourceDictionary::LookupImpl(const Key &key, const ThemeState &theme,
                                                    const GlobalResources *global, uint32_t scope,
                                                    DictionaryPtr *foundIn, size_t depth) const
{
    if (depth > kMaxMergeDepth)
        return std::nullopt;

    auto it = m_items.find(key);
    if (it != m_items.end())
    {
        if (foundIn)
            *foundIn = const_cast<ResourceDictionary *>(this)->shared_from_this();
        return it->second;
    }

    if (scope & kMerged)
    {
        // later merged dictionaries win; only the first one searched consults the global theme
        for (size_t i = m_merged.size(); i-- > 0;)
        {
            auto v = m_merged[i]->LookupImpl(key, theme, global, scope, foundIn, depth + 1);
            if (v)
                return v;
            scope &= ~static_cast<uint32_t>(kGlobalTheme);
        }
    }

    if (scope & kLocalTheme)
    {
        if (DictionaryPtr active = ActiveThemeDictionary(theme))
        {
            auto v = active->LookupImpl(key, theme, nullptr, kLocalOnly | kMerged, foundIn, depth + 1);
            if (v)
                return v;
        }
    }

    if ((scope & kGlobalTheme) && global)
        return FromGlobal(key, theme, *global, foundIn);

    return std::nullopt;
}

std::optional<Value> ResourceDictionary::Lookup(const Key &key, const ThemeState &theme, const GlobalResources *global,
                                                uint32_t scope, DictionaryPtr *foundIn) const
{
    if (foundIn)
        foundIn->reset();
    return LookupImpl(key, theme, global, scope, foundIn, 0);
}

}  // namespace laml::resources
