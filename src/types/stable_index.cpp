// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "types/stable_index.h"

namespace laml::types {

namespace {

const size_t kMaxRows = 1u << 20;
const size_t kMaxLine = 1024;

Result Fail(Status status, size_t line, const std::string &message)
{
    Result r;
    r.status = status;
    r.line = line;
    r.message = message;
    return r;
}

bool NameChars(const std::string &s)
{
    if (s.empty())
        return false;
    for (unsigned char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

// "Name_Deleted3" -> "Name"; other names unchanged
std::string StripDeleted(const std::string &name)
{
    size_t pos = name.rfind("_Deleted");
    if (pos == std::string::npos || pos == 0)
        return name;
    for (size_t i = pos + 8; i < name.size(); i++)
        if (name[i] < '0' || name[i] > '9')
            return name;
    return name.substr(0, pos);
}

struct Row
{
    std::string original, current;
};

// Split "a,b,index" and check that the index equals the row number. Returns Ok or an error.
Result ParseRows(const char *text, size_t size, std::vector<Row> &rows)
{
    size_t pos = 0, lineNo = 0;
    if (size >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        pos = 3;
    while (pos < size)
    {
        size_t end = pos;
        while (end < size && text[end] != '\n')
            end++;
        std::string line(text + pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        lineNo++;
        if (lineNo > kMaxRows)
            return Fail(Status::LimitExceeded, lineNo, "too many rows");
        if (line.size() > kMaxLine)
            return Fail(Status::LimitExceeded, lineNo, "line too long");
        size_t c1 = line.find(','), c2 = c1 == std::string::npos ? c1 : line.find(',', c1 + 1);
        if (c2 == std::string::npos || line.find(',', c2 + 1) != std::string::npos)
            return Fail(Status::Syntax, lineNo, "expected three comma separated fields");
        Row row;
        row.original = line.substr(0, c1);
        row.current = line.substr(c1 + 1, c2 - c1 - 1);
        std::string index = line.substr(c2 + 1);
        if (!NameChars(row.original) || !NameChars(row.current))
            return Fail(Status::BadName, lineNo, "bad name");
        if (index.empty() || index.size() > 9)
            return Fail(Status::Syntax, lineNo, "bad index");
        size_t value = 0;
        for (char ch : index)
        {
            if (ch < '0' || ch > '9')
                return Fail(Status::Syntax, lineNo, "index is not a decimal number");
            value = value * 10 + static_cast<size_t>(ch - '0');
        }
        if (value != rows.size())
            return Fail(Status::Syntax, lineNo, "index " + index + " does not equal the row number " + std::to_string(rows.size()));
        rows.push_back(std::move(row));
    }
    if (rows.empty())
        return Fail(Status::Syntax, 0, "empty table");
    return Result();
}

}  // namespace

Result StableTable::LoadTypes(const char *text, size_t size)
{
    types_.clear();
    props_.clear();
    typeByName_.clear();
    typeByBase_.clear();
    propByName_.clear();
    std::vector<Row> rows;
    Result r = ParseRows(text, size, rows);
    if (!r.ok())
        return r;
    if (rows[0].current != "UnknownType")
        return Fail(Status::Syntax, 1, "row 0 must be the UnknownType entry");
    for (size_t i = 0; i < rows.size(); i++)
    {
        StableType t;
        t.original = rows[i].original;
        t.originalBase = StripDeleted(t.original);
        t.retired = (rows[i].current == "UnknownType");
        if (i > 0)
            typeByBase_.emplace(t.originalBase, static_cast<uint32_t>(i));
        if (!t.retired)
            t.name = rows[i].current;
        if (!t.retired)
        {
            if (!typeByName_.emplace(t.name, static_cast<uint32_t>(i)).second)
            {
                types_.clear();
                typeByName_.clear();
                typeByBase_.clear();
                return Fail(Status::Duplicate, i + 1, "type '" + t.name + "' appears twice");
            }
        }
        types_.push_back(std::move(t));
    }
    return Result();
}

Result StableTable::LoadProperties(const char *text, size_t size)
{
    props_.clear();
    propByName_.clear();
    if (types_.empty())
        return Fail(Status::UnknownOwner, 0, "load the type table first");
    std::vector<Row> rows;
    Result r = ParseRows(text, size, rows);
    if (!r.ok())
        return r;
    if (rows[0].current != "UnknownType_UnknownProperty")
        return Fail(Status::Syntax, 1, "row 0 must be the UnknownType_UnknownProperty entry");
    for (size_t i = 0; i < rows.size(); i++)
    {
        StableProperty p;
        p.original = rows[i].original;
        p.retired = (rows[i].current == "UnknownType_UnknownProperty");
        if (p.retired && i > 0)
        {
            std::string base = StripDeleted(p.original);
            for (size_t u = base.size(); u-- > 1;)
            {
                if (base[u] != '_' || u + 1 >= base.size())
                    continue;
                if (typeByBase_.count(base.substr(0, u)))
                {
                    p.originalOwner = base.substr(0, u);
                    p.originalMember = base.substr(u + 1);
                    break;
                }
            }
        }
        if (!p.retired)
        {
            p.name = rows[i].current;
            // longest live type name that is a prefix followed by '_'
            for (size_t u = p.name.size(); u-- > 1;)
            {
                if (p.name[u] != '_' || u + 1 >= p.name.size())
                    continue;
                if (typeByName_.count(p.name.substr(0, u)))
                {
                    p.owner = p.name.substr(0, u);
                    p.member = p.name.substr(u + 1);
                    break;
                }
            }
            if (p.owner.empty())
            {
                props_.clear();
                propByName_.clear();
                return Fail(Status::UnknownOwner, i + 1, "property '" + p.name + "' has no known type prefix");
            }
            if (!propByName_.emplace(p.name, static_cast<uint32_t>(i)).second)
            {
                props_.clear();
                propByName_.clear();
                return Fail(Status::Duplicate, i + 1, "property '" + p.name + "' appears twice");
            }
        }
        props_.push_back(std::move(p));
    }
    return Result();
}

long StableTable::FindType(const std::string &name) const
{
    auto it = typeByName_.find(name);
    return it == typeByName_.end() ? -1 : static_cast<long>(it->second);
}

long StableTable::FindProperty(const std::string &owner, const std::string &member) const
{
    auto it = propByName_.find(owner + "_" + member);
    return it == propByName_.end() ? -1 : static_cast<long>(it->second);
}

size_t StableTable::LiveTypes() const
{
    size_t n = 0;
    for (const StableType &t : types_)
        n += t.retired ? 0 : 1;
    return n;
}

size_t StableTable::LiveProperties() const
{
    size_t n = 0;
    for (const StableProperty &p : props_)
        n += p.retired ? 0 : 1;
    return n;
}

Coverage Compare(const StableTable &table, const Catalog &catalog, const std::string &xmlnsUri)
{
    Coverage c;
    std::unordered_map<std::string, TypeId> found;
    for (const StableType &t : table.types())
    {
        if (t.retired)
            continue;
        c.liveTypes++;
        TypeId id = catalog.ResolveXmlName(xmlnsUri, t.name).type;
        if (id != kNoType)
        {
            c.typesFound++;
            found[t.name] = id;
        }
        else
            c.missingTypes.push_back(t.name);
    }
    for (const StableProperty &p : table.properties())
    {
        if (p.retired)
            continue;
        c.liveProperties++;
        auto it = found.find(p.owner);
        bool ok = it != found.end() && (catalog.FindProperty(it->second, p.member) || catalog.FindAttachedProperty(it->second, p.member));
        if (ok)
            c.propertiesFound++;
        else
            c.missingProperties.push_back(p.name);
    }
    return c;
}

}  // namespace laml::types
