// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "types/xbf_bridge.h"

namespace laml::types {

std::string Utf16ToUtf8(const std::u16string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
        {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        }
        else if (c >= 0xD800 && c <= 0xDFFF)
            c = 0xFFFD;
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

namespace {

// Names that cannot go through the catalog (empty, not an identifier) end as Malformed or Unknown;
// the catalog itself validates names, so a bad one simply does not resolve.
bool BindImpl(const uint8_t *data, size_t size, const xbf::File &file, const Catalog &catalog, Catalog *record,
              XbfBinding &out)
{
    out = XbfBinding();
    if (!catalog.IsResolved())
        return false;

    out.types.resize(file.types.size());
    for (size_t i = 0; i < file.types.size(); i++)
    {
        const xbf::Type &xt = file.types[i];
        BoundType &bt = out.types[i];
        bt.xbfFlags = xt.flags;
        // Observed in the MIT fixtures: a type flagged Unknown (0x2) has a namespace index that points into
        // the XML namespace list (CustomTypesPage.xbf: namespace 5 of a file with no type namespaces, and
        // xmlns 5 is "using:Microsoft.ContactSupport.Views"); a known type indexes the type namespace
        // list (SampleXbf2.xbf). The Unknown case is read that way here; see docs/TYPES.md.
        const bool viaXml = (xt.flags & xbf::kTypeIsUnknown) != 0;
        uint32_t nsString = 0;
        bool nsOk;
        if (viaXml)
        {
            nsOk = xt.namespaceIndex < file.xmlNamespaces.size();
            if (nsOk)
                nsString = file.xmlNamespaces[xt.namespaceIndex].uriIndex;
        }
        else
        {
            nsOk = xt.namespaceIndex < file.typeNamespaces.size();
            if (nsOk)
                nsString = file.typeNamespaces[xt.namespaceIndex].nameIndex;
        }
        if (!nsOk || nsString >= file.strings.size() || xt.nameIndex >= file.strings.size())
        {
            bt.binding = Binding::Malformed;
            out.malformed++;
            continue;
        }
        std::string ns = Utf16ToUtf8(xbf::GetString(data, size, file, nsString));
        std::string name = Utf16ToUtf8(xbf::GetString(data, size, file, xt.nameIndex));
        std::string xmlUri;
        if (viaXml)
        {
            static const std::string kUsing = "using:";
            if (ns.compare(0, kUsing.size(), kUsing) == 0)
                ns = ns.substr(kUsing.size());
            else
            {
                xmlUri = ns;    // a real xmlns URI: resolve through the catalog's xmlns mappings
                ns.clear();
            }
        }
        bt.fullName = ns.empty() ? name : ns + "." + name;
        if (xt.flags & xbf::kTypeIsMarkupDirective)
        {
            bt.binding = Binding::Directive;
            continue;
        }
        const TypeInfo *t = xmlUri.empty() ? catalog.Find(bt.fullName) : catalog.Get(catalog.ResolveXmlName(xmlUri, name).type);
        if (t)
        {
            bt.binding = Binding::Resolved;
            bt.type = t->id;
        }
        else
        {
            bt.binding = Binding::Unknown;
            out.unknownTypes++;
            if (record && xmlUri.empty())
                record->RecordUnknown(bt.fullName);
        }
    }

    out.properties.resize(file.properties.size());
    for (size_t i = 0; i < file.properties.size(); i++)
    {
        const xbf::Property &xp = file.properties[i];
        BoundProperty &bp = out.properties[i];
        bp.xbfFlags = xp.flags;
        if (xp.nameIndex >= file.strings.size())
        {
            bp.binding = Binding::Malformed;
            out.malformed++;
            continue;
        }
        bp.name = Utf16ToUtf8(xbf::GetString(data, size, file, xp.nameIndex));
        if (xp.flags & (xbf::kPropIsMarkupDirective | xbf::kPropIsXmlProperty))
        {
            bp.binding = Binding::Directive;
            continue;
        }
        if (xp.typeIndex >= out.types.size())
        {
            bp.binding = Binding::Malformed;
            out.malformed++;
            continue;
        }
        const BoundType &owner = out.types[xp.typeIndex];
        bp.ownerFullName = owner.fullName;
        bp.owner = owner.type;
        if (owner.binding == Binding::Malformed)
        {
            bp.binding = Binding::Malformed;
            out.malformed++;
            continue;
        }
        const Property *p = nullptr;
        TypeId declaring = kNoType;
        if (owner.type != kNoType)
        {
            p = catalog.FindProperty(owner.type, bp.name, &declaring);
            if (!p)
                p = catalog.FindAttachedProperty(owner.type, bp.name);
            if (p && declaring == kNoType)
                declaring = p->owner;
        }
        if (p)
        {
            bp.binding = Binding::Resolved;
            bp.property = p;
            bp.declaring = declaring;
        }
        else
        {
            bp.binding = Binding::Unknown;
            out.unknownProperties++;
        }
    }
    return true;
}

}  // namespace

bool BindXbf(const uint8_t *data, size_t size, const xbf::File &file, const Catalog &catalog, XbfBinding &out)
{
    return BindImpl(data, size, file, catalog, nullptr, out);
}

bool BindXbfRecording(const uint8_t *data, size_t size, const xbf::File &file, Catalog &catalog, XbfBinding &out)
{
    return BindImpl(data, size, file, catalog, &catalog, out);
}

}  // namespace laml::types
