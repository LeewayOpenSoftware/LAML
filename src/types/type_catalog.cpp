// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML type catalog. See type_catalog.h. Independent implementation; no WinUI code.
#include "types/type_catalog.h"

#include <algorithm>

namespace laml::types {

const char *KindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Class: return "class";
    case Kind::Struct: return "struct";
    case Kind::Enum: return "enum";
    case Kind::Interface: return "interface";
    case Kind::Primitive: return "primitive";
    case Kind::MarkupDirective: return "directive";
    }
    return "?";
}

const char *StatusName(Status status)
{
    switch (status)
    {
    case Status::Ok: return "Ok";
    case Status::Frozen: return "Frozen";
    case Status::EmptyName: return "EmptyName";
    case Status::BadName: return "BadName";
    case Status::Duplicate: return "Duplicate";
    case Status::UnknownOwner: return "UnknownOwner";
    case Status::LimitExceeded: return "LimitExceeded";
    case Status::BadKind: return "BadKind";
    case Status::Unresolved: return "Unresolved";
    case Status::Syntax: return "Syntax";
    case Status::UnknownSource: return "UnknownSource";
    }
    return "?";
}

static Result Fail(Status status, std::string message)
{
    Result r;
    r.status = status;
    r.message = std::move(message);
    return r;
}

static bool IsIdentStart(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80; }
static bool IsIdentChar(unsigned char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }

Catalog::Catalog(const Limits &limits) : limits_(limits) {}

Result Catalog::CheckName(const std::string &name, bool dotted) const
{
    if (name.empty())
        return Fail(Status::EmptyName, "empty name");
    if (name.size() > limits_.maxNameChars)
        return Fail(Status::LimitExceeded, "name longer than the limit");
    bool segStart = true;
    for (unsigned char c : name)
    {
        if (c == '.' && dotted && !segStart)
        {
            segStart = true;
            continue;
        }
        if (segStart ? !IsIdentStart(c) : !IsIdentChar(c))
            return Fail(Status::BadName, "bad name '" + name + "'");
        segStart = false;
    }
    if (segStart)
        return Fail(Status::BadName, "name ends with a dot: '" + name + "'");
    return Result();
}

static void SplitName(const std::string &full, std::string &ns, std::string &name)
{
    size_t dot = full.rfind('.');
    if (dot == std::string::npos)
    {
        ns.clear();
        name = full;
    }
    else
    {
        ns = full.substr(0, dot);
        name = full.substr(dot + 1);
    }
}

Result Catalog::AddType(const TypeDecl &decl)
{
    if (resolved_)
        return Fail(Status::Frozen, "catalog is resolved");
    Result r = CheckName(decl.fullName, true);
    if (!r.ok())
        return r;
    if (!decl.baseName.empty())
    {
        r = CheckName(decl.baseName, true);
        if (!r.ok())
            return r;
    }
    if (!decl.contentProperty.empty())
    {
        r = CheckName(decl.contentProperty, false);
        if (!r.ok())
            return r;
    }
    if (decl.fullName == decl.baseName)
        return Fail(Status::Unresolved, "type '" + decl.fullName + "' is its own base");
    if ((decl.flags & kTypeUnknown) != 0)
        return Fail(Status::BadKind, "the Unknown flag is set by RecordUnknown only");
    if ((decl.flags & kTypeFlags) != 0 && decl.kind != Kind::Enum)
        return Fail(Status::BadKind, "the flags option is for enums only");
    if (!decl.source.empty() && FindSource(decl.source) == nullptr)
        return Fail(Status::UnknownSource, "type '" + decl.fullName + "' names the undeclared source '" + decl.source + "'");

    TypeInfo *slot = nullptr;
    auto it = byFullName_.find(decl.fullName);
    if (it != byFullName_.end())
    {
        slot = &types_[it->second - 1];
        if (!slot->isUnknown())
            return Fail(Status::Duplicate, "type '" + decl.fullName + "' is already declared");
    }
    else
    {
        if (types_.size() >= limits_.maxTypes)
            return Fail(Status::LimitExceeded, "too many types");
        types_.emplace_back();
        slot = &types_.back();
        slot->id = static_cast<TypeId>(types_.size());
        byFullName_[decl.fullName] = slot->id;
    }
    slot->fullName = decl.fullName;
    SplitName(decl.fullName, slot->ns, slot->name);
    slot->kind = decl.kind;
    slot->flags = decl.flags;
    slot->baseName = decl.baseName;
    slot->base = kNoType;
    slot->contentProperty = decl.contentProperty;
    slot->source = decl.source;
    return Result();
}

Result Catalog::AddSource(const Source &source)
{
    if (resolved_)
        return Fail(Status::Frozen, "catalog is resolved");
    if (source.id.empty() || source.location.empty() || source.revision.empty() || source.license.empty())
        return Fail(Status::EmptyName, "a source needs an id, a location, a revision and a license");
    if (source.id.size() > limits_.maxNameChars || source.location.size() > limits_.maxNameChars ||
        source.revision.size() > limits_.maxNameChars || source.license.size() > limits_.maxNameChars)
        return Fail(Status::LimitExceeded, "source field longer than the limit");
    if (FindSource(source.id) != nullptr)
        return Fail(Status::Duplicate, "source '" + source.id + "' already declared");
    if (sources_.size() >= limits_.maxSources)
        return Fail(Status::LimitExceeded, "too many sources");
    sources_.push_back(source);
    return Result();
}

const Source *Catalog::FindSource(const std::string &id) const
{
    for (const Source &s : sources_)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool Catalog::EnumIsNumbered(TypeId type) const
{
    const TypeInfo *t = Get(type);
    return t && t->kind == Kind::Enum && !t->enumValues.empty() && t->enumNumbers.size() == t->enumValues.size();
}

bool Catalog::EnumAccepts(TypeId type, int64_t number) const
{
    if (!EnumIsNumbered(type))
        return false;
    const TypeInfo *t = Get(type);
    int64_t all = 0;
    for (int64_t n : t->enumNumbers)
    {
        if (n == number)
            return true;
        all |= n;
    }
    // A flags enum takes any combination of its values; the bits must all belong to the declared ones.
    return (t->flags & kTypeFlags) != 0 && number >= 0 && (number & ~all) == 0;
}

const std::string *Catalog::EnumNameOf(TypeId type, int64_t number) const
{
    if (!EnumIsNumbered(type))
        return nullptr;
    const TypeInfo *t = Get(type);
    for (size_t i = 0; i < t->enumNumbers.size(); i++)
        if (t->enumNumbers[i] == number)
            return &t->enumValues[i];
    return nullptr;
}

Result Catalog::AddProperty(const std::string &ownerFullName, const PropertyDecl &decl)
{
    if (resolved_)
        return Fail(Status::Frozen, "catalog is resolved");
    Result r = CheckName(decl.name, false);
    if (!r.ok())
        return r;
    r = CheckName(decl.valueTypeName, true);
    if (!r.ok())
        return r;
    auto it = byFullName_.find(ownerFullName);
    if (it == byFullName_.end() || types_[it->second - 1].isUnknown())
        return Fail(Status::UnknownOwner, "property '" + decl.name + "' on undeclared type '" + ownerFullName + "'");
    TypeInfo &owner = types_[it->second - 1];
    if (owner.kind == Kind::Enum)
        return Fail(Status::BadKind, "enum '" + ownerFullName + "' has values, not properties");
    for (const Property &p : owner.properties)
        if (p.name == decl.name)
            return Fail(Status::Duplicate, "property '" + decl.name + "' already declared on '" + ownerFullName + "'");
    if (owner.properties.size() >= limits_.maxPropertiesPerType)
        return Fail(Status::LimitExceeded, "too many properties on '" + ownerFullName + "'");
    Property p;
    p.name = decl.name;
    p.owner = owner.id;
    p.valueTypeName = decl.valueTypeName;
    p.flags = decl.flags;
    owner.properties.push_back(std::move(p));
    return Result();
}

Result Catalog::AddEnumValue(const std::string &ownerFullName, const std::string &value)
{
    return AddEnumImpl(ownerFullName, value, false, 0);
}

Result Catalog::AddEnumValue(const std::string &ownerFullName, const std::string &value, int64_t number)
{
    return AddEnumImpl(ownerFullName, value, true, number);
}

Result Catalog::AddEnumImpl(const std::string &ownerFullName, const std::string &value, bool numbered, int64_t number)
{
    if (resolved_)
        return Fail(Status::Frozen, "catalog is resolved");
    Result r = CheckName(value, false);
    if (!r.ok())
        return r;
    auto it = byFullName_.find(ownerFullName);
    if (it == byFullName_.end() || types_[it->second - 1].isUnknown())
        return Fail(Status::UnknownOwner, "enum value on undeclared type '" + ownerFullName + "'");
    TypeInfo &owner = types_[it->second - 1];
    if (owner.kind != Kind::Enum)
        return Fail(Status::BadKind, "'" + ownerFullName + "' is not an enum");
    if (std::find(owner.enumValues.begin(), owner.enumValues.end(), value) != owner.enumValues.end())
        return Fail(Status::Duplicate, "enum value '" + value + "' already declared on '" + ownerFullName + "'");
    if (owner.enumValues.size() >= limits_.maxEnumValuesPerType)
        return Fail(Status::LimitExceeded, "too many enum values on '" + ownerFullName + "'");
    // an enum is numbered from its first value on or not at all
    if (numbered && !owner.enumValues.empty() && owner.enumNumbers.empty())
        return Fail(Status::BadKind, "enum '" + ownerFullName + "' has unnumbered values; '" + value + "' cannot have a number");
    if (!numbered && !owner.enumNumbers.empty())
        return Fail(Status::BadKind, "enum '" + ownerFullName + "' has numbered values; '" + value + "' needs a number");
    owner.enumValues.push_back(value);
    if (numbered)
        owner.enumNumbers.push_back(number);
    return Result();
}

Result Catalog::MapXmlNamespace(const std::string &uri, const std::string &typeNamespace)
{
    if (resolved_)
        return Fail(Status::Frozen, "catalog is resolved");
    if (uri.empty())
        return Fail(Status::EmptyName, "empty xmlns uri");
    if (uri.size() > limits_.maxNameChars)
        return Fail(Status::LimitExceeded, "xmlns uri longer than the limit");
    Result r = CheckName(typeNamespace, true);
    if (!r.ok())
        return r;
    size_t total = 0;
    for (const auto &e : xmlMap_)
        total += e.second.size();
    if (total >= limits_.maxXmlMappings)
        return Fail(Status::LimitExceeded, "too many xmlns mappings");
    auto &list = xmlMap_[uri];
    if (std::find(list.begin(), list.end(), typeNamespace) != list.end())
        return Fail(Status::Duplicate, "xmlns '" + uri + "' already maps '" + typeNamespace + "'");
    list.push_back(typeNamespace);
    return Result();
}

const TypeInfo *Catalog::Get(TypeId id) const
{
    if (id == kNoType || id > types_.size())
        return nullptr;
    return &types_[id - 1];
}

const TypeInfo *Catalog::FindIncludingUnknown(const std::string &fullName) const
{
    auto it = byFullName_.find(fullName);
    return it == byFullName_.end() ? nullptr : &types_[it->second - 1];
}

const TypeInfo *Catalog::Find(const std::string &fullName) const
{
    const TypeInfo *t = FindIncludingUnknown(fullName);
    return (t && !t->isUnknown()) ? t : nullptr;
}

const TypeInfo *Catalog::Find(const std::string &typeNamespace, const std::string &name) const
{
    return Find(typeNamespace.empty() ? name : typeNamespace + "." + name);
}

TypeId Catalog::RecordUnknown(const std::string &fullName)
{
    if (!CheckName(fullName, true).ok())
        return kNoType;
    auto it = byFullName_.find(fullName);
    if (it != byFullName_.end())
        return it->second;
    if (types_.size() >= limits_.maxTypes)
        return kNoType;
    types_.emplace_back();
    TypeInfo &t = types_.back();
    t.id = static_cast<TypeId>(types_.size());
    t.fullName = fullName;
    SplitName(fullName, t.ns, t.name);
    t.flags = kTypeUnknown;
    byFullName_[fullName] = t.id;
    return t.id;
}

XmlResolution Catalog::ResolveXmlName(const std::string &uri, const std::string &localName) const
{
    XmlResolution out;
    if (!CheckName(localName, false).ok())
        return out;
    auto it = xmlMap_.find(uri);
    if (it == xmlMap_.end())
    {
        out.tried = "(xmlns not mapped)";
        return out;
    }
    for (const std::string &ns : it->second)
    {
        std::string full = ns + "." + localName;
        if (!out.tried.empty())
            out.tried += ", ";
        out.tried += full;
        if (const TypeInfo *t = Find(full))
        {
            out.type = t->id;
            return out;
        }
    }
    return out;
}

std::vector<Problem> Catalog::Resolve()
{
    std::vector<Problem> errors;
    warnings_.clear();
    resolved_ = false;

    for (TypeInfo &t : types_)
    {
        t.base = kNoType;
        if (t.isUnknown() || t.baseName.empty())
            continue;
        const TypeInfo *b = Find(t.baseName);
        if (!b)
            errors.push_back({Status::Unresolved, "base '" + t.baseName + "' of '" + t.fullName + "' is not declared"});
        else
            t.base = b->id;
    }

    // Cycles and depth: a chain longer than the limit is either a cycle or too deep to accept.
    for (const TypeInfo &t : types_)
    {
        uint32_t depth = 0;
        TypeId cur = t.base;
        while (cur != kNoType && depth <= limits_.maxHierarchyDepth)
        {
            cur = types_[cur - 1].base;
            depth++;
        }
        if (depth > limits_.maxHierarchyDepth)
            errors.push_back({Status::Unresolved, "inheritance cycle or chain deeper than the limit at '" + t.fullName + "'"});
    }
    if (!errors.empty())
        return errors;

    // resolved_ must be true for the chain-walking queries used below.
    resolved_ = true;
    for (TypeInfo &t : types_)
    {
        if (t.isUnknown())
            continue;
        if (!t.contentProperty.empty() && !FindProperty(t.id, t.contentProperty))
            errors.push_back({Status::BadKind, "content property '" + t.contentProperty + "' of '" + t.fullName + "' is not an instance property of the type or its bases"});
        for (Property &p : t.properties)
        {
            const TypeInfo *vt = Find(p.valueTypeName);
            if (vt)
                p.valueType = vt->id;
            else
            {
                p.valueType = kNoType;
                warnings_.push_back({Status::Unresolved, "value type '" + p.valueTypeName + "' of '" + t.fullName + "." + p.name + "' is not declared"});
            }
        }
    }
    if (!errors.empty())
        resolved_ = false;
    return errors;
}

bool Catalog::IsAssignable(TypeId derived, TypeId base) const
{
    return HierarchyDistance(derived, base) >= 0;
}

int Catalog::HierarchyDistance(TypeId derived, TypeId base) const
{
    if (!resolved_ || !Get(derived) || !Get(base))
        return -1;
    int d = 0;
    TypeId cur = derived;
    while (cur != kNoType)
    {
        if (cur == base)
            return d;
        cur = types_[cur - 1].base;
        d++;
    }
    return -1;
}

const Property *Catalog::FindProperty(TypeId type, const std::string &name, TypeId *declaring) const
{
    if (!resolved_ || !Get(type))
        return nullptr;
    for (TypeId cur = type; cur != kNoType; cur = types_[cur - 1].base)
        for (const Property &p : types_[cur - 1].properties)
            if (p.name == name && (p.flags & kPropAttached) == 0)
            {
                if (declaring)
                    *declaring = cur;
                return &p;
            }
    return nullptr;
}

const Property *Catalog::FindAttachedProperty(TypeId owner, const std::string &name) const
{
    if (!resolved_ || !Get(owner))
        return nullptr;
    for (TypeId cur = owner; cur != kNoType; cur = types_[cur - 1].base)
        for (const Property &p : types_[cur - 1].properties)
            if (p.name == name && (p.flags & kPropAttached) != 0)
                return &p;
    return nullptr;
}

std::vector<const Property *> Catalog::AllProperties(TypeId type) const
{
    std::vector<const Property *> out;
    if (!resolved_ || !Get(type))
        return out;
    std::vector<TypeId> chain;
    for (TypeId cur = type; cur != kNoType; cur = types_[cur - 1].base)
        chain.push_back(cur);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        for (const Property &p : types_[*it - 1].properties)
        {
            if (p.flags & kPropAttached)
                continue;
            auto same = std::find_if(out.begin(), out.end(), [&](const Property *q) { return q->name == p.name; });
            if (same != out.end())
                *same = &p;
            else
                out.push_back(&p);
        }
    return out;
}

const Property *Catalog::ContentProperty(TypeId type) const
{
    if (!resolved_ || !Get(type))
        return nullptr;
    for (TypeId cur = type; cur != kNoType; cur = types_[cur - 1].base)
        if (!types_[cur - 1].contentProperty.empty())
            return FindProperty(cur, types_[cur - 1].contentProperty);
    return nullptr;
}

}  // namespace laml::types
