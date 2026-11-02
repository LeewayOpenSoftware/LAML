// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
#include "tree/logical_tree.h"

#include <algorithm>
#include <cstdio>

namespace laml::tree {

const char *ValueKindName(ValueKind kind)
{
    switch (kind)
    {
    case ValueKind::None: return "None";
    case ValueKind::Constant: return "Constant";
    case ValueKind::Object: return "Object";
    case ValueKind::StaticResource: return "StaticResource";
    case ValueKind::ThemeResource: return "ThemeResource";
    case ValueKind::TemplateBinding: return "TemplateBinding";
    case ValueKind::MarkupExtension: return "MarkupExtension";
    case ValueKind::ResolvedProperty: return "ResolvedProperty";
    case ValueKind::ResolvedType: return "ResolvedType";
    case ValueKind::Deferred: return "Deferred";
    case ValueKind::NotImplemented: return "NotImplemented";
    }
    return "?";
}

const char *DiagKindName(DiagKind kind)
{
    switch (kind)
    {
    case DiagKind::InvalidReference: return "InvalidReference";
    case DiagKind::UnknownType: return "UnknownType";
    case DiagKind::UnknownProperty: return "UnknownProperty";
    case DiagKind::NotImplemented: return "NotImplemented";
    case DiagKind::MalformedStructure: return "MalformedStructure";
    }
    return "?";
}

namespace {

struct Frame
{
    uint32_t instance = kNoNode;
    bool created = false;               // the instance was created in this frame (not fetched from a member)
    std::vector<LogicalValue> values;   // values waiting to be consumed by SetValue / AddTo*
};

struct ConditionalScope
{
    TypeResolution predicate;
};

class Builder
{
public:
    Builder(const xbf::Decoded &decoded, const RefResolver &resolver, const BuildLimits &limits, LogicalTree &out)
        : decoded_(decoded), resolver_(resolver), limits_(limits), out_(out)
    {
    }

    void Run()
    {
        out_.streams.resize(decoded_.streams.size());
        for (uint32_t s = 0; s < decoded_.streams.size(); s++)
            BuildStream(s);
    }

private:
    const xbf::Decoded &decoded_;
    const RefResolver &resolver_;
    const BuildLimits &limits_;
    LogicalTree &out_;

    uint32_t stream_ = 0;
    uint32_t nodeIndex_ = 0;
    const xbf::Node *cur_ = nullptr;
    std::vector<Frame> frames_;
    std::vector<ConditionalScope> conditionals_;

    SourcePos Pos() const
    {
        SourcePos p;
        p.stream = stream_;
        p.nodeIndex = nodeIndex_;
        if (cur_)
        {
            p.offset = cur_->offset;
            p.line = cur_->line;
            p.column = cur_->column;
        }
        return p;
    }

    void Diag(DiagKind kind, const std::string &message)
    {
        switch (kind)
        {
        case DiagKind::NotImplemented: out_.counts.notImplementedOps++; break;
        case DiagKind::MalformedStructure: out_.counts.structureErrors++; break;
        default: break;
        }
        if (out_.diagnostics.size() < limits_.maxDiagnostics)
        {
            Diagnostic d;
            d.kind = kind;
            d.message = message;
            d.pos = Pos();
            out_.diagnostics.push_back(std::move(d));
        }
    }

    TypeResolution Type(const xbf::Ref &ref)
    {
        TypeResolution r = resolver_.ResolveType(ref);
        if (r.retiredSlot)
            out_.counts.retiredStableRefs++;
        switch (r.status)
        {
        case RefStatus::Resolved: out_.counts.typesResolved++; break;
        case RefStatus::UnknownType:
            out_.counts.typesUnknown++;
            Diag(DiagKind::UnknownType, r.detail);
            break;
        case RefStatus::InvalidReference:
            out_.counts.typesInvalid++;
            Diag(DiagKind::InvalidReference, r.detail);
            break;
        case RefStatus::Unimplemented:
            out_.counts.typesUnimplemented++;
            Diag(DiagKind::NotImplemented, r.detail);
            break;
        case RefStatus::Directive: out_.counts.directives++; break;
        default: break;
        }
        return r;
    }

    PropertyResolution Property(const xbf::Ref &ref)
    {
        PropertyResolution r = resolver_.ResolveProperty(ref);
        if (r.retiredSlot)
            out_.counts.retiredStableRefs++;
        switch (r.status)
        {
        case RefStatus::Resolved: out_.counts.propertiesResolved++; break;
        case RefStatus::UnknownProperty:
        case RefStatus::UnknownType:    // the owner type of a stable property is missing: still a catalog gap
            out_.counts.propertiesUnknown++;
            Diag(r.status == RefStatus::UnknownType ? DiagKind::UnknownType : DiagKind::UnknownProperty, r.detail);
            break;
        case RefStatus::InvalidReference:
            out_.counts.propertiesInvalid++;
            Diag(DiagKind::InvalidReference, r.detail);
            break;
        case RefStatus::Unimplemented:
            out_.counts.propertiesUnimplemented++;
            Diag(DiagKind::NotImplemented, r.detail);
            break;
        case RefStatus::Directive: out_.counts.directives++; break;
        default: break;
        }
        return r;
    }

    LogicalNode &NewNode(const TypeResolution &type, bool created)
    {
        LogicalNode n;
        n.id = static_cast<uint32_t>(out_.nodes.size());
        n.stream = stream_;
        n.type = type;
        n.fromMember = !created;
        n.conditionalDepth = static_cast<uint32_t>(conditionals_.size());
        n.pos = Pos();
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
            if (it->instance != kNoNode) { n.parent = it->instance; break; }
        out_.nodes.push_back(std::move(n));
        if (created) out_.counts.objects++; else out_.counts.memberNodes++;
        return out_.nodes.back();
    }

    // The object that member operations apply to: the innermost frame that has one.
    LogicalNode *Target(const char *what)
    {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
            if (it->instance != kNoNode)
                return &out_.nodes[it->instance];
        if (stream_ > 0 && out_.nodes.size() < limits_.maxNodes)
        {
            // A deferred stream continues a scope of its parent stream: the object it acts on is not in this
            // stream. Stand in for it with an explicit placeholder; linking it to the parent is not done in M3.
            if (frames_.empty())
                frames_.emplace_back();
            TypeResolution none;
            LogicalNode &node = NewNode(none, true);
            node.external = true;
            out_.counts.objects--;
            out_.counts.externalContexts++;
            frames_.front().instance = node.id;
            frames_.front().created = false;
            out_.streams[stream_].roots.push_back(node.id);
            Diag(DiagKind::NotImplemented, std::string(what) + ": the object this deferred stream acts on is in another stream (not linked)");
            return &out_.nodes[node.id];
        }
        Diag(DiagKind::MalformedStructure, std::string(what) + ": there is no object in any open scope to apply it to");
        return nullptr;
    }

    Frame *Top(const char *what)
    {
        if (frames_.empty())
        {
            Diag(DiagKind::MalformedStructure, std::string(what) + ": no scope is open");
            return nullptr;
        }
        return &frames_.back();
    }

    bool PopValue(const char *what, LogicalValue &v)
    {
        Frame *f = Top(what);
        if (!f)
            return false;
        if (f->values.empty())
        {
            Diag(DiagKind::MalformedStructure, std::string(what) + ": the value it needs is not on the scope's value stack");
            return false;
        }
        v = std::move(f->values.back());
        f->values.pop_back();
        return true;
    }

    void PushFrame()
    {
        if (frames_.size() >= limits_.maxDepth)
        {
            Diag(DiagKind::MalformedStructure, "scope nesting deeper than the limit");
            return;
        }
        frames_.emplace_back();
    }

    void Create(const xbf::Node &n, bool withConstant, bool typeConverted)
    {
        TypeResolution t = Type(n.type1);
        if (out_.nodes.size() >= limits_.maxNodes)
        {
            Diag(DiagKind::MalformedStructure, "too many logical nodes");
            return;
        }
        if (frames_.empty())
            PushFrame();   // a stream may start without an opening scope (deferred streams)
        if (frames_.empty())
            return;
        Frame &f = frames_.back();
        LogicalNode &node = NewNode(t, true);
        if (withConstant)
        {
            node.hasInitialValue = true;
            node.initialValue = n.constant;
            node.initialTypeConverted = typeConverted;
        }
        if (f.instance != kNoNode)
        {
            // An object created while another is already the frame's object: the writer's own comments describe
            // this only before optimisation. Keep it as a pending value, and say so.
            LogicalValue v;
            v.kind = ValueKind::Object;
            v.object = node.id;
            f.values.push_back(std::move(v));
            Diag(DiagKind::NotImplemented, "an object is created inside a scope that already has one without opening a scope; kept as a pending value");
            return;
        }
        f.instance = node.id;
        f.created = true;
    }

    void ClosePop(bool provideValue)
    {
        if (frames_.empty())
        {
            Diag(DiagKind::MalformedStructure, "a scope is closed that was never opened");
            return;
        }
        Frame f = std::move(frames_.back());
        frames_.pop_back();
        if (!f.values.empty())
            Diag(DiagKind::MalformedStructure, std::to_string(f.values.size()) + " value(s) left unconsumed when the scope closed");
        if (f.instance != kNoNode && f.created)
        {
            LogicalValue v;
            v.kind = provideValue ? ValueKind::MarkupExtension : ValueKind::Object;
            v.object = f.instance;
            if (frames_.empty())
                out_.streams[stream_].roots.push_back(f.instance);
            else
                frames_.back().values.push_back(std::move(v));
        }
    }

    void SetOn(const xbf::Node &n, const char *what, LogicalValue value)
    {
        LogicalNode *target = Target(what);
        Assignment a;
        a.property = Property(n.property1);
        a.value = std::move(value);
        a.pos = Pos();
        if (!target)
            return;
        target->properties.push_back(std::move(a));
        out_.counts.assignments++;
    }

    void BuildStream(uint32_t s)
    {
        const xbf::DecodedStream &ds = decoded_.streams[s];
        stream_ = s;
        frames_.clear();
        conditionals_.clear();
        out_.streams[s].status = ds.status;

        for (nodeIndex_ = 0; nodeIndex_ < ds.nodes.size(); nodeIndex_++)
        {
            const xbf::Node &n = ds.nodes[nodeIndex_];
            cur_ = &n;
            Step(n);
        }
        cur_ = nullptr;

        StreamTree &st = out_.streams[s];
        st.unclosedFrames = static_cast<uint32_t>(frames_.size());
        for (const Frame &f : frames_)
            if (f.instance != kNoNode && f.created)
                st.roots.push_back(f.instance);
        frames_.clear();
    }

    void Step(const xbf::Node &n)
    {
        using xbf::NodeType;
        LogicalValue v;

        switch (n.type)
        {
        case NodeType::PushScope:
            PushFrame();
            break;
        case NodeType::PopScope:
            ClosePop(false);
            break;
        case NodeType::PushScopeAddNamespace:
            PushFrame();
            AddNamespace(n);
            break;
        case NodeType::AddNamespace:
            AddNamespace(n);
            break;
        case NodeType::CheckPeerType:
            out_.streams[stream_].peerType = n.text;
            out_.streams[stream_].hasPeerType = true;
            break;

        case NodeType::PushScopeCreateTypeBeginInit:
            PushFrame();
            Create(n, false, false);
            break;
        case NodeType::PushScopeCreateTypeWithConstantBeginInit:
            PushFrame();
            Create(n, true, false);
            break;
        case NodeType::PushScopeCreateTypeWithTypeConvertedConstantBeginInit:
            PushFrame();
            Create(n, true, true);
            break;
        case NodeType::CreateTypeBeginInit:
            Create(n, false, false);
            break;
        case NodeType::CreateTypeWithConstantBeginInit:
            Create(n, true, false);
            break;
        case NodeType::CreateTypeWithTypeConvertedConstantBeginInit:
            Create(n, true, true);
            break;

        case NodeType::PushScopeGetValue:
        {
            PropertyResolution p = Property(n.property1);
            LogicalNode *target = Target("PushScopeGetValue");
            uint32_t parent = target ? target->id : kNoNode;
            PushFrame();
            if (frames_.empty())
                break;
            TypeResolution t;
            LogicalNode &node = NewNode(t, false);
            node.member = p;
            node.parent = parent;
            frames_.back().instance = node.id;
            frames_.back().created = false;
            if (parent != kNoNode)
            {
                Assignment a;
                a.property = p;
                a.value.kind = ValueKind::Object;
                a.value.object = node.id;
                a.pos = Pos();
                out_.nodes[parent].properties.push_back(std::move(a));
                out_.counts.assignments++;
            }
            break;
        }

        case NodeType::EndInitPopScope:
            ClosePop(false);
            break;
        case NodeType::EndInitProvideValuePopScope:
            ClosePop(true);
            break;

        case NodeType::PushConstant:
        {
            Frame *f = Top("PushConstant");
            if (f)
            {
                v.kind = ValueKind::Constant;
                v.constant = n.constant;
                f->values.push_back(std::move(v));
            }
            break;
        }
        case NodeType::SetValue:
            if (PopValue("SetValue", v))
                SetOn(n, "SetValue", std::move(v));
            break;
        case NodeType::SetValueConstant:
            v.kind = ValueKind::Constant;
            v.constant = n.constant;
            SetOn(n, "SetValueConstant", std::move(v));
            break;
        case NodeType::SetValueTypeConvertedConstant:
            v.kind = ValueKind::Constant;
            v.constant = n.constant;
            v.typeConverted = true;
            SetOn(n, "SetValueTypeConvertedConstant", std::move(v));
            break;
        case NodeType::SetValueTypeConvertedResolvedProperty:
            v.kind = ValueKind::ResolvedProperty;
            v.property = Property(n.property2);
            v.typeConverted = true;
            SetOn(n, "SetValueTypeConvertedResolvedProperty", std::move(v));
            break;
        case NodeType::SetValueTypeConvertedResolvedType:
            v.kind = ValueKind::ResolvedType;
            v.type = Type(n.type1);
            v.typeConverted = true;
            SetOn(n, "SetValueTypeConvertedResolvedType", std::move(v));
            break;
        case NodeType::SetValueFromStaticResource:
            v.kind = ValueKind::StaticResource;
            v.constant = n.constant;
            SetOn(n, "SetValueFromStaticResource", std::move(v));
            break;
        case NodeType::SetValueFromThemeResource:
            v.kind = ValueKind::ThemeResource;
            v.constant = n.constant;
            SetOn(n, "SetValueFromThemeResource", std::move(v));
            break;
        case NodeType::SetValueFromTemplateBinding:
            v.kind = ValueKind::TemplateBinding;
            v.property = Property(n.property2);
            SetOn(n, "SetValueFromTemplateBinding", std::move(v));
            break;
        case NodeType::SetValueFromMarkupExtension:
            if (PopValue("SetValueFromMarkupExtension", v))
            {
                if (v.kind == ValueKind::Object)
                    v.kind = ValueKind::MarkupExtension;
                SetOn(n, "SetValueFromMarkupExtension", std::move(v));
            }
            break;

        case NodeType::ProvideStaticResourceValue:
        case NodeType::ProvideThemeResourceValue:
        {
            Frame *f = Top("ProvideResourceValue");
            if (f)
            {
                v.kind = n.type == NodeType::ProvideStaticResourceValue ? ValueKind::StaticResource : ValueKind::ThemeResource;
                v.constant = n.constant;
                f->values.push_back(std::move(v));
            }
            break;
        }

        case NodeType::AddToCollection:
        case NodeType::AddToDictionary:
        case NodeType::AddToDictionaryWithKey:
        {
            const char *what = n.type == NodeType::AddToCollection ? "AddToCollection" : "AddToDictionary";
            if (!PopValue(what, v))
                break;
            Frame *f = Top(what);
            LogicalNode *target = (f && f->instance != kNoNode) ? &out_.nodes[f->instance] : nullptr;
            if (!target)
            {
                Diag(DiagKind::MalformedStructure, std::string(what) + ": the scope has no collection to add to");
                break;
            }
            Item item;
            item.value = std::move(v);
            item.dictionary = n.type != NodeType::AddToCollection;
            if (n.type == NodeType::AddToDictionaryWithKey)
            {
                item.hasKey = true;
                item.key = n.constant;
            }
            item.pos = Pos();
            target->items.push_back(std::move(item));
            out_.counts.items++;
            break;
        }

        case NodeType::SetName:
            if (LogicalNode *t = Target("SetName"))
            {
                t->hasName = true;
                t->name = n.constant;
            }
            break;
        case NodeType::SetConnectionId:
            if (LogicalNode *t = Target("SetConnectionId"))
            {
                t->hasConnectionId = true;
                t->connectionId = n.constant;
            }
            break;

        case NodeType::SetDeferredProperty:
            v.kind = ValueKind::Deferred;
            v.targetStream = n.targetStream;
            SetOn(n, "SetDeferredProperty", std::move(v));
            break;
        case NodeType::SetCustomRuntimeData:
            if (LogicalNode *t = Target("SetCustomRuntimeData"))
            {
                std::string note = "runtime data";
                if (n.runtimeData)
                    note = std::string("runtime data ") + xbf::RuntimeDataKindName(n.runtimeData->kind) + " v" +
                           std::to_string(n.runtimeData->version);
                note += " in stream " + std::to_string(n.targetStream) + " (kept, not interpreted)";
                t->notes.push_back(note);
            }
            Diag(DiagKind::NotImplemented, "SetCustomRuntimeData: the payload is decoded by the XBF layer but not interpreted by the tree");
            break;
        case NodeType::GetResourcePropertyBag:
            if (LogicalNode *t = Target("GetResourcePropertyBag"))
                t->notes.push_back("GetResourcePropertyBag (not interpreted)");
            Diag(DiagKind::NotImplemented, "GetResourcePropertyBag is not interpreted");
            break;

        case NodeType::BeginConditionalScope:
        {
            ConditionalScope c;
            c.predicate = Type(n.type1);
            conditionals_.push_back(c);
            Diag(DiagKind::NotImplemented, "conditional XAML scope: predicates are not evaluated; the objects inside are kept and marked");
            break;
        }
        case NodeType::EndConditionalScope:
            if (conditionals_.empty())
                Diag(DiagKind::MalformedStructure, "EndConditionalScope without BeginConditionalScope");
            else
                conditionals_.pop_back();
            break;

        case NodeType::ProvideValue:
            Diag(DiagKind::NotImplemented, "ProvideValue is not interpreted");
            break;
        }
    }

    void AddNamespace(const xbf::Node &n)
    {
        Namespace ns;
        ns.prefix = n.text;
        ns.uri = resolver_.XmlNamespaceUri(n.xmlNamespace.id);
        ns.pos = Pos();
        out_.streams[stream_].namespaces.push_back(std::move(ns));
    }
};

std::string Utf8(const std::u16string &s)
{
    return types::Utf16ToUtf8(s);
}

std::string TypeText(const TypeResolution &t)
{
    std::string out = t.name.empty() ? std::string("<no name>") : t.name;
    out += " [";
    out += RefStatusName(t.status);
    out += t.stable ? ", stable#" : ", #";
    out += std::to_string(t.id) + "]";
    return out;
}

std::string PropText(const PropertyResolution &p)
{
    std::string out = p.ownerName.empty() ? p.name : p.ownerName + "." + p.name;
    if (out.empty())
        out = "<no name>";
    out += " [";
    out += RefStatusName(p.status);
    if (p.attached) out += ", attached";
    out += p.stable ? ", stable#" : ", #";
    out += std::to_string(p.id) + "]";
    return out;
}

std::string ConstText(const xbf::Constant &c)
{
    char buf[96];
    switch (c.kind)
    {
    case xbf::ConstKind::None: return "none";
    case xbf::ConstKind::BoolFalse: return "false";
    case xbf::ConstKind::BoolTrue: return "true";
    case xbf::ConstKind::Float: std::snprintf(buf, sizeof(buf), "float %g", c.real[0]); return buf;
    case xbf::ConstKind::Signed: std::snprintf(buf, sizeof(buf), "int %lld", static_cast<long long>(c.integer)); return buf;
    case xbf::ConstKind::SharedString: std::snprintf(buf, sizeof(buf), "string#%lld", static_cast<long long>(c.integer)); return buf;
    case xbf::ConstKind::Thickness: std::snprintf(buf, sizeof(buf), "thickness(%g,%g,%g,%g)", c.real[0], c.real[1], c.real[2], c.real[3]); return buf;
    case xbf::ConstKind::GridLength: std::snprintf(buf, sizeof(buf), "gridlength(%g, unit %d)", c.real[0], c.gridUnit); return buf;
    case xbf::ConstKind::Color: std::snprintf(buf, sizeof(buf), "color 0x%08llx", static_cast<unsigned long long>(c.integer)); return buf;
    case xbf::ConstKind::UniqueString: return "\"" + Utf8(c.text) + "\"";
    case xbf::ConstKind::NullString: return "null";
    case xbf::ConstKind::Enum: std::snprintf(buf, sizeof(buf), "enum(stable type#%u, %lld)", c.enumType, static_cast<long long>(c.integer)); return buf;
    }
    return "?";
}

void DumpNode(const LogicalTree &t, uint32_t id, int depth, std::string &out);

std::string ValueText(const LogicalTree &t, const LogicalValue &v, int depth, std::string &out)
{
    (void)t; (void)depth; (void)out;
    std::string s = ValueKindName(v.kind);
    switch (v.kind)
    {
    case ValueKind::Constant:
    case ValueKind::StaticResource:
    case ValueKind::ThemeResource:
        s += " " + ConstText(v.constant);
        break;
    case ValueKind::TemplateBinding:
    case ValueKind::ResolvedProperty:
        s += " " + PropText(v.property);
        break;
    case ValueKind::ResolvedType:
        s += " " + TypeText(v.type);
        break;
    case ValueKind::Deferred:
        s += " stream " + std::to_string(v.targetStream);
        break;
    default:
        break;
    }
    if (v.typeConverted) s += " (type converter named, not applied)";
    return s;
}

void DumpNode(const LogicalTree &t, uint32_t id, int depth, std::string &out)
{
    if (depth > 200 || id >= t.nodes.size())
    {
        out += std::string(depth * 2, ' ') + "...\n";
        return;
    }
    const LogicalNode &n = t.nodes[id];
    std::string ind(depth * 2, ' ');
    if (n.external)
        out += ind + "object of another stream (deferred stream context, not linked)\n";
    else if (n.fromMember)
        out += ind + "member value of " + PropText(n.member) + "\n";
    else
        out += ind + TypeText(n.type) + (n.hasName ? " name=" + ConstText(n.name) : std::string()) + "\n";
    for (const std::string &note : n.notes)
        out += ind + "  note: " + note + "\n";
    for (const Assignment &a : n.properties)
    {
        if (a.value.kind == ValueKind::Object && a.value.object < t.nodes.size() && t.nodes[a.value.object].fromMember)
            continue;   // shown below as the member node
        out += ind + "  " + PropText(a.property) + " = " + ValueText(t, a.value, depth, out) + "\n";
        if (a.value.kind == ValueKind::Object || a.value.kind == ValueKind::MarkupExtension)
            DumpNode(t, a.value.object, depth + 2, out);
    }
    for (const Assignment &a : n.properties)
        if (a.value.kind == ValueKind::Object && a.value.object < t.nodes.size() && t.nodes[a.value.object].fromMember)
        {
            out += ind + "  " + PropText(a.property) + " ->\n";
            DumpNode(t, a.value.object, depth + 2, out);
        }
    for (const Item &i : n.items)
    {
        out += ind + (i.dictionary ? "  + dict item" : "  + item");
        if (i.hasKey) out += " key=" + ConstText(i.key);
        out += ": " + ValueText(t, i.value, depth, out) + "\n";
        if (i.value.kind == ValueKind::Object || i.value.kind == ValueKind::MarkupExtension)
            DumpNode(t, i.value.object, depth + 2, out);
    }
}

}  // namespace

void BuildLogicalTree(const xbf::Decoded &decoded, const RefResolver &resolver, const BuildLimits &limits, LogicalTree &out)
{
    out = LogicalTree();
    Builder b(decoded, resolver, limits, out);
    b.Run();
}

std::string DumpLogicalTree(const LogicalTree &tree)
{
    std::string out;

    for (size_t s = 0; s < tree.streams.size(); s++)
    {
        const StreamTree &st = tree.streams[s];
        out += "stream " + std::to_string(s) + " (decoder " + xbf::StreamStatusName(st.status) + ", " +
               std::to_string(st.roots.size()) + " root(s), " + std::to_string(st.unclosedFrames) + " unclosed scope(s))\n";
        for (const Namespace &ns : st.namespaces)
            out += "  xmlns:" + Utf8(ns.prefix) + " = " + ns.uri + "\n";
        if (st.hasPeerType)
            out += "  peer type " + Utf8(st.peerType) + "\n";
        for (uint32_t r : st.roots)
            DumpNode(tree, r, 1, out);
    }
    return out;
}

}  // namespace laml::tree
