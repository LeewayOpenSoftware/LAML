// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "tree/ref_resolver.h"

namespace laml::tree {

const char *RefStatusName(RefStatus status)
{
    switch (status)
    {
    case RefStatus::Absent: return "Absent";
    case RefStatus::Resolved: return "Resolved";
    case RefStatus::InvalidReference: return "InvalidReference";
    case RefStatus::UnknownType: return "UnknownType";
    case RefStatus::UnknownProperty: return "UnknownProperty";
    case RefStatus::Directive: return "Directive";
    case RefStatus::Unimplemented: return "Unimplemented";
    }
    return "?";
}

RefResolver::RefResolver(const uint8_t *data, size_t size, const xbf::File &file, const types::Catalog &catalog,
                         const types::StableTable *stable, const ResolverOptions &options)
    : data_(data), size_(size), file_(file), catalog_(catalog), stable_(stable), options_(options)
{
    bound_ = types::BindXbf(data, size, file, catalog, binding_);
    if (!bound_)
        stableReason_ = "the type catalog is not resolved";
    else if (file.version.major != 2)
        stableReason_ = "stable-index references are understood for XBF 2.x only";
    else if (!stable || stable->TypeSlots() == 0 || stable->PropertySlots() == 0)
        stableReason_ = "no stable index table is loaded";
    else
        stableSupported_ = true;
}

std::string RefResolver::XmlNamespaceUri(uint32_t index) const
{
    if (index >= file_.xmlNamespaces.size() || file_.xmlNamespaces[index].uriIndex >= file_.strings.size())
        return std::string();
    return types::Utf16ToUtf8(xbf::GetString(data_, size_, file_, file_.xmlNamespaces[index].uriIndex));
}

TypeResolution RefResolver::ResolveType(const xbf::Ref &ref) const
{
    TypeResolution r;

    if (!ref.present)
        return r;
    r.id = ref.id;
    r.stable = ref.trusted;
    if (!bound_)
    {
        r.status = RefStatus::Unimplemented;
        r.detail = stableReason_;
        return r;
    }

    if (!ref.trusted)
    {
        if (ref.id >= binding_.types.size())
        {
            r.status = RefStatus::InvalidReference;
            r.detail = "type index " + std::to_string(ref.id) + " is outside the file's type table (" +
                       std::to_string(binding_.types.size()) + " entries)";
            return r;
        }
        const types::BoundType &bt = binding_.types[ref.id];
        r.name = bt.fullName;
        switch (bt.binding)
        {
        case types::Binding::Resolved:
            r.status = RefStatus::Resolved;
            r.type = bt.type;
            break;
        case types::Binding::Unknown:
            r.status = RefStatus::UnknownType;
            r.detail = "type '" + bt.fullName + "' is listed in the file but the catalog does not describe it";
            break;
        case types::Binding::Directive:
            r.status = RefStatus::Directive;
            r.detail = "markup directive type";
            break;
        case types::Binding::Malformed:
            r.status = RefStatus::InvalidReference;
            r.detail = "the file's type entry " + std::to_string(ref.id) + " points outside its namespace or string tables";
            break;
        }
        return r;
    }

    if (!stableSupported_)
    {
        r.status = RefStatus::Unimplemented;
        r.detail = "stable type index " + std::to_string(ref.id) + ": " + stableReason_;
        return r;
    }
    const types::StableType *st = stable_->Type(ref.id);
    if (st && ref.id == 0)
    {
        r.status = RefStatus::InvalidReference;
        r.detail = "stable type index 0 is the table's Unknown entry";
        return r;
    }
    if (!st)
    {
        r.status = RefStatus::InvalidReference;
        r.detail = "stable type index " + std::to_string(ref.id) + " is outside the stable table (" +
                   std::to_string(stable_->TypeSlots()) + " slots)";
        return r;
    }
    r.retiredSlot = st->retired;
    const std::string &stableName = st->retired ? st->originalBase : st->name;
    if (stableName.empty())
    {
        r.status = RefStatus::InvalidReference;
        r.detail = "stable type index " + std::to_string(ref.id) + " is a retired slot without a usable original name";
        return r;
    }
    r.name = stableName;
    types::XmlResolution xr = catalog_.ResolveXmlName(options_.stableXmlns, stableName);
    if (xr.type == types::kNoType)
    {
        r.status = RefStatus::UnknownType;
        r.detail = "stable type '" + stableName + "' is not in the catalog (tried: " + xr.tried + ")" +
                   (st->retired ? "; the slot is retired in the table, its original name was used" : "");
        return r;
    }
    r.status = RefStatus::Resolved;
    r.type = xr.type;
    if (const types::TypeInfo *ti = catalog_.Get(xr.type))
        r.name = ti->fullName;
    return r;
}

PropertyResolution RefResolver::ResolveProperty(const xbf::Ref &ref) const
{
    PropertyResolution r;

    if (!ref.present)
        return r;
    r.id = ref.id;
    r.stable = ref.trusted;
    if (!bound_)
    {
        r.status = RefStatus::Unimplemented;
        r.detail = stableReason_;
        return r;
    }

    if (!ref.trusted)
    {
        if (ref.id >= binding_.properties.size())
        {
            r.status = RefStatus::InvalidReference;
            r.detail = "property index " + std::to_string(ref.id) + " is outside the file's property table (" +
                       std::to_string(binding_.properties.size()) + " entries)";
            return r;
        }
        const types::BoundProperty &bp = binding_.properties[ref.id];
        r.name = bp.name;
        r.ownerName = bp.ownerFullName;
        r.owner = bp.owner;
        switch (bp.binding)
        {
        case types::Binding::Resolved:
            r.status = RefStatus::Resolved;
            r.declaring = bp.declaring;
            r.attached = bp.property && (bp.property->flags & types::kPropAttached);
            break;
        case types::Binding::Unknown:
            r.status = RefStatus::UnknownProperty;
            r.detail = "property '" + bp.ownerFullName + "." + bp.name + "' is listed in the file but the catalog does not describe it";
            break;
        case types::Binding::Directive:
            r.status = RefStatus::Directive;
            r.detail = "markup directive property";
            break;
        case types::Binding::Malformed:
            r.status = RefStatus::InvalidReference;
            r.detail = "the file's property entry " + std::to_string(ref.id) + " points outside its tables";
            break;
        }
        return r;
    }

    if (!stableSupported_)
    {
        r.status = RefStatus::Unimplemented;
        r.detail = "stable property index " + std::to_string(ref.id) + ": " + stableReason_;
        return r;
    }
    const types::StableProperty *sp = stable_->Property(ref.id);
    if (sp && ref.id == 0)
    {
        r.status = RefStatus::InvalidReference;
        r.detail = "stable property index 0 is the table's Unknown entry";
        return r;
    }
    if (!sp)
    {
        r.status = RefStatus::InvalidReference;
        r.detail = "stable property index " + std::to_string(ref.id) + " is outside the stable table (" +
                   std::to_string(stable_->PropertySlots()) + " slots)";
        return r;
    }
    r.retiredSlot = sp->retired;
    const std::string &ownerName = sp->retired ? sp->originalOwner : sp->owner;
    const std::string &memberName = sp->retired ? sp->originalMember : sp->member;
    if (ownerName.empty() || memberName.empty())
    {
        r.status = RefStatus::InvalidReference;
        r.name = sp->original;
        r.detail = "stable property index " + std::to_string(ref.id) + " is a retired slot (" + sp->original +
                   ") whose original owner and member cannot be recovered";
        return r;
    }
    r.ownerName = ownerName;
    r.name = memberName;
    types::XmlResolution xr = catalog_.ResolveXmlName(options_.stableXmlns, ownerName);
    if (xr.type == types::kNoType)
    {
        r.status = RefStatus::UnknownType;
        r.detail = "owner type '" + ownerName + "' of stable property '" + ownerName + "." + memberName + "' is not in the catalog" +
                   (sp->retired ? " (retired slot, original name used)" : "");
        return r;
    }
    r.owner = xr.type;
    if (const types::TypeInfo *ti = catalog_.Get(xr.type))
        r.ownerName = ti->fullName;
    types::TypeId declaring = types::kNoType;
    const types::Property *p = catalog_.FindProperty(xr.type, memberName, &declaring);
    if (!p)
    {
        p = catalog_.FindAttachedProperty(xr.type, memberName);
        if (p)
        {
            declaring = p->owner;
            r.attached = true;
        }
    }
    if (!p)
    {
        r.status = RefStatus::UnknownProperty;
        r.detail = "property '" + ownerName + "." + memberName + "' is not in the catalog" +
                   (sp->retired ? " (retired slot, original name used)" : "");
        return r;
    }
    r.status = RefStatus::Resolved;
    r.declaring = declaring;
    return r;
}

}  // namespace laml::tree
