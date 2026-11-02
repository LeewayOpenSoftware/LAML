// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML type metadata, plain-text loader. The format is described in type_catalog.h.
#include "types/type_catalog.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace laml::types {

namespace {

// Decimal or 0x-hex, optional leading '-', the whole text must be consumed and fit in int64_t.
bool ParseNumber(const std::string &s, int64_t &out)
{
    if (s.empty())
        return false;
    size_t i = 0;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+')
    {
        neg = s[0] == '-';
        i = 1;
    }
    int base = 10;
    if (s.size() > i + 2 && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'))
    {
        base = 16;
        i += 2;
    }
    if (i >= s.size() || s[i] == '-' || s[i] == '+' || s[i] == ' ')
        return false;
    errno = 0;
    char *end = nullptr;
    unsigned long long v = std::strtoull(s.c_str() + i, &end, base);
    if (errno != 0 || *end != '\0')
        return false;
    if (neg ? v > 0x8000000000000000ull : v > 0x7fffffffffffffffull)
        return false;
    out = neg ? static_cast<int64_t>(0 - v) : static_cast<int64_t>(v);
    return true;
}

Result LineError(size_t line, Status status, const std::string &message)
{
    Result r;
    r.status = status;
    r.message = message;
    r.line = line;
    return r;
}

std::vector<std::string> Tokens(const std::string &line)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i >= line.size())
            break;
        if (line[i] == '#')   // comment runs to the end of the line, only at the start of a token
            break;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t')
            j++;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

bool StartsWith(const std::string &s, const char *prefix)
{
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

Result Apply(Catalog &catalog, const std::vector<std::string> &t, size_t line)
{
    Result r;
    if (t[0] == "xmlns")
    {
        if (t.size() != 3)
            return LineError(line, Status::Syntax, "xmlns needs <uri> <type-namespace>");
        r = catalog.MapXmlNamespace(t[1], t[2]);
    }
    else if (t[0] == "source")
    {
        if (t.size() != 5)
            return LineError(line, Status::Syntax, "source needs <id> <location> <revision> <license>");
        Source s;
        s.id = t[1];
        s.location = t[2];
        s.revision = t[3];
        s.license = t[4];
        r = catalog.AddSource(s);
    }
    else if (t[0] == "type")
    {
        if (t.size() < 2)
            return LineError(line, Status::Syntax, "type needs a full name");
        TypeDecl d;
        d.fullName = t[1];
        bool haveKind = false, haveBase = false, haveContent = false;
        for (size_t i = 2; i < t.size(); i++)
        {
            const std::string &o = t[i];
            if (o == "abstract") d.flags |= kTypeAbstract;
            else if (o == "sealed") d.flags |= kTypeSealed;
            else if (o == "flags") d.flags |= kTypeFlags;
            else if (StartsWith(o, "src="))
            {
                if (!d.source.empty())
                    return LineError(line, Status::Syntax, "src given twice");
                d.source = o.substr(4);
                if (d.source.empty())
                    return LineError(line, Status::Syntax, "src needs a source id");
            }
            else if (StartsWith(o, "kind="))
            {
                if (haveKind)
                    return LineError(line, Status::Syntax, "kind given twice");
                haveKind = true;
                std::string v = o.substr(5);
                if (v == "class") d.kind = Kind::Class;
                else if (v == "struct") d.kind = Kind::Struct;
                else if (v == "enum") d.kind = Kind::Enum;
                else if (v == "interface") d.kind = Kind::Interface;
                else if (v == "primitive") d.kind = Kind::Primitive;
                else if (v == "directive") d.kind = Kind::MarkupDirective;
                else return LineError(line, Status::Syntax, "unknown kind '" + v + "'");
            }
            else if (StartsWith(o, "base="))
            {
                if (haveBase)
                    return LineError(line, Status::Syntax, "base given twice");
                haveBase = true;
                d.baseName = o.substr(5);
            }
            else if (StartsWith(o, "content="))
            {
                if (haveContent)
                    return LineError(line, Status::Syntax, "content given twice");
                haveContent = true;
                d.contentProperty = o.substr(8);
            }
            else
                return LineError(line, Status::Syntax, "unknown type option '" + o + "'");
        }
        r = catalog.AddType(d);
    }
    else if (t[0] == "prop")
    {
        if (t.size() < 3)
            return LineError(line, Status::Syntax, "prop needs <Owner>.<Name> <ValueType>");
        size_t dot = t[1].rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == t[1].size())
            return LineError(line, Status::Syntax, "prop owner and name must be written Owner.Name");
        PropertyDecl d;
        d.name = t[1].substr(dot + 1);
        d.valueTypeName = t[2];
        for (size_t i = 3; i < t.size(); i++)
        {
            if (t[i] == "attached") d.flags |= kPropAttached;
            else if (t[i] == "readonly") d.flags |= kPropReadOnly;
            else if (t[i] == "dependency") d.flags |= kPropDependency;
            else if (t[i] == "nullable") d.flags |= kPropNullable;
            else return LineError(line, Status::Syntax, "unknown prop option '" + t[i] + "'");
        }
        r = catalog.AddProperty(t[1].substr(0, dot), d);
    }
    else if (t[0] == "enumval")
    {
        if (t.size() != 3)
            return LineError(line, Status::Syntax, "enumval needs <Owner> <Value>");
        size_t eq = t[2].find('=');
        if (eq == std::string::npos)
            r = catalog.AddEnumValue(t[1], t[2]);
        else
        {
            int64_t number = 0;
            if (!ParseNumber(t[2].substr(eq + 1), number))
                return LineError(line, Status::Syntax, "enum value number is not a decimal or 0x-hex integer: '" + t[2] + "'");
            r = catalog.AddEnumValue(t[1], t[2].substr(0, eq), number);
        }
    }
    else
        return LineError(line, Status::Syntax, "unknown directive '" + t[0] + "'");

    r.line = line;
    return r;
}

}  // namespace

Result LoadText(Catalog &catalog, const char *text, size_t size)
{
    const Limits &lim = catalog.limits();
    size_t pos = 0, lineNo = 0;
    bool header = false;

    if (size >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        pos = 3;

    while (pos < size)
    {
        size_t end = pos;
        while (end < size && text[end] != '\n')
            end++;
        lineNo++;
        if (lineNo > lim.maxTextLines)
            return LineError(lineNo, Status::LimitExceeded, "too many lines");
        if (end - pos > lim.maxTextLineChars)
            return LineError(lineNo, Status::LimitExceeded, "line longer than the limit");
        std::string line(text + pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.find('\0') != std::string::npos)
            return LineError(lineNo, Status::Syntax, "NUL byte in the text");

        std::vector<std::string> t = Tokens(line);
        if (t.empty())
            continue;
        if (!header)
        {
            if (t.size() != 2 || t[0] != "laml-types" || t[1] != "1")
                return LineError(lineNo, Status::Syntax, "first line must be 'laml-types 1'");
            header = true;
            continue;
        }
        Result r = Apply(catalog, t, lineNo);
        if (!r.ok())
            return r;
    }
    if (!header)
        return LineError(0, Status::Syntax, "empty metadata: no 'laml-types 1' line");
    return Result();
}

}  // namespace laml::types
