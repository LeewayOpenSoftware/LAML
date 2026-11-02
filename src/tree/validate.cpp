// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML tree validation (M4). See validate.h for what the three kinds of finding mean.
#include "tree/validate.h"

#include <cstdio>

namespace laml::tree {

const char *FindingKindName(FindingKind kind)
{
    switch (kind)
    {
    case FindingKind::FormatError: return "FormatError";
    case FindingKind::UnknownReference: return "UnknownReference";
    case FindingKind::NotImplemented: return "NotImplemented";
    }
    return "?";
}

const char *FindingCodeName(FindingCode code)
{
    switch (code)
    {
    case FindingCode::ConstantKindMismatch: return "ConstantKindMismatch";
    case FindingCode::ConstantOutOfRange: return "ConstantOutOfRange";
    case FindingCode::NullNotAllowed: return "NullNotAllowed";
    case FindingCode::EnumTypeMismatch: return "EnumTypeMismatch";
    case FindingCode::EnumValueNotMember: return "EnumValueNotMember";
    case FindingCode::PropertyNotOnType: return "PropertyNotOnType";
    case FindingCode::ReadOnlyAssigned: return "ReadOnlyAssigned";
    case FindingCode::ObjectTypeMismatch: return "ObjectTypeMismatch";
    case FindingCode::InvalidReference: return "InvalidReference";
    case FindingCode::UnknownProperty: return "UnknownProperty";
    case FindingCode::UnknownObjectType: return "UnknownObjectType";
    case FindingCode::UnknownValueType: return "UnknownValueType";
    case FindingCode::ValueFormNotChecked: return "ValueFormNotChecked";
    case FindingCode::EnumNumbersNotDeclared: return "EnumNumbersNotDeclared";
    case FindingCode::CollectionItemNotChecked: return "CollectionItemNotChecked";
    case FindingCode::StructTextNotChecked: return "StructTextNotChecked";
    }
    return "?";
}

namespace {

using types::Catalog;
using types::Kind;
using types::Property;
using types::TypeId;
using types::TypeInfo;

struct Outcome
{
    bool format = false, unknown = false, notImplemented = false;
};

enum class Num { None, Int16, UInt16, Int32, UInt32, Int64, UInt64, Byte, Single, Double };

Num NumericOf(const std::string &name)
{
    if (name == "LamlCore.Int16") return Num::Int16;
    if (name == "LamlCore.UInt16") return Num::UInt16;
    if (name == "LamlCore.Int32") return Num::Int32;
    if (name == "LamlCore.UInt32") return Num::UInt32;
    if (name == "LamlCore.Int64") return Num::Int64;
    if (name == "LamlCore.UInt64") return Num::UInt64;
    if (name == "LamlCore.Byte") return Num::Byte;
    if (name == "LamlCore.Single") return Num::Single;
    if (name == "LamlCore.Double") return Num::Double;
    return Num::None;
}

bool FitsInteger(Num n, int64_t v)
{
    switch (n)
    {
    case Num::Int16: return v >= -32768 && v <= 32767;
    case Num::UInt16: return v >= 0 && v <= 65535;
    case Num::Int32: return v >= -2147483648LL && v <= 2147483647LL;
    case Num::UInt32: return v >= 0 && v <= 4294967295LL;
    case Num::Int64: return true;
    case Num::UInt64: return v >= 0;
    case Num::Byte: return v >= 0 && v <= 255;
    default: return true;
    }
}

class Validator
{
public:
    Validator(const LogicalTree &t, const Catalog &c, const RefResolver &r, const ValidateLimits &l, ValidationReport &o)
        : t_(t), c_(c), r_(r), limits_(l), out_(o)
    {
    }

    void Run()
    {
        for (const LogicalNode &n : t_.nodes)
        {
            for (uint32_t i = 0; i < n.properties.size(); i++)
                Assignment_(n, i);
            for (uint32_t i = 0; i < n.items.size(); i++)
                Item_(n, i);
        }
    }

private:
    const LogicalTree &t_;
    const Catalog &c_;
    const RefResolver &r_;
    const ValidateLimits &limits_;
    ValidationReport &out_;

    void Add(Outcome &o, FindingKind kind, FindingCode code, const LogicalNode &n, bool item, uint32_t index, const SourcePos &pos,
             const std::string &message)
    {
        if (kind == FindingKind::FormatError) o.format = true;
        else if (kind == FindingKind::UnknownReference) o.unknown = true;
        else o.notImplemented = true;
        out_.counts.byCode[static_cast<size_t>(code)]++;
        if (out_.findings.size() >= limits_.maxFindings)
        {
            out_.findingsDropped++;
            return;
        }
        Finding f;
        f.kind = kind;
        f.code = code;
        f.node = n.id;
        f.item = item;
        f.index = index;
        f.message = message;
        f.pos = pos;
        out_.findings.push_back(std::move(f));
    }

    static std::string Name(const types::Property *p, const PropertyResolution &r)
    {
        if (p)
        {
            std::string owner = r.ownerName.empty() ? std::string() : r.ownerName + ".";
            return owner + p->name;
        }
        return r.ownerName.empty() ? r.name : r.ownerName + "." + r.name;
    }

    // The type of the object a node stands for: its own, the value type of the member it is the value of,
    // or - for a placeholder of a deferred stream - the type of the node it stands for.
    TypeId EffectiveType(const LogicalNode &n, int depth = 0) const
    {
        if (depth > 8) return types::kNoType;
        if (n.external)
            return n.contextNode < t_.nodes.size() ? EffectiveType(t_.nodes[n.contextNode], depth + 1) : types::kNoType;
        if (n.fromMember)
        {
            if (n.member.status != RefStatus::Resolved) return types::kNoType;
            const Property *p = n.member.attached ? c_.FindAttachedProperty(n.member.owner, n.member.name)
                                                  : c_.FindProperty(n.member.owner, n.member.name);
            return p ? p->valueType : types::kNoType;
        }
        return n.type.status == RefStatus::Resolved ? n.type.type : types::kNoType;
    }

    void Assignment_(const LogicalNode &n, uint32_t index)
    {
        const struct Assignment &a = n.properties[index];
        Outcome o;
        out_.counts.assignments++;
        Check(n, a, index, o);
        if (o.format) out_.counts.assignmentsFormatError++;
        else if (o.unknown) out_.counts.assignmentsUnknown++;
        else if (o.notImplemented) out_.counts.assignmentsNotImplemented++;
        else out_.counts.assignmentsValid++;
    }

    void Item_(const LogicalNode &n, uint32_t index)
    {
        const Item &it = n.items[index];
        Outcome o;
        out_.counts.items++;
        // The catalog stores a property's value type, not the element type of a collection: nothing to check against.
        Add(o, FindingKind::NotImplemented, FindingCode::CollectionItemNotChecked, n, true, index, it.pos,
            "collection element types are not in the catalog; the item is not checked");
        out_.counts.itemsNotImplemented++;
    }

    void Check(const LogicalNode &n, const struct Assignment &a, uint32_t index, Outcome &o)
    {
        const PropertyResolution &pr = a.property;
        switch (pr.status)
        {
        case RefStatus::Resolved: break;
        case RefStatus::UnknownProperty:
        case RefStatus::UnknownType:
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownProperty, n, false, index, a.pos,
                "property " + Name(nullptr, pr) + " is not in the catalog: " + pr.detail);
            return;
        case RefStatus::InvalidReference:
            Add(o, FindingKind::FormatError, FindingCode::InvalidReference, n, false, index, a.pos,
                "invalid property reference: " + pr.detail);
            return;
        default:
            Add(o, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked, n, false, index, a.pos,
                std::string("property reference outcome ") + RefStatusName(pr.status) + " is not checked");
            return;
        }

        const Property *p = pr.attached ? c_.FindAttachedProperty(pr.owner, pr.name) : c_.FindProperty(pr.owner, pr.name);
        if (!p)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownProperty, n, false, index, a.pos,
                "property " + Name(nullptr, pr) + " is resolved but not found by the catalog");
            return;
        }

        // 1. the property belongs to the object it is set on
        if (!pr.attached && pr.declaring != types::kNoType)
        {
            TypeId eff = EffectiveType(n);
            if (eff == types::kNoType)
                Add(o, FindingKind::UnknownReference, FindingCode::UnknownObjectType, n, false, index, a.pos,
                    "the type of the object " + Name(p, pr) + " is set on is not in the catalog");
            else if (!c_.IsAssignable(eff, pr.declaring))
            {
                const TypeInfo *ti = c_.Get(eff);
                Add(o, FindingKind::FormatError, FindingCode::PropertyNotOnType, n, false, index, a.pos,
                    Name(p, pr) + " is set on " + (ti ? ti->fullName : std::string("?")) + ", which does not derive from " +
                        pr.ownerName);
            }
        }

        // 2. the value
        const LogicalValue &v = a.value;
        switch (v.kind)
        {
        case ValueKind::None: break;
        case ValueKind::Constant:
            if ((p->flags & types::kPropReadOnly) != 0)
                Add(o, FindingKind::FormatError, FindingCode::ReadOnlyAssigned, n, false, index, a.pos,
                    Name(p, pr) + " is read-only but is given a constant");
            CheckConstant(n, a, index, *p, o);
            break;
        case ValueKind::StaticResource:
        case ValueKind::ThemeResource:
            if ((p->flags & types::kPropReadOnly) != 0)
                Add(o, FindingKind::FormatError, FindingCode::ReadOnlyAssigned, n, false, index, a.pos,
                    Name(p, pr) + " is read-only but is given a resource reference");
            Add(o, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked, n, false, index, a.pos,
                "resource references are not looked up, so the value type is not checked");
            break;
        case ValueKind::Object:
            CheckObject(n, a, index, *p, o);
            break;
        default:
            Add(o, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked, n, false, index, a.pos,
                std::string("value form ") + ValueKindName(v.kind) + " is not checked");
            break;
        }
    }

    void CheckObject(const LogicalNode &n, const struct Assignment &a, uint32_t index, const Property &p, Outcome &o)
    {
        if (a.value.object >= t_.nodes.size())
            return;
        const LogicalNode &obj = t_.nodes[a.value.object];
        if (obj.fromMember)
            return;     // the property's own value (a collection filled in place): same property, nothing to compare
        if (p.valueType == types::kNoType)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownValueType, n, false, index, a.pos,
                "the value type " + p.valueTypeName + " of " + p.name + " is not in the catalog");
            return;
        }
        if (p.valueTypeName == "LamlCore.Object")
            return;
        TypeId eff = EffectiveType(obj);
        if (eff == types::kNoType)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownObjectType, n, false, index, a.pos,
                "the type of the object given to " + p.name + " is not in the catalog");
            return;
        }
        if (!c_.IsAssignable(eff, p.valueType))
        {
            const TypeInfo *ti = c_.Get(eff);
            Add(o, FindingKind::FormatError, FindingCode::ObjectTypeMismatch, n, false, index, a.pos,
                "an object of type " + (ti ? ti->fullName : std::string("?")) + " is given to " + p.name + " of type " + p.valueTypeName);
        }
    }

    void CheckConstant(const LogicalNode &n, const struct Assignment &a, uint32_t index, const Property &p, Outcome &o)
    {
        const xbf::Constant &k = a.value.constant;
        const std::string &vn = p.valueTypeName;
        const TypeInfo *vt = c_.Get(p.valueType);
        const bool known = vt != nullptr;
        const bool isObject = vn == "LamlCore.Object";
        const bool isBool = vn == "LamlCore.Boolean";
        const bool isString = vn == "LamlCore.String";
        const Num num = NumericOf(vn);
        const bool isEnum = known && vt->kind == Kind::Enum;
        const bool nullable = (p.flags & types::kPropNullable) != 0;

        auto Mismatch = [&](const char *what) {
            Add(o, FindingKind::FormatError, FindingCode::ConstantKindMismatch, n, false, index, a.pos,
                std::string(what) + " constant for " + p.name + " of type " + vn);
        };
        auto Unknown = [&]() {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownValueType, n, false, index, a.pos,
                "the value type " + vn + " of " + p.name + " is not in the catalog");
        };
        // A type-converted constant is text for a converter that LAML does not run.
        if (a.value.typeConverted)
        {
            Add(o, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked, n, false, index, a.pos,
                "type-converted text for " + p.name + " of type " + vn + " is not checked");
            return;
        }

        switch (k.kind)
        {
        case xbf::ConstKind::None:
            Add(o, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked, n, false, index, a.pos, "empty constant");
            break;
        case xbf::ConstKind::BoolFalse:
        case xbf::ConstKind::BoolTrue:
            if (isBool || isObject) break;
            if (!known && num == Num::None && !isString) { Unknown(); break; }
            Mismatch("boolean");
            break;
        case xbf::ConstKind::Float:
            if (num == Num::Single || num == Num::Double || isObject) break;
            if (!known && num == Num::None && !isBool && !isString) { Unknown(); break; }
            Mismatch("floating-point");
            break;
        case xbf::ConstKind::Signed:
            if (num != Num::None)
            {
                if (!FitsInteger(num, k.integer))
                    Add(o, FindingKind::FormatError, FindingCode::ConstantOutOfRange, n, false, index, a.pos,
                        "the integer " + std::to_string(static_cast<long long>(k.integer)) + " does not fit " + vn);
                break;     // an integer given to Single/Double widens
            }
            if (isObject) break;
            if (!known && !isBool && !isString) { Unknown(); break; }
            Mismatch("integer");
            break;
        case xbf::ConstKind::SharedString:
        case xbf::ConstKind::UniqueString:
            if (isString || isObject) break;
            if (isBool || num != Num::None || isEnum) { Mismatch("string"); break; }
            if (!known) { Unknown(); break; }
            Add(o, FindingKind::NotImplemented, FindingCode::StructTextNotChecked, n, false, index, a.pos,
                "a string given to " + p.name + " of type " + vn + " is not checked (no converter is run)");
            break;
        case xbf::ConstKind::NullString:
            if (isString || isObject || nullable) break;
            if (!known && !isBool && num == Num::None) { Unknown(); break; }
            if (isBool || num != Num::None || isEnum || (known && vt->kind == Kind::Struct))
                Add(o, FindingKind::FormatError, FindingCode::NullNotAllowed, n, false, index, a.pos,
                    "null for " + p.name + " of type " + vn + ", which cannot be null");
            break;
        case xbf::ConstKind::Thickness:
            CheckStruct(n, a, index, p, "Microsoft.UI.Xaml.Thickness", "thickness", o);
            break;
        case xbf::ConstKind::GridLength:
            CheckStruct(n, a, index, p, "Microsoft.UI.Xaml.GridLength", "grid length", o);
            break;
        case xbf::ConstKind::Color:
            CheckStruct(n, a, index, p, "Windows.UI.Color", "color", o);
            break;
        case xbf::ConstKind::Enum:
            CheckEnum(n, a, index, p, o);
            break;
        }
    }

    // A struct constant fits a property whose declared value type has exactly that name (the name decides even
    // when the catalog does not describe the struct), or Object.
    void CheckStruct(const LogicalNode &n, const struct Assignment &a, uint32_t index, const Property &p, const char *structName,
                     const char *what, Outcome &o)
    {
        if (p.valueTypeName == structName || p.valueTypeName == "LamlCore.Object")
            return;
        // The XAML syntax of a Brush property takes a colour (documented on the Brush class page: "#aarrggbb",
        // a colour name, ...), and the file stores it as a colour constant: a Brush or SolidColorBrush property
        // accepts one. Gradient and image brushes do not.
        if (std::string(structName) == "Windows.UI.Color" &&
            (p.valueTypeName == "Microsoft.UI.Xaml.Media.Brush" || p.valueTypeName == "Microsoft.UI.Xaml.Media.SolidColorBrush"))
            return;
        if (p.valueType == types::kNoType)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownValueType, n, false, index, a.pos,
                "the value type " + p.valueTypeName + " of " + p.name + " is not in the catalog");
            return;
        }
        Add(o, FindingKind::FormatError, FindingCode::ConstantKindMismatch, n, false, index, a.pos,
            std::string(what) + " constant for " + p.name + " of type " + p.valueTypeName);
    }

    void CheckEnum(const LogicalNode &n, const struct Assignment &a, uint32_t index, const Property &p, Outcome &o)
    {
        const xbf::Constant &k = a.value.constant;
        xbf::Ref ref;
        ref.id = k.enumType;
        ref.trusted = true;
        ref.present = true;
        TypeResolution tr = r_.ResolveType(ref);
        if (tr.status != RefStatus::Resolved)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownValueType, n, false, index, a.pos,
                "the enum type of the constant (stable type #" + std::to_string(k.enumType) + ") is not resolvable: " + tr.detail);
            return;
        }
        if (p.valueTypeName == "LamlCore.Object")
            return;
        if (p.valueType == types::kNoType)
        {
            Add(o, FindingKind::UnknownReference, FindingCode::UnknownValueType, n, false, index, a.pos,
                "the value type " + p.valueTypeName + " of " + p.name + " is not in the catalog");
            return;
        }
        if (p.valueType != tr.type)
        {
            Add(o, FindingKind::FormatError, FindingCode::EnumTypeMismatch, n, false, index, a.pos,
                "an enum constant of " + tr.name + " is given to " + p.name + " of type " + p.valueTypeName);
            return;
        }
        if (!c_.EnumIsNumbered(tr.type))
        {
            Add(o, FindingKind::NotImplemented, FindingCode::EnumNumbersNotDeclared, n, false, index, a.pos,
                "the catalog declares no numbers for " + tr.name);
            return;
        }
        if (!c_.EnumAccepts(tr.type, k.integer))
            Add(o, FindingKind::FormatError, FindingCode::EnumValueNotMember, n, false, index, a.pos,
                std::to_string(static_cast<long long>(k.integer)) + " is not a value of " + tr.name);
    }
};

}  // namespace

void Validate(const LogicalTree &tree, const types::Catalog &catalog, const RefResolver &resolver, const ValidateLimits &limits,
              ValidationReport &out)
{
    out = ValidationReport();
    Validator v(tree, catalog, resolver, limits, out);
    v.Run();
}

std::string DumpValidation(const ValidationReport &r, size_t maxFindings)
{
    char buf[512];
    std::string s;
    const ValidationCounts &c = r.counts;
    std::snprintf(buf, sizeof(buf),
                  "assignments=%zu valid=%zu formatError=%zu unknownReference=%zu notImplemented=%zu\n"
                  "items=%zu valid=%zu formatError=%zu unknownReference=%zu notImplemented=%zu\n",
                  c.assignments, c.assignmentsValid, c.assignmentsFormatError, c.assignmentsUnknown, c.assignmentsNotImplemented,
                  c.items, c.itemsValid, c.itemsFormatError, c.itemsUnknown, c.itemsNotImplemented);
    s += buf;
    for (size_t i = 0; i < r.findings.size() && i < maxFindings; i++)
    {
        const Finding &f = r.findings[i];
        std::snprintf(buf, sizeof(buf), "  %s/%s stream %u node %u %s %u (offset %u): ", FindingKindName(f.kind),
                      FindingCodeName(f.code), f.pos.stream, f.node, f.item ? "item" : "assignment", f.index, f.pos.offset);
        s += buf;
        s += f.message + "\n";
    }
    return s;
}

}  // namespace laml::tree
