// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "xbf_nodes.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace laml::xbf {

const char *NodeTypeName(NodeType t)
{
    switch (t)
    {
    case NodeType::PushScope: return "PushScope";
    case NodeType::PopScope: return "PopScope";
    case NodeType::AddNamespace: return "AddNamespace";
    case NodeType::PushConstant: return "PushConstant";
    case NodeType::SetValue: return "SetValue";
    case NodeType::AddToCollection: return "AddToCollection";
    case NodeType::AddToDictionary: return "AddToDictionary";
    case NodeType::AddToDictionaryWithKey: return "AddToDictionaryWithKey";
    case NodeType::CheckPeerType: return "CheckPeerType";
    case NodeType::SetConnectionId: return "SetConnectionId";
    case NodeType::SetName: return "SetName";
    case NodeType::GetResourcePropertyBag: return "GetResourcePropertyBag";
    case NodeType::SetCustomRuntimeData: return "SetCustomRuntimeData";
    case NodeType::SetDeferredProperty: return "SetDeferredProperty";
    case NodeType::PushScopeAddNamespace: return "PushScopeAddNamespace";
    case NodeType::PushScopeGetValue: return "PushScopeGetValue";
    case NodeType::PushScopeCreateTypeBeginInit: return "PushScopeCreateTypeBeginInit";
    case NodeType::PushScopeCreateTypeWithConstantBeginInit: return "PushScopeCreateTypeWithConstantBeginInit";
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit: return "PushScopeCreateTypeWithTypeConvertedConstantBeginInit";
    case NodeType::CreateTypeBeginInit: return "CreateTypeBeginInit";
    case NodeType::CreateTypeWithConstantBeginInit: return "CreateTypeWithConstantBeginInit";
    case NodeType::CreateTypeWithTypeConvertedConstantBeginInit: return "CreateTypeWithTypeConvertedConstantBeginInit";
    case NodeType::SetValueConstant: return "SetValueConstant";
    case NodeType::SetValueTypeConvertedConstant: return "SetValueTypeConvertedConstant";
    case NodeType::SetValueTypeConvertedResolvedProperty: return "SetValueTypeConvertedResolvedProperty";
    case NodeType::SetValueTypeConvertedResolvedType: return "SetValueTypeConvertedResolvedType";
    case NodeType::SetValueFromStaticResource: return "SetValueFromStaticResource";
    case NodeType::SetValueFromTemplateBinding: return "SetValueFromTemplateBinding";
    case NodeType::SetValueFromMarkupExtension: return "SetValueFromMarkupExtension";
    case NodeType::EndInitPopScope: return "EndInitPopScope";
    case NodeType::ProvideStaticResourceValue: return "ProvideStaticResourceValue";
    case NodeType::ProvideThemeResourceValue: return "ProvideThemeResourceValue";
    case NodeType::SetValueFromThemeResource: return "SetValueFromThemeResource";
    case NodeType::BeginConditionalScope: return "BeginConditionalScope";
    case NodeType::EndConditionalScope: return "EndConditionalScope";
    case NodeType::EndInitProvideValuePopScope: return "EndInitProvideValuePopScope";
    case NodeType::ProvideValue: return "ProvideValue";
    }
    return "?";
}

const char *RuntimeDataKindName(RuntimeDataKind k)
{
    switch (k)
    {
    case RuntimeDataKind::None: return "None";
    case RuntimeDataKind::ResourceDictionary: return "ResourceDictionary";
    case RuntimeDataKind::Style: return "Style";
    case RuntimeDataKind::VisualStateGroupCollection: return "VisualStateGroupCollection";
    case RuntimeDataKind::DeferredElement: return "DeferredElement";
    }
    return "?";
}

const char *StreamStatusName(StreamStatus s)
{
    switch (s)
    {
    case StreamStatus::Ok: return "Ok";
    case StreamStatus::BadRange: return "BadRange";
    case StreamStatus::BadOpcode: return "BadOpcode";
    case StreamStatus::Truncated: return "Truncated";
    case StreamStatus::BadReference: return "BadReference";
    case StreamStatus::BadScope: return "BadScope";
    case StreamStatus::LimitExceeded: return "LimitExceeded";
    case StreamStatus::OpaqueData: return "OpaqueData";
    }
    return "?";
}

namespace {

bool IsKnownOpcode(uint8_t op)
{
    switch (static_cast<NodeType>(op))
    {
    case NodeType::PushScope: case NodeType::PopScope: case NodeType::AddNamespace: case NodeType::PushConstant:
    case NodeType::SetValue: case NodeType::AddToCollection: case NodeType::AddToDictionary:
    case NodeType::AddToDictionaryWithKey: case NodeType::CheckPeerType: case NodeType::SetConnectionId:
    case NodeType::SetName: case NodeType::GetResourcePropertyBag: case NodeType::SetCustomRuntimeData:
    case NodeType::SetDeferredProperty: case NodeType::PushScopeAddNamespace: case NodeType::PushScopeGetValue:
    case NodeType::PushScopeCreateTypeBeginInit: case NodeType::PushScopeCreateTypeWithConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit: case NodeType::CreateTypeBeginInit:
    case NodeType::CreateTypeWithConstantBeginInit: case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
    case NodeType::SetValueConstant: case NodeType::SetValueTypeConvertedConstant:
    case NodeType::SetValueTypeConvertedResolvedProperty: case NodeType::SetValueTypeConvertedResolvedType:
    case NodeType::SetValueFromStaticResource: case NodeType::SetValueFromTemplateBinding:
    case NodeType::SetValueFromMarkupExtension: case NodeType::EndInitPopScope:
    case NodeType::ProvideStaticResourceValue: case NodeType::ProvideThemeResourceValue:
    case NodeType::SetValueFromThemeResource: case NodeType::BeginConditionalScope:
    case NodeType::EndConditionalScope: case NodeType::EndInitProvideValuePopScope: case NodeType::ProvideValue:
        return true;
    }
    return false;
}

bool OpensScope(NodeType t)
{
    switch (t)
    {
    case NodeType::PushScope: case NodeType::PushScopeAddNamespace: case NodeType::PushScopeGetValue:
    case NodeType::PushScopeCreateTypeBeginInit: case NodeType::PushScopeCreateTypeWithConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
        return true;
    default:
        return false;
    }
}

bool ClosesScope(NodeType t)
{
    return t == NodeType::PopScope || t == NodeType::EndInitPopScope || t == NodeType::EndInitProvideValuePopScope;
}

// Thrown inside the decoder only; never escapes DecodeNodes.
struct Fail
{
    StreamStatus status;
    uint32_t offset;
    std::string message;
};

class Cursor
{
public:
    Cursor(const uint8_t *p, size_t n) : m_p(p), m_n(n) {}
    size_t pos() const { return m_pos; }
    size_t remaining() const { return m_n - m_pos; }
    bool done() const { return m_pos >= m_n; }

    uint8_t U8()
    {
        Need(1);
        return m_p[m_pos++];
    }
    uint16_t U16()
    {
        Need(2);
        uint16_t v = (uint16_t)(m_p[m_pos] | (m_p[m_pos + 1] << 8));
        m_pos += 2;
        return v;
    }
    uint32_t U32()
    {
        Need(4);
        uint32_t v = (uint32_t)m_p[m_pos] | ((uint32_t)m_p[m_pos + 1] << 8) | ((uint32_t)m_p[m_pos + 2] << 16) |
                     ((uint32_t)m_p[m_pos + 3] << 24);
        m_pos += 4;
        return v;
    }
    float F32()
    {
        uint32_t u = U32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    // 7-bit encoded unsigned 32-bit value: at most 5 bytes, the fifth may carry only 4 bits.
    uint32_t V7()
    {
        uint32_t v = 0;
        for (unsigned i = 0; i < 5; i++)
        {
            Need(1);
            uint8_t b = m_p[m_pos++];
            if (i == 4 && (b & 0xF0))
                throw Fail{StreamStatus::BadReference, (uint32_t)m_pos - 1, "7-bit integer is longer than 32 bits"};
            v |= (uint32_t)(b & 0x7F) << (7 * i);
            if (!(b & 0x80))
                return v;
        }
        throw Fail{StreamStatus::BadReference, (uint32_t)m_pos, "7-bit integer is longer than 5 bytes"};
    }
    bool TryV7(uint32_t &v)
    {
        size_t save = m_pos;
        try
        {
            v = V7();
            return true;
        }
        catch (const Fail &)
        {
            m_pos = save;
            return false;
        }
    }
    std::u16string Utf16(uint32_t chars)
    {
        if ((uint64_t)chars * 2 > remaining())
            throw Fail{StreamStatus::Truncated, (uint32_t)m_pos, "string runs past the end of the stream"};
        std::u16string s;
        s.reserve(chars);
        for (uint32_t i = 0; i < chars; i++)
            s.push_back((char16_t)U16());
        return s;
    }

private:
    void Need(size_t n)
    {
        if (n > remaining())
            throw Fail{StreamStatus::Truncated, (uint32_t)m_pos, "operand runs past the end of the stream"};
    }
    const uint8_t *m_p;
    size_t m_n;
    size_t m_pos = 0;
};

struct Ctx
{
    const File &file;
    const DecodeLimits &limits;
    uint32_t streamCount;
    uint32_t selfStream;
};

[[noreturn]] void Bad(StreamStatus s, const Cursor &c, const char *msg)
{
    throw Fail{s, (uint32_t)c.pos(), msg};
}

constexpr uint16_t kTrustedLimit = 0x2000;   // stable index tables stay below this (format assumption)

// PersistedXamlNode2: 15-bit object id + 1-bit "trusted" flag, 16 bits little endian.
Ref ReadRef(Cursor &c, size_t tableSize, const char *what, const Ctx &x)
{
    uint16_t raw = c.U16();
    Ref r;
    r.id = raw & 0x7FFF;
    r.trusted = (raw & 0x8000) != 0;
    r.present = true;
    (void)x;
    if (r.trusted)
    {
        if (r.id >= kTrustedLimit)
            Bad(StreamStatus::BadReference, c, "well-known index is out of range");
    }
    else if (r.id >= tableSize)
    {
        std::string m = std::string(what) + " index is outside its table";
        throw Fail{StreamStatus::BadReference, (uint32_t)c.pos() - 2, m};
    }
    return r;
}

Ref ReadType(Cursor &c, const Ctx &x) { return ReadRef(c, x.file.types.size(), "type", x); }
Ref ReadProperty(Cursor &c, const Ctx &x) { return ReadRef(c, x.file.properties.size(), "property", x); }

// Strings and namespaces only use the file tables (never the trusted form).
uint16_t ReadTableId(Cursor &c, size_t tableSize, const char *what)
{
    uint16_t raw = c.U16();
    if (raw & 0x8000)
        throw Fail{StreamStatus::BadReference, (uint32_t)c.pos() - 2, std::string(what) + " reference has the trusted flag"};
    if (raw >= tableSize)
        throw Fail{StreamStatus::BadReference, (uint32_t)c.pos() - 2, std::string(what) + " index is outside its table"};
    return raw;
}

std::u16string ReadInlineString(Cursor &c, const Ctx &x)
{
    uint32_t cch = c.U32();
    if (cch > x.limits.maxStringChars)
        Bad(StreamStatus::LimitExceeded, c, "inline string is longer than the limit");
    return c.Utf16(cch);
}

Constant ReadConstant(Cursor &c, const Ctx &x)
{
    Constant k;
    uint8_t kind = c.U8();
    if (kind == 0 || kind > (uint8_t)ConstKind::Enum)
        throw Fail{StreamStatus::BadReference, (uint32_t)c.pos() - 1, "unknown constant kind"};
    k.kind = (ConstKind)kind;
    switch (k.kind)
    {
    case ConstKind::BoolFalse:
    case ConstKind::BoolTrue:
    case ConstKind::NullString:
        break;
    case ConstKind::Float:
        k.real[0] = c.F32();
        break;
    case ConstKind::Signed:
        k.integer = (int32_t)c.U32();
        break;
    case ConstKind::Color:
        k.integer = c.U32();
        break;
    case ConstKind::SharedString:
        k.integer = ReadTableId(c, x.file.strings.size(), "string");
        break;
    case ConstKind::Thickness:
        for (float &f : k.real)
            f = c.F32();
        break;
    case ConstKind::GridLength:
        k.gridUnit = (int32_t)c.U32();   // the unit is stored first, then the value
        k.real[0] = c.F32();
        break;
    case ConstKind::UniqueString:
        k.text = ReadInlineString(c, x);
        break;
    case ConstKind::Enum:
        k.enumType = c.U16();
        if (k.enumType == 0 || k.enumType >= kTrustedLimit)
            Bad(StreamStatus::BadReference, c, "enum type index is out of range");
        k.integer = c.U32();
        break;
    case ConstKind::None:
        break;
    }
    return k;
}

// ---- runtime data payloads ----------------------------------------------------------------------
// CustomWriterRuntimeDataTypeIndex values (the numbers are part of the file format).
struct FamilyInfo
{
    RuntimeDataKind kind;
    uint32_t version;
};

bool ClassifyRuntimeData(uint32_t typeIndex, FamilyInfo &out)
{
    switch (typeIndex)
    {
    case 371: out = {RuntimeDataKind::ResourceDictionary, 1}; return true;   // legacy: stable type index
    case 7: out = {RuntimeDataKind::ResourceDictionary, 2}; return true;
    case 10: out = {RuntimeDataKind::ResourceDictionary, 3}; return true;
    case 12: out = {RuntimeDataKind::ResourceDictionary, 4}; return true;
    case 2: out = {RuntimeDataKind::Style, 1}; return true;
    case 8: out = {RuntimeDataKind::Style, 2}; return true;
    case 11: out = {RuntimeDataKind::Style, 3}; return true;
    case 420: out = {RuntimeDataKind::VisualStateGroupCollection, 1}; return true;   // legacy
    case 1: out = {RuntimeDataKind::VisualStateGroupCollection, 2}; return true;
    case 3: out = {RuntimeDataKind::VisualStateGroupCollection, 3}; return true;
    case 4: out = {RuntimeDataKind::VisualStateGroupCollection, 4}; return true;
    case 5: out = {RuntimeDataKind::VisualStateGroupCollection, 5}; return true;
    case 745: out = {RuntimeDataKind::DeferredElement, 1}; return true;   // legacy
    case 6: out = {RuntimeDataKind::DeferredElement, 2}; return true;
    case 9: out = {RuntimeDataKind::DeferredElement, 3}; return true;
    }
    return false;
}

// Element count of a persisted container: 7-bit count, bounded by the limit and by the bytes left
// (every element takes at least `minBytes`), so a damaged count cannot cause a large allocation.
uint32_t ReadCount(Cursor &c, const Ctx &x, size_t minBytes)
{
    uint32_t n = c.V7();
    if (n > x.limits.maxResourceStrings)
        Bad(StreamStatus::LimitExceeded, c, "container has more elements than the limit");
    if ((uint64_t)n * minBytes > c.remaining())
        Bad(StreamStatus::Truncated, c, "container runs past the end of the stream");
    return n;
}

uint16_t ReadStr(Cursor &c, const Ctx &x) { return ReadTableId(c, x.file.strings.size(), "string"); }

bool ReadBoolConstant(Cursor &c, const Ctx &x)
{
    size_t at = c.pos();
    Constant k = ReadConstant(c, x);
    if (k.kind == ConstKind::BoolTrue)
        return true;
    if (k.kind == ConstKind::BoolFalse)
        return false;
    throw Fail{StreamStatus::BadReference, (uint32_t)at, "boolean expected"};
}

std::vector<uint32_t> ReadTokens(Cursor &c, const Ctx &x)
{
    std::vector<uint32_t> v;
    uint32_t n = ReadCount(c, x, 1);
    for (uint32_t i = 0; i < n; i++)
        v.push_back(c.V7());
    return v;
}

std::vector<uint16_t> ReadStrings(Cursor &c, const Ctx &x)
{
    std::vector<uint16_t> v;
    uint32_t n = ReadCount(c, x, 2);
    for (uint32_t i = 0; i < n; i++)
        v.push_back(ReadStr(c, x));
    return v;
}

uint64_t ReadFixed64(Cursor &c)
{
    uint64_t lo = c.U32();
    uint64_t hi = c.U32();
    return lo | (hi << 32);
}

void ReadResourceDictionary(Cursor &c, const Ctx &x, RuntimeData &d)
{
    auto legacyMap = [&](bool implicit) {
        uint32_t n = ReadCount(c, x, 3);
        for (uint32_t i = 0; i < n; i++)
        {
            RdEntry e;
            e.key.stringId = ReadStr(c, x);
            e.key.isType = implicit;
            e.token = c.V7();
            d.resources.push_back(e);
        }
    };
    auto legacyConditional = [&](bool implicit) {
        uint32_t n = ReadCount(c, x, 3);
        for (uint32_t i = 0; i < n; i++)
        {
            RdConditional e;
            e.key.stringId = ReadStr(c, x);
            e.key.isType = implicit;
            e.tokens = ReadTokens(c, x);
            d.conditionalResources.push_back(std::move(e));
        }
    };
    auto key4 = [&]() {
        RdKey k;
        k.stringId = ReadStr(c, x);
        k.hashAndType = ReadFixed64(c);
        k.isType = (k.hashAndType & 1) != 0;
        return k;
    };
    switch (d.version)
    {
    case 1:
        legacyMap(false);
        d.autoUndeferral = ReadStrings(c, x);
        legacyMap(true);
        d.legacyImplicitTemplates = ReadStrings(c, x);
        d.legacyImplicitStyleKeys = ReadStrings(c, x);
        break;
    case 2:
        legacyMap(false);
        d.autoUndeferral = ReadStrings(c, x);
        legacyMap(true);
        d.legacyImplicitStyleKeys = ReadStrings(c, x);
        break;
    case 3:
        legacyMap(false);
        d.autoUndeferral = ReadStrings(c, x);
        legacyMap(true);
        legacyConditional(false);
        legacyConditional(true);
        break;
    default:   // 4
    {
        uint32_t n = ReadCount(c, x, 11);
        for (uint32_t i = 0; i < n; i++)
        {
            RdEntry e;
            e.key = key4();
            e.token = c.V7();
            d.resources.push_back(e);
        }
        d.autoUndeferral = ReadStrings(c, x);
        uint32_t m = ReadCount(c, x, 11);
        for (uint32_t i = 0; i < m; i++)
        {
            RdConditional e;
            e.key = key4();
            e.tokens = ReadTokens(c, x);
            d.conditionalResources.push_back(std::move(e));
        }
        break;
    }
    }
}

void ReadStyle(Cursor &c, const Ctx &x, RuntimeData &d)
{
    uint32_t n = ReadCount(c, x, 2);
    for (uint32_t i = 0; i < n; i++)
    {
        SetterEssence s;
        uint32_t flags = c.V7();
        if (flags > 0xFF)
            Bad(StreamStatus::BadReference, c, "setter flags are wider than 8 bits");
        s.flags = (uint8_t)flags;
        const bool theme = flags & 0x01, stat = flags & 0x02, str = flags & 0x04, obj = flags & 0x08,
                   resolved = flags & 0x10, container = flags & 0x20, self = flags & 0x40;
        // from version 3 a setter that is a token for itself carries no property
        if (!(d.version >= 3 && self))
        {
            if (resolved)
                s.property = ReadProperty(c, x);
            else
            {
                s.propertyName = ReadStr(c, x);
                s.hasPropertyName = true;
                s.ownerType = ReadType(c, x);
            }
        }
        if (str)
            s.stringValue = ReadStr(c, x);
        else if (container)
            s.container = ReadConstant(c, x);
        else if (stat || theme || obj || self)
            s.token = c.V7();
        d.setters.push_back(std::move(s));
    }
}

void ReadVisualStates(Cursor &c, const Ctx &x, RuntimeData &d)
{
    d.visualStateToGroup = ReadTokens(c, x);
    uint32_t n = ReadCount(c, x, 4);
    for (uint32_t i = 0; i < n; i++)
    {
        VisualStateEssence e;
        e.name = ReadStr(c, x);
        e.storyboardToken = c.V7();
        e.hasStoryboard = ReadBoolConstant(c, x);
        if (d.version >= 2)
        {
            e.propertySetterTokens = ReadTokens(c, x);
            uint32_t outer = ReadCount(c, x, 1);
            for (uint32_t k = 0; k < outer; k++)
                e.triggerValues.push_back(ReadTokens(c, x));
            if (d.version >= 3)
            {
                e.extensibleTriggerTokens = ReadTokens(c, x);
                e.triggerCollectionTokens = ReadTokens(c, x);
                if (d.version >= 5)
                    e.staticResourceTriggerTokens = ReadTokens(c, x);
            }
        }
        d.visualStates.push_back(std::move(e));
    }
    uint32_t g = ReadCount(c, x, 4);
    for (uint32_t i = 0; i < g; i++)
    {
        VisualStateGroupEssence e;
        e.name = ReadStr(c, x);
        e.hasDynamicTimelines = ReadBoolConstant(c, x);
        e.selfToken = c.V7();
        d.visualStateGroups.push_back(e);
    }
    uint32_t t = ReadCount(c, x, 5);
    for (uint32_t i = 0; i < t; i++)
    {
        VisualTransitionEssence e;
        e.toState = ReadStr(c, x);
        e.fromState = ReadStr(c, x);
        e.token = c.V7();
        d.visualTransitions.push_back(e);
    }
    d.unexpectedTokensDetected = ReadBoolConstant(c, x);
    uint32_t m = ReadCount(c, x, 3);
    for (uint32_t i = 0; i < m; i++)
    {
        uint32_t from = c.V7(), to = c.V7(), tr = c.V7();
        d.transitionMap.push_back({{from, to}, tr});
    }
    d.defaultTransitionByGroup = ReadTokens(c, x);
    d.entireCollectionToken = c.V7();
    if (d.version == 4 || d.version == 5)
        d.seenNameDirectives = ReadStrings(c, x);
}

void ReadDeferredElement(Cursor &c, const Ctx &x, RuntimeData &d)
{
    d.elementName = ReadStr(c, x);
    if (d.version >= 2)
    {
        uint32_t n = ReadCount(c, x, 3);
        for (uint32_t i = 0; i < n; i++)
        {
            Ref p = ReadProperty(c, x);
            Constant k = ReadConstant(c, x);
            d.nonDeferredProperties.push_back({p, std::move(k)});
        }
        if (d.version >= 3)
            d.realize = ReadBoolConstant(c, x);
    }
}

// Returns null for a runtime data version this decoder does not know (the caller stops the stream).
std::shared_ptr<const RuntimeData> ReadRuntimeData(Cursor &c, const Ctx &x, uint32_t typeIndex)
{
    FamilyInfo fi;
    if (!ClassifyRuntimeData(typeIndex, fi))
        return nullptr;
    auto d = std::make_shared<RuntimeData>();
    d->kind = fi.kind;
    d->version = fi.version;
    d->typeIndex = typeIndex;
    switch (fi.kind)
    {
    case RuntimeDataKind::ResourceDictionary: ReadResourceDictionary(c, x, *d); break;
    case RuntimeDataKind::Style: ReadStyle(c, x, *d); break;
    case RuntimeDataKind::VisualStateGroupCollection: ReadVisualStates(c, x, *d); break;
    case RuntimeDataKind::DeferredElement: ReadDeferredElement(c, x, *d); break;
    case RuntimeDataKind::None: break;
    }
    const bool hasConditional = (fi.kind == RuntimeDataKind::ResourceDictionary && fi.version >= 3) ||
                                (fi.kind == RuntimeDataKind::Style && fi.version >= 3);
    if (hasConditional)
    {
        uint32_t n = ReadCount(c, x, 2);
        for (uint32_t i = 0; i < n; i++)
        {
            ConditionallyDeclared cd;
            cd.token = c.V7();
            uint32_t m = ReadCount(c, x, 4);
            for (uint32_t k = 0; k < m; k++)
            {
                PredicateRef pr;
                pr.type = ReadType(c, x);
                pr.argumentsString = ReadStr(c, x);
                cd.predicates.push_back(pr);
            }
            d->conditionallyDeclared.push_back(std::move(cd));
        }
    }
    return d;
}

void DecodeOperands(Cursor &c, Node &n, const Ctx &x, const DecodeLimits &lim)
{
    switch (n.type)
    {
    case NodeType::PushScope: case NodeType::PopScope: case NodeType::AddToCollection:
    case NodeType::AddToDictionary: case NodeType::ProvideValue: case NodeType::EndInitPopScope:
    case NodeType::EndInitProvideValuePopScope: case NodeType::EndConditionalScope:
        break;

    case NodeType::AddNamespace:
    case NodeType::PushScopeAddNamespace:
        n.xmlNamespace.id = ReadTableId(c, x.file.xmlNamespaces.size(), "xml namespace");
        n.xmlNamespace.present = true;
        n.text = ReadInlineString(c, x);
        break;

    case NodeType::PushConstant: case NodeType::AddToDictionaryWithKey: case NodeType::SetConnectionId:
    case NodeType::SetName: case NodeType::GetResourcePropertyBag: case NodeType::ProvideStaticResourceValue:
    case NodeType::ProvideThemeResourceValue:
        n.constant = ReadConstant(c, x);
        break;

    case NodeType::SetValue: case NodeType::SetValueFromMarkupExtension: case NodeType::PushScopeGetValue:
        n.property1 = ReadProperty(c, x);
        break;

    case NodeType::SetValueConstant: case NodeType::SetValueTypeConvertedConstant:
    case NodeType::SetValueFromStaticResource: case NodeType::SetValueFromThemeResource:
        n.property1 = ReadProperty(c, x);
        n.constant = ReadConstant(c, x);
        break;

    case NodeType::SetValueFromTemplateBinding: case NodeType::SetValueTypeConvertedResolvedProperty:
        n.property1 = ReadProperty(c, x);
        n.property2 = ReadProperty(c, x);
        break;

    case NodeType::SetValueTypeConvertedResolvedType:
        n.property1 = ReadProperty(c, x);
        n.type1 = ReadType(c, x);
        break;

    case NodeType::PushScopeCreateTypeBeginInit: case NodeType::CreateTypeBeginInit:
        n.type1 = ReadType(c, x);
        break;

    case NodeType::PushScopeCreateTypeWithConstantBeginInit:
    case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
    case NodeType::CreateTypeWithConstantBeginInit:
    case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
        n.type1 = ReadType(c, x);
        n.constant = ReadConstant(c, x);
        break;

    case NodeType::CheckPeerType:
        n.text = ReadInlineString(c, x);
        break;

    case NodeType::BeginConditionalScope:
    {
        n.type1 = ReadType(c, x);
        uint16_t s = ReadTableId(c, x.file.strings.size(), "string");
        n.constant.kind = ConstKind::SharedString;
        n.constant.integer = s;
        break;
    }

    case NodeType::SetDeferredProperty:
    {
        n.property1 = ReadProperty(c, x);
        n.targetStream = c.V7();
        if (n.targetStream >= x.streamCount || n.targetStream == x.selfStream)
            Bad(StreamStatus::BadReference, c, "deferred stream index is out of range or refers to the stream itself");
        n.staticResourceCount = c.V7();
        n.themeResourceCount = c.V7();
        uint64_t total = (uint64_t)n.staticResourceCount + n.themeResourceCount;
        if (total > lim.maxResourceStrings)
            Bad(StreamStatus::LimitExceeded, c, "too many deferred resource names");
        if (total * 2 > c.remaining())
            Bad(StreamStatus::Truncated, c, "deferred resource names run past the end of the stream");
        for (uint64_t i = 0; i < total; i++)
            n.resourceStrings.push_back(ReadTableId(c, x.file.strings.size(), "string"));
        break;
    }

    case NodeType::SetCustomRuntimeData:
    {
        n.targetStream = c.V7();
        if (n.targetStream >= x.streamCount || n.targetStream == x.selfStream)
            Bad(StreamStatus::BadReference, c, "runtime data stream index is out of range or refers to the stream itself");
        // Legacy pre-resolved resource names: the current writers emit two zero counts.
        n.staticResourceCount = c.V7();
        n.themeResourceCount = c.V7();
        uint64_t total = (uint64_t)n.staticResourceCount + n.themeResourceCount;
        if (total > lim.maxResourceStrings)
            Bad(StreamStatus::LimitExceeded, c, "too many pre-resolved resource names");
        if (total * 2 > c.remaining())
            Bad(StreamStatus::Truncated, c, "pre-resolved resource names run past the end of the stream");
        for (uint64_t i = 0; i < total; i++)
            n.resourceStrings.push_back(ReadTableId(c, x.file.strings.size(), "string"));
        n.runtimeDataType = c.V7();
        n.runtimeData = ReadRuntimeData(c, x, n.runtimeDataType);   // null: unknown version
        break;
    }
    }
}

// Line stream: triples of 7-bit values (stream offset delta, zigzag line delta, zigzag column delta).
struct LineEntry { uint32_t offset; int64_t line; int64_t column; };

std::vector<LineEntry> ReadLines(const uint8_t *p, size_t n)
{
    std::vector<LineEntry> out;
    Cursor c(p, n);
    uint64_t off = 0;
    int64_t line = 0, col = 0;
    for (;;)
    {
        uint32_t d, l, k;
        if (!c.TryV7(d) || !c.TryV7(l) || !c.TryV7(k))
            break;   // padding or a damaged tail ends the table; line info is advisory
        off += d;
        if (off > 0xFFFFFFFFull)
            break;
        line += (l & 1) ? -(int64_t)((l + 1) / 2) : (int64_t)(l / 2);
        col += (k & 1) ? -(int64_t)((k + 1) / 2) : (int64_t)(k / 2);
        out.push_back({(uint32_t)off, line, col});
    }
    return out;
}

void DecodeStream(const uint8_t *nodes, size_t nodeLen, const uint8_t *lines, size_t lineLen, const Ctx &x,
                  uint64_t &totalNodes, DecodedStream &out)
{
    out.nodeBytes = (uint32_t)nodeLen;
    out.lineBytes = (uint32_t)lineLen;
    std::vector<LineEntry> lineTable = ReadLines(lines, lineLen);
    size_t li = 0;   // nodes ascend in offset, so the line table is walked once

    Cursor c(nodes, nodeLen);
    uint32_t depth = 0, condDepth = 0;
    try
    {
        while (!c.done())
        {
            Node n;
            n.offset = (uint32_t)c.pos();
            uint8_t op = c.U8();
            if (!IsKnownOpcode(op))
                throw Fail{StreamStatus::BadOpcode, n.offset, "opcode is not valid in a serialized stream"};
            n.type = (NodeType)op;
            if (out.nodes.size() >= x.limits.maxNodesPerStream || totalNodes >= x.limits.maxTotalNodes)
                throw Fail{StreamStatus::LimitExceeded, n.offset, "node count limit reached"};

            while (li + 1 < lineTable.size() && lineTable[li + 1].offset <= n.offset)
                li++;
            if (li < lineTable.size() && lineTable[li].offset <= n.offset)
            {
                n.line = (uint32_t)lineTable[li].line;
                n.column = (uint32_t)lineTable[li].column;
            }

            n.depth = depth;
            DecodeOperands(c, n, x, x.limits);

            if (ClosesScope(n.type))
            {
                if (depth == 0)
                    throw Fail{StreamStatus::BadScope, n.offset, "scope is closed but none is open"};
                depth--;
            }
            if (OpensScope(n.type))
            {
                if (depth >= x.limits.maxDepth)
                    throw Fail{StreamStatus::LimitExceeded, n.offset, "scope nesting is deeper than the limit"};
                depth++;
            }
            if (n.type == NodeType::BeginConditionalScope)
                condDepth++;
            if (n.type == NodeType::EndConditionalScope)
            {
                if (condDepth == 0)
                    throw Fail{StreamStatus::BadScope, n.offset, "conditional scope is closed but none is open"};
                condDepth--;
            }

            // A runtime data version this decoder does not know cannot be framed: the stream stops there.
            bool opaque = n.type == NodeType::SetCustomRuntimeData && !n.runtimeData;
            out.nodes.push_back(std::move(n));
            totalNodes++;
            if (opaque)
            {
                // The runtime-data payload that follows is not decoded here, so the rest of the stream
                // cannot be framed. This is a stop, not a malformed stream.
                out.status = StreamStatus::OpaqueData;
                out.errorOffset = (uint32_t)c.pos();
                out.message = "custom runtime data of an unsupported version; decoding of this stream stops here";
                out.finalDepth = depth;
                out.finalConditionalDepth = condDepth;
                return;
            }
        }
    }
    catch (const Fail &f)
    {
        out.status = f.status;
        out.errorOffset = f.offset;
        out.message = f.message;
        out.finalDepth = depth;
        out.finalConditionalDepth = condDepth;
        return;
    }
    out.finalDepth = depth;
    out.finalConditionalDepth = condDepth;
}

}  // namespace

void DecodeNodes(const uint8_t *data, size_t size, const File &file, const DecodeLimits &limits, Decoded &out)
{
    out = Decoded();
    const size_t n = file.subStreams.size();
    out.streams.resize(n);

    const uint64_t base = file.nodeStreamFileOffset + file.streamTableBytes;
    const uint64_t end = file.nodeStreamFileOffset + file.header.nodeStreamSize;
    uint64_t total = 0;

    for (size_t i = 0; i < n; i++)
    {
        DecodedStream &ds = out.streams[i];
        const SubStream &s = file.subStreams[i];
        uint64_t nodeStart = base + s.nodeOffset;
        uint64_t lineStart = base + s.lineOffset;
        uint64_t lineEnd = (i + 1 < n) ? base + file.subStreams[i + 1].nodeOffset : end;
        if (!(nodeStart <= lineStart && lineStart <= lineEnd && lineEnd <= end && end <= size))
        {
            ds.status = StreamStatus::BadRange;
            ds.message = "stream offsets are not ascending or run past the node stream";
            out.failedStreams++;
            continue;
        }
        Ctx x{file, limits, (uint32_t)n, (uint32_t)i};
        DecodeStream(data + nodeStart, (size_t)(lineStart - nodeStart), data + lineStart, (size_t)(lineEnd - lineStart), x,
                     total, ds);
        if (ds.status == StreamStatus::OpaqueData)
            out.opaqueStreams++;
        else if (ds.status != StreamStatus::Ok)
            out.failedStreams++;
        else if (ds.finalDepth != 0 || ds.finalConditionalDepth != 0)
        {
            char b[160];
            std::snprintf(b, sizeof b, "stream %zu ends with %u open scope(s) and %u open conditional scope(s)", i,
                          ds.finalDepth, ds.finalConditionalDepth);
            out.warnings.push_back(b);
        }
    }
}

// ---- text dump ------------------------------------------------------------------------------

namespace {

void AppendUtf8(std::string &o, const std::u16string &s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
        if (c < 0x20 && c != '\t')
        {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02x", c);
            o += b;
        }
        else if (c == '"' || c == '\\')
        {
            o += '\\';
            o += (char)c;
        }
        else if (c < 0x80)
            o += (char)c;
        else if (c < 0x800)
        {
            o += (char)(0xC0 | (c >> 6));
            o += (char)(0x80 | (c & 0x3F));
        }
        else if (c < 0x10000)
        {
            o += (char)(0xE0 | (c >> 12));
            o += (char)(0x80 | ((c >> 6) & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            o += (char)(0xF0 | (c >> 18));
            o += (char)(0x80 | ((c >> 12) & 0x3F));
            o += (char)(0x80 | ((c >> 6) & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
    }
}

std::string Str(const uint8_t *d, size_t sz, const File &f, uint32_t idx)
{
    std::string o;
    AppendUtf8(o, GetString(d, sz, f, idx));
    return o;
}

void Fmt(std::string &o, const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    o += b;
}

std::string TypeName(const uint8_t *d, size_t sz, const File &f, const Ref &r)
{
    if (r.trusted)
    {
        char b[32];
        std::snprintf(b, sizeof b, "stable-type#%u", r.id);
        return b;
    }
    const Type &t = f.types[r.id];
    std::string n = Str(d, sz, f, t.nameIndex);
    if (t.namespaceIndex < f.typeNamespaces.size())
    {
        std::string ns = Str(d, sz, f, f.typeNamespaces[t.namespaceIndex].nameIndex);
        if (!ns.empty())
            n = ns + "." + n;
    }
    return n;
}

std::string PropName(const uint8_t *d, size_t sz, const File &f, const Ref &r)
{
    if (r.trusted)
    {
        char b[32];
        std::snprintf(b, sizeof b, "stable-prop#%u", r.id);
        return b;
    }
    const Property &p = f.properties[r.id];
    std::string n = Str(d, sz, f, p.nameIndex);
    if (p.typeIndex < f.types.size())
        n = Str(d, sz, f, f.types[p.typeIndex].nameIndex) + "." + n;
    return n;
}

std::string ConstText(const uint8_t *d, size_t sz, const File &f, const Constant &k)
{
    std::string o;
    switch (k.kind)
    {
    case ConstKind::None: o = "none"; break;
    case ConstKind::BoolFalse: o = "false"; break;
    case ConstKind::BoolTrue: o = "true"; break;
    case ConstKind::NullString: o = "null"; break;
    case ConstKind::Float: Fmt(o, "%g", (double)k.real[0]); break;
    case ConstKind::Signed: Fmt(o, "%lld", (long long)k.integer); break;
    case ConstKind::Color: Fmt(o, "color #%08X", (unsigned)k.integer); break;
    case ConstKind::SharedString: o = "\"" + Str(d, sz, f, (uint32_t)k.integer) + "\""; break;
    case ConstKind::UniqueString:
        o = "\"";
        AppendUtf8(o, k.text);
        o += "\"";
        break;
    case ConstKind::Thickness: Fmt(o, "thickness(%g,%g,%g,%g)", (double)k.real[0], (double)k.real[1], (double)k.real[2], (double)k.real[3]); break;
    case ConstKind::GridLength: Fmt(o, "gridlength(%g, unit %d)", (double)k.real[0], k.gridUnit); break;
    case ConstKind::Enum: Fmt(o, "enum(stable-type#%u, %lld)", k.enumType, (long long)k.integer); break;
    }
    return o;
}

void DumpRuntimeData(std::string &o, const uint8_t *d, size_t sz, const File &f, const RuntimeData &r)
{
    auto S = [&](uint32_t id) { return "\"" + Str(d, sz, f, id) + "\""; };
    auto toks = [](const std::vector<uint32_t> &v) {
        std::string t = "[";
        for (size_t i = 0; i < v.size(); i++)
            t += (i ? "," : "") + std::to_string(v[i]);
        return t + "]";
    };
    Fmt(o, "           %s v%u (type %u)\n", RuntimeDataKindName(r.kind), r.version, r.typeIndex);
    for (const RdEntry &e : r.resources)
        Fmt(o, "             resource %s%s -> token %u\n", e.key.isType ? "[type] " : "", S(e.key.stringId).c_str(), e.token);
    for (const RdConditional &e : r.conditionalResources)
        Fmt(o, "             conditional resource %s%s -> tokens %s\n", e.key.isType ? "[type] " : "",
            S(e.key.stringId).c_str(), toks(e.tokens).c_str());
    for (uint16_t s : r.autoUndeferral)
        Fmt(o, "             auto-undeferral %s\n", S(s).c_str());
    for (uint16_t s : r.legacyImplicitTemplates)
        Fmt(o, "             legacy implicit template %s\n", S(s).c_str());
    for (uint16_t s : r.legacyImplicitStyleKeys)
        Fmt(o, "             legacy implicit style key %s\n", S(s).c_str());
    for (const SetterEssence &s : r.setters)
    {
        Fmt(o, "             setter flags=0x%02x", s.flags);
        if (s.property.present) o += " prop=" + PropName(d, sz, f, s.property);
        if (s.hasPropertyName) o += " name=" + S(s.propertyName) + " owner=" + TypeName(d, sz, f, s.ownerType);
        if (s.flags & 0x04) o += " string=" + S(s.stringValue);
        else if (s.flags & 0x20) o += " value=" + ConstText(d, sz, f, s.container);
        else if (s.flags & 0x4B) Fmt(o, " token=%u", s.token);
        o += "\n";
    }
    if (r.kind == RuntimeDataKind::VisualStateGroupCollection)
    {
        Fmt(o, "             state->group %s, entire-collection token %u, unexpected tokens %s\n",
            toks(r.visualStateToGroup).c_str(), r.entireCollectionToken, r.unexpectedTokensDetected ? "yes" : "no");
        for (const auto &v : r.visualStates)
            Fmt(o, "             state %s storyboard=%s token %u setters %s triggers %zu\n", S(v.name).c_str(),
                v.hasStoryboard ? "yes" : "no", v.storyboardToken, toks(v.propertySetterTokens).c_str(),
                v.triggerValues.size());
        for (const auto &g : r.visualStateGroups)
            Fmt(o, "             group %s dynamic-timelines=%s token %u\n", S(g.name).c_str(),
                g.hasDynamicTimelines ? "yes" : "no", g.selfToken);
        for (const auto &t : r.visualTransitions)
            Fmt(o, "             transition %s -> %s token %u\n", S(t.fromState).c_str(), S(t.toState).c_str(), t.token);
        for (uint16_t s : r.seenNameDirectives)
            Fmt(o, "             name directive %s\n", S(s).c_str());
    }
    if (r.kind == RuntimeDataKind::DeferredElement)
    {
        Fmt(o, "             element name %s realize=%s\n", S(r.elementName).c_str(), r.realize ? "yes" : "no");
        for (const auto &p : r.nonDeferredProperties)
            Fmt(o, "             property %s = %s\n", PropName(d, sz, f, p.first).c_str(), ConstText(d, sz, f, p.second).c_str());
    }
    for (const ConditionallyDeclared &cd : r.conditionallyDeclared)
    {
        Fmt(o, "             conditionally declared token %u:", cd.token);
        for (const PredicateRef &p : cd.predicates)
            o += " " + TypeName(d, sz, f, p.type) + "(" + Str(d, sz, f, p.argumentsString) + ")";
        o += "\n";
    }
}

}  // namespace

std::string DumpNodes(const uint8_t *d, size_t sz, const File &f, const Decoded &dec)
{
    std::string o;
    Fmt(o, "node streams: %zu (failed %zu, stopped at runtime data %zu)\n", dec.streams.size(), dec.failedStreams,
        dec.opaqueStreams);
    for (size_t i = 0; i < dec.streams.size(); i++)
    {
        const DecodedStream &s = dec.streams[i];
        Fmt(o, "stream [%zu]: %zu nodes, %u node bytes, %u line bytes, status %s", i, s.nodes.size(), s.nodeBytes,
            s.lineBytes, StreamStatusName(s.status));
        if (s.status != StreamStatus::Ok)
            Fmt(o, " at +%u: %s", s.errorOffset, s.message.c_str());
        o += "\n";
        for (const Node &n : s.nodes)
        {
            Fmt(o, "  +%-6u %u:%-4u ", n.offset, n.line, n.column);
            for (uint32_t k = 0; k < n.depth; k++)
                o += "  ";
            o += NodeTypeName(n.type);
            if (n.type1.present) o += " type=" + TypeName(d, sz, f, n.type1);
            if (n.property1.present) o += " prop=" + PropName(d, sz, f, n.property1);
            if (n.property2.present) o += " prop2=" + PropName(d, sz, f, n.property2);
            if (n.xmlNamespace.present)
            {
                const XmlNamespace &xn = f.xmlNamespaces[n.xmlNamespace.id];
                o += " xmlns:";
                AppendUtf8(o, n.text);
                o += "=\"" + Str(d, sz, f, xn.uriIndex) + "\"";
            }
            else if (!n.text.empty() || n.type == NodeType::CheckPeerType)
            {
                o += " text=\"";
                AppendUtf8(o, n.text);
                o += "\"";
            }
            if (n.constant.kind != ConstKind::None)
                o += (n.type == NodeType::BeginConditionalScope ? " args=" : " value=") + ConstText(d, sz, f, n.constant);
            if (n.type == NodeType::SetDeferredProperty || n.type == NodeType::SetCustomRuntimeData)
                Fmt(o, " stream=%u", n.targetStream);
            if (n.type == NodeType::SetDeferredProperty)
            {
                Fmt(o, " static=%u theme=%u", n.staticResourceCount, n.themeResourceCount);
                for (uint16_t sid : n.resourceStrings)
                    o += " \"" + Str(d, sz, f, sid) + "\"";
            }
            if (n.type == NodeType::SetCustomRuntimeData)
                Fmt(o, " runtime-data-type=%u", n.runtimeDataType);
            o += "\n";
            if (n.runtimeData)
                DumpRuntimeData(o, d, sz, f, *n.runtimeData);
        }
    }
    for (const std::string &w : dec.warnings)
        o += "warning: " + w + "\n";
    return o;
}

}  // namespace laml::xbf
