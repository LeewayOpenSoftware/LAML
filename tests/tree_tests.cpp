// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the reference resolver and the logical tree: synthetic node streams (positive, negative, limits),
// and the real MIT fixtures. Expectations come from the documented rules, not from upstream output.
// Usage: tree_tests <xbf-fixture-dir> <winui-metadata-dir> <seed-catalog-file>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../src/tree/logical_tree.h"

using namespace laml;
using namespace laml::tree;
using xbf::NodeType;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                          \
    do                                                                       \
    {                                                                        \
        g_checks++;                                                          \
        if (!(cond))                                                         \
        {                                                                    \
            g_failed++;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

static std::vector<uint8_t> Slurp(const std::string &path)
{
    std::vector<uint8_t> v;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    uint8_t b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}

static const char *kPres = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

struct Env
{
    types::Catalog catalog;
    types::StableTable stable;
    bool ok = false;
};

static bool LoadEnv(Env &e, const std::string &metaDir, const std::string &seedPath)
{
    std::vector<uint8_t> t = Slurp(metaDir + "/StableXbfTypeIndexes.g.csv");
    std::vector<uint8_t> p = Slurp(metaDir + "/StableXbfPropertyIndexes.g.csv");
    std::vector<uint8_t> s = Slurp(seedPath);
    if (t.empty() || p.empty() || s.empty()) return false;
    if (!e.stable.LoadTypes(reinterpret_cast<const char *>(t.data()), t.size()).ok()) return false;
    if (!e.stable.LoadProperties(reinterpret_cast<const char *>(p.data()), p.size()).ok()) return false;
    if (!types::LoadText(e.catalog, reinterpret_cast<const char *>(s.data()), s.size()).ok()) return false;
    if (!e.catalog.Resolve().empty()) return false;
    e.ok = true;
    return true;
}

static xbf::Ref STy(const Env &e, const char *name)
{
    xbf::Ref r;
    long i = e.stable.FindType(name);
    r.id = static_cast<uint16_t>(i < 0 ? 0x7ffe : i);
    r.trusted = true;
    r.present = true;
    return r;
}
static xbf::Ref SProp(const Env &e, const char *owner, const char *member)
{
    xbf::Ref r;
    long i = e.stable.FindProperty(owner, member);
    r.id = static_cast<uint16_t>(i < 0 ? 0x7ffe : i);
    r.trusted = true;
    r.present = true;
    return r;
}
static xbf::Ref Raw(uint16_t id, bool trusted)
{
    xbf::Ref r;
    r.id = id;
    r.trusted = trusted;
    r.present = true;
    return r;
}

static xbf::Constant Str(const char16_t *s)
{
    xbf::Constant c;
    c.kind = xbf::ConstKind::UniqueString;
    c.text = s;
    return c;
}
static xbf::Constant Int(int64_t v)
{
    xbf::Constant c;
    c.kind = xbf::ConstKind::Signed;
    c.integer = v;
    return c;
}

struct StreamBuilder
{
    xbf::DecodedStream s;
    uint32_t off = 0;
    xbf::Node &Add(NodeType t)
    {
        xbf::Node n;
        n.type = t;
        n.offset = off;
        off += 3;
        s.nodes.push_back(n);
        return s.nodes.back();
    }
    void PushScope() { Add(NodeType::PushScope); }
    void Create(NodeType t, xbf::Ref type) { Add(t).type1 = type; }
    void End() { Add(NodeType::EndInitPopScope); }
    void SetConst(xbf::Ref prop, xbf::Constant c)
    {
        xbf::Node &n = Add(NodeType::SetValueConstant);
        n.property1 = prop;
        n.constant = c;
    }
    void Prop(NodeType t, xbf::Ref prop) { Add(t).property1 = prop; }
};

static xbf::File EmptyFile(uint32_t major = 2, uint32_t minor = 1)
{
    xbf::File f;
    f.version.major = major;
    f.version.minor = minor;
    return f;
}

static xbf::Decoded One(StreamBuilder &b)
{
    xbf::Decoded d;
    d.streams.push_back(b.s);
    return d;
}

// ---------------------------------------------------------------------------------------------------
static void TestResolver(const Env &e)
{
    xbf::File file = EmptyFile();
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    CHECK(r.StableSupported());

    // absent
    CHECK(r.ResolveType(xbf::Ref()).status == RefStatus::Absent);
    CHECK(r.ResolveProperty(xbf::Ref()).status == RefStatus::Absent);

    // trusted types
    TypeResolution grid = r.ResolveType(STy(e, "Grid"));
    CHECK(grid.status == RefStatus::Resolved && grid.name == "Microsoft.UI.Xaml.Controls.Grid" && grid.stable);
    CHECK(grid.type == e.catalog.Find("Microsoft.UI.Xaml.Controls.Grid")->id);
    CHECK(r.ResolveType(STy(e, "Button")).status == RefStatus::Resolved);
    // a valid stable type the seed catalog does not describe: UnknownType, with the name kept
    TypeResolution known = r.ResolveType(STy(e, "Canvas"));
    CHECK(e.stable.FindType("Canvas") >= 0);
    CHECK(known.status == RefStatus::UnknownType && known.name == "Canvas");
    CHECK(known.detail.find("Canvas") != std::string::npos);
    // reference problems
    // a retired slot of the pinned table (slot 15 was DataPackage): resolved by its original name, and flagged
    TypeResolution retired = r.ResolveType(Raw(15, true));
    CHECK(retired.status == RefStatus::UnknownType && retired.retiredSlot && retired.name == "DataPackage");
    CHECK(retired.detail.find("retired") != std::string::npos);
    TypeResolution beyond = r.ResolveType(Raw(0x7ffe, true));
    CHECK(beyond.status == RefStatus::InvalidReference && beyond.detail.find("outside") != std::string::npos);
    CHECK(r.ResolveType(Raw(0, true)).status == RefStatus::InvalidReference);   // slot 0 is the Unknown entry
    // untrusted reference into an empty file
    CHECK(r.ResolveType(Raw(0, false)).status == RefStatus::InvalidReference);
    CHECK(r.ResolveProperty(Raw(3, false)).status == RefStatus::InvalidReference);

    // properties
    PropertyResolution w = r.ResolveProperty(SProp(e, "FrameworkElement", "Width"));
    CHECK(w.status == RefStatus::Resolved && w.name == "Width" && !w.attached && w.declaring == e.catalog.Find("Microsoft.UI.Xaml.FrameworkElement")->id);
    PropertyResolution row = r.ResolveProperty(SProp(e, "Grid", "Row"));
    CHECK(row.status == RefStatus::Resolved && row.attached);
    PropertyResolution tap = r.ResolveProperty(SProp(e, "UIElement", "IsTapEnabled"));
    CHECK(tap.status == RefStatus::UnknownProperty && tap.owner != types::kNoType);       // owner known, property not
    PropertyResolution orphan = r.ResolveProperty(SProp(e, "Canvas", "Left"));
    CHECK(orphan.status == RefStatus::UnknownType && orphan.owner == types::kNoType);     // owner not in the catalog
    PropertyResolution oldProp = r.ResolveProperty(Raw(104, true));                      // retired property slot
    CHECK(oldProp.retiredSlot && (oldProp.status == RefStatus::UnknownType || oldProp.status == RefStatus::InvalidReference));
    CHECK(r.ResolveProperty(Raw(0, true)).status == RefStatus::InvalidReference);       // slot 0 is the Unknown entry
    // an inherited property resolves through the base chain and reports the declaring type
    PropertyResolution ctl = r.ResolveProperty(SProp(e, "UIElement", "Opacity"));
    CHECK(ctl.status == RefStatus::Resolved);

    // no stable table: trusted references are Unimplemented, not invalid or unknown
    RefResolver nostable(nullptr, 0, file, e.catalog, nullptr);
    CHECK(!nostable.StableSupported() && !nostable.StableUnsupportedReason().empty());
    CHECK(nostable.ResolveType(STy(e, "Grid")).status == RefStatus::Unimplemented);
    CHECK(nostable.ResolveProperty(SProp(e, "Grid", "Row")).status == RefStatus::Unimplemented);
    CHECK(nostable.ResolveType(Raw(0, false)).status == RefStatus::InvalidReference);       // untrusted still works

    // format version outside 2.x
    xbf::File v3 = EmptyFile(3, 0);
    RefResolver other(nullptr, 0, v3, e.catalog, &e.stable);
    CHECK(!other.StableSupported());
    CHECK(other.ResolveType(STy(e, "Grid")).status == RefStatus::Unimplemented);
    xbf::File v20 = EmptyFile(2, 0);
    CHECK(RefResolver(nullptr, 0, v20, e.catalog, &e.stable).StableSupported());

    // catalog not resolved
    types::Catalog raw;
    CHECK(raw.AddType(types::TypeDecl{"A.B", types::Kind::Class, 0, "A.Missing", ""}).ok());
    CHECK(!raw.Resolve().empty());
    RefResolver unresolved(nullptr, 0, file, raw, &e.stable);
    CHECK(!unresolved.StableSupported());
    CHECK(unresolved.ResolveType(STy(e, "Grid")).status == RefStatus::Unimplemented);
    CHECK(unresolved.ResolveType(Raw(0, false)).status == RefStatus::Unimplemented);

    // a different default xmlns for stable names
    ResolverOptions opt;
    opt.stableXmlns = "urn:not-mapped";
    RefResolver wrong(nullptr, 0, file, e.catalog, &e.stable, opt);
    CHECK(wrong.ResolveType(STy(e, "Grid")).status == RefStatus::UnknownType);
}

// Page -> Grid { Children: [Button, Button(Width), TextBlock(Text)] } built the way real streams do it
static void BuildPage(const Env &e, StreamBuilder &b)
{
    xbf::Node &ns = b.Add(NodeType::PushScopeAddNamespace);
    ns.xmlNamespace.id = 0;
    ns.xmlNamespace.present = true;
    b.Add(NodeType::CheckPeerType).text = u"Test.MainPage";
    b.Create(NodeType::CreateTypeBeginInit, STy(e, "Page"));
    b.Add(NodeType::SetName).constant = Str(u"Root");
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    b.Prop(NodeType::PushScopeGetValue, SProp(e, "Panel", "Children"));
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    b.End();
    b.Add(NodeType::AddToCollection);
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    b.SetConst(SProp(e, "FrameworkElement", "Width"), Int(120));
    b.End();
    b.Add(NodeType::AddToCollection);
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "TextBlock"));
    b.Add(NodeType::PushConstant).constant = Str(u"hello");
    b.Prop(NodeType::SetValue, SProp(e, "TextBlock", "Text"));
    b.End();
    b.Add(NodeType::AddToCollection);
    b.Add(NodeType::PopScope);          // closes the Children frame
    b.End();                            // Grid
    b.Prop(NodeType::SetValue, SProp(e, "ContentControl", "Content"));   // consumes the Grid
    b.End();                            // Page
}

static void TestTreeStructure(const Env &e)
{
    StreamBuilder b;
    BuildPage(e, b);
    xbf::File file = EmptyFile();
    xbf::Decoded d = One(b);
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t;
    BuildLogicalTree(d, r, t);

    CHECK(t.streams.size() == 1 && t.streams[0].roots.size() == 1);
    CHECK(t.streams[0].unclosedFrames == 0);
    CHECK(t.streams[0].hasPeerType && t.streams[0].peerType == u"Test.MainPage");
    CHECK(t.streams[0].namespaces.size() == 1);
    CHECK(t.counts.objects == 5 && t.counts.memberNodes == 1);   // Page, Grid, 2 Buttons, TextBlock; Grid.Children is fetched
}

static void TestTreeShape(const Env &e)
{
    StreamBuilder b;
    BuildPage(e, b);
    xbf::File file = EmptyFile();
    xbf::Decoded d = One(b);
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    std::printf("%s", DumpLogicalTree(t).c_str());

    const LogicalNode &page = t.nodes[t.streams[0].roots[0]];
    CHECK(page.type.name == "Microsoft.UI.Xaml.Controls.Page" && page.hasName && page.name.text == u"Root");
    CHECK(page.properties.size() == 1 && page.properties[0].property.name == "Content");
    CHECK(page.properties[0].value.kind == ValueKind::Object);
    const LogicalNode &grid = t.nodes[page.properties[0].value.object];
    CHECK(grid.type.name == "Microsoft.UI.Xaml.Controls.Grid");
    CHECK(grid.parent == page.id);
    // Grid.Children is fetched, not created: a member node holding three items
    CHECK(grid.properties.size() == 1 && grid.properties[0].value.kind == ValueKind::Object);
    const LogicalNode &children = t.nodes[grid.properties[0].value.object];
    CHECK(children.fromMember && children.member.name == "Children" && children.parent == grid.id);
    CHECK(children.items.size() == 3 && !children.items[0].dictionary);
    const LogicalNode &b2 = t.nodes[children.items[1].value.object];
    CHECK(b2.type.name == "Microsoft.UI.Xaml.Controls.Button" && b2.properties.size() == 1);
    CHECK(b2.properties[0].property.name == "Width" && b2.properties[0].value.constant.integer == 120);
    const LogicalNode &tb = t.nodes[children.items[2].value.object];
    CHECK(tb.properties.size() == 1 && tb.properties[0].value.kind == ValueKind::Constant);
    CHECK(tb.properties[0].value.constant.text == u"hello");
    CHECK(t.ok());
    CHECK(t.counts.propertiesResolved >= 4 && t.counts.typesUnknown == 0);
    // each node is the value of at most one slot (a tree, not a graph)
    std::set<uint32_t> used;
    for (const LogicalNode &n : t.nodes)
    {
        for (const Assignment &a : n.properties)
            if (a.value.kind == ValueKind::Object) CHECK(used.insert(a.value.object).second);
        for (const Item &i : n.items)
            if (i.value.kind == ValueKind::Object) CHECK(used.insert(i.value.object).second);
    }
}

static void TestUnknownDoesNotBreakStructure(const Env &e)
{
    // The same stream against an empty catalog: every type and property is unknown, the shape is identical.
    StreamBuilder b;
    BuildPage(e, b);
    xbf::File file = EmptyFile();
    xbf::Decoded d = One(b);
    types::Catalog empty;
    CHECK(empty.Resolve().empty());
    RefResolver r(nullptr, 0, file, empty, &e.stable);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    RefResolver full(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t2;
    BuildLogicalTree(d, full, t2);
    CHECK(t.counts.objects == t2.counts.objects && t.counts.items == t2.counts.items &&
          t.counts.assignments == t2.counts.assignments && t.nodes.size() == t2.nodes.size());
    CHECK(t.counts.typesResolved == 0 && t.counts.typesUnknown == 5);
    CHECK(t.counts.propertiesResolved == 0 && t.counts.propertiesUnknown > 0);
    CHECK(t.ok());   // unknown is a catalog gap, not a structural error
    bool sawUnknownType = false, sawUnknownProp = true, sawInvalid = false;   // stable properties of unknown owners report UnknownType
    for (const Diagnostic &x : t.diagnostics)
    {
        if (x.kind == DiagKind::UnknownType) sawUnknownType = true;
        if (x.kind == DiagKind::UnknownProperty) sawUnknownProp = true;
        if (x.kind == DiagKind::InvalidReference) sawInvalid = true;
    }
    CHECK(sawUnknownType && sawUnknownProp && !sawInvalid);
}

static void TestInvalidReferences(const Env &e)
{
    StreamBuilder b;
    b.Create(NodeType::PushScopeCreateTypeBeginInit, Raw(0, true));        // the table's Unknown entry
    b.SetConst(Raw(0x7ffe, true), Int(1));                                  // beyond the table
    b.End();
    xbf::File file = EmptyFile();
    xbf::Decoded d = One(b);
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    CHECK(t.counts.typesInvalid == 1 && t.counts.propertiesInvalid == 1);
    CHECK(t.counts.typesUnknown == 0 && t.counts.notImplementedOps == 0);
    CHECK(t.ok());      // an invalid reference is reported as such; the stack discipline is intact
    CHECK(t.streams[0].roots.size() == 1);
    int invalid = 0;
    for (const Diagnostic &x : t.diagnostics) invalid += x.kind == DiagKind::InvalidReference;
    CHECK(invalid == 2);
}

static void TestNotImplemented(const Env &e)
{
    xbf::File file = EmptyFile();
    // no stable table: everything trusted is "not implemented", not "unknown"
    StreamBuilder b;
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    b.End();
    xbf::Decoded d = One(b);
    RefResolver r(nullptr, 0, file, e.catalog, nullptr);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    CHECK(t.counts.typesUnimplemented == 1 && t.counts.typesUnknown == 0 && t.counts.typesInvalid == 0);
    CHECK(t.counts.notImplementedOps == 1 && t.ok());

    // operations that are kept but not interpreted
    StreamBuilder c;
    c.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    c.Add(NodeType::GetResourcePropertyBag).constant = Str(u"key");
    xbf::Node &rd = c.Add(NodeType::SetCustomRuntimeData);
    rd.targetStream = 0;
    c.Add(NodeType::BeginConditionalScope).type1 = STy(e, "Button");
    c.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    c.End();
    c.Prop(NodeType::SetValue, SProp(e, "ContentControl", "Content"));       // consume the Button
    c.Add(NodeType::EndConditionalScope);
    c.Add(NodeType::ProvideValue);
    c.End();
    xbf::Decoded d2 = One(c);
    RefResolver r2(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t2;
    BuildLogicalTree(d2, r2, t2);
    const LogicalNode &grid = t2.nodes[t2.streams[0].roots[0]];
    CHECK(grid.notes.size() == 2);
    CHECK(t2.counts.notImplementedOps == 4);          // property bag, runtime data, conditional scope, ProvideValue
    CHECK(t2.nodes.size() == 2 && t2.nodes[1].conditionalDepth == 1 && grid.conditionalDepth == 0);
    // the runtime data above names stream 0 from stream 0: since M4 a reference must point to a later stream
    CHECK(t2.counts.structureErrors == 1 && !t2.ok());

    // deferred property keeps the target stream
    StreamBuilder dp;
    dp.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    xbf::Node &def = dp.Add(NodeType::SetDeferredProperty);
    def.property1 = SProp(e, "ContentControl", "Content");
    def.targetStream = 1;
    dp.End();
    xbf::Decoded d3 = One(dp);
    LogicalTree t3;
    BuildLogicalTree(d3, r2, t3);
    CHECK(t3.nodes[0].properties.size() == 1 && t3.nodes[0].properties[0].value.kind == ValueKind::Deferred &&
          t3.nodes[0].properties[0].value.targetStream == 1);
}

static void TestValueForms(const Env &e)
{
    xbf::File file = EmptyFile();
    StreamBuilder b;
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    b.SetConst(SProp(e, "FrameworkElement", "Width"), Int(5));
    {
        xbf::Node &n = b.Add(NodeType::SetValueTypeConvertedConstant);
        n.property1 = SProp(e, "FrameworkElement", "Margin");
        n.constant = Str(u"1,2,3,4");
    }
    {
        xbf::Node &n = b.Add(NodeType::SetValueFromStaticResource);
        n.property1 = SProp(e, "Control", "Background");
        n.constant = Str(u"BrushKey");
    }
    {
        xbf::Node &n = b.Add(NodeType::SetValueFromThemeResource);
        n.property1 = SProp(e, "Control", "Foreground");
        n.constant = Str(u"ThemeKey");
    }
    {
        xbf::Node &n = b.Add(NodeType::SetValueFromTemplateBinding);
        n.property1 = SProp(e, "FrameworkElement", "Height");
        n.property2 = SProp(e, "FrameworkElement", "Width");
    }
    {
        xbf::Node &n = b.Add(NodeType::SetValueTypeConvertedResolvedType);
        n.property1 = SProp(e, "FrameworkElement", "Tag");
        n.type1 = STy(e, "Grid");
    }
    b.Add(NodeType::SetConnectionId).constant = Int(7);
    b.End();
    xbf::Decoded d = One(b);
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    const LogicalNode &n = t.nodes[0];
    CHECK(n.properties.size() == 6);
    CHECK(n.properties[1].value.typeConverted && n.properties[1].value.kind == ValueKind::Constant);
    CHECK(n.properties[2].value.kind == ValueKind::StaticResource && n.properties[2].value.constant.text == u"BrushKey");
    CHECK(n.properties[3].value.kind == ValueKind::ThemeResource);
    CHECK(n.properties[4].value.kind == ValueKind::TemplateBinding && n.properties[4].value.property.name == "Width");
    CHECK(n.properties[5].value.kind == ValueKind::ResolvedType && n.properties[5].value.type.status == RefStatus::Resolved);
    CHECK(n.hasConnectionId && n.connectionId.integer == 7);
    CHECK(t.ok());

    // dictionary items, with and without key; initial value
    StreamBuilder c;
    c.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    c.Prop(NodeType::PushScopeGetValue, SProp(e, "Panel", "Children"));
    {
        xbf::Node &n2 = c.Add(NodeType::PushScopeCreateTypeWithConstantBeginInit);
        n2.type1 = STy(e, "TextBlock");
        n2.constant = Str(u"init");
    }
    c.End();
    c.Add(NodeType::AddToDictionaryWithKey).constant = Str(u"k1");
    c.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    c.End();
    c.Add(NodeType::AddToDictionary);
    c.Add(NodeType::PopScope);
    c.End();
    xbf::Decoded d2 = One(c);
    LogicalTree t2;
    BuildLogicalTree(d2, r, t2);
    const LogicalNode *coll = nullptr;
    for (const LogicalNode &x : t2.nodes) if (x.fromMember) coll = &x;
    CHECK(coll && coll->items.size() == 2 && coll->items[0].dictionary && coll->items[0].hasKey &&
          coll->items[0].key.text == u"k1" && !coll->items[1].hasKey);
    CHECK(t2.nodes[coll->items[0].value.object].hasInitialValue &&
          t2.nodes[coll->items[0].value.object].initialValue.text == u"init");
    CHECK(t2.ok());
}

static void TestMalformedStructure(const Env &e)
{
    xbf::File file = EmptyFile();
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);

    // SetValue with no value on the stack
    {
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        b.Prop(NodeType::SetValue, SProp(e, "FrameworkElement", "Width"));
        b.End();
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(!t.ok() && t.counts.structureErrors == 1 && t.nodes[0].properties.empty());
    }
    // AddToCollection with nothing to add, and with nothing to add to
    {
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        b.Add(NodeType::AddToCollection);
        b.End();
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(!t.ok() && t.counts.structureErrors == 1);
        StreamBuilder c;
        c.PushScope();
        c.Add(NodeType::PushConstant).constant = Int(1);
        c.Add(NodeType::AddToCollection);
        c.Add(NodeType::PopScope);
        xbf::Decoded d2 = One(c);
        LogicalTree t2;
        BuildLogicalTree(d2, r, t2);
        CHECK(!t2.ok() && t2.counts.structureErrors >= 1);
    }
    // member operations with no object anywhere
    {
        StreamBuilder b;
        b.PushScope();
        b.SetConst(SProp(e, "FrameworkElement", "Width"), Int(1));
        b.Add(NodeType::SetName).constant = Str(u"n");
        b.Add(NodeType::SetConnectionId).constant = Int(2);
        b.Add(NodeType::PopScope);
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(!t.ok() && t.counts.structureErrors == 3 && t.nodes.empty());
    }
    // a scope closed that was never opened (the decoder rejects this, the builder does not trust it)
    {
        StreamBuilder b;
        b.Add(NodeType::PopScope);
        b.Add(NodeType::EndInitPopScope);
        b.Add(NodeType::EndConditionalScope);
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(!t.ok() && t.counts.structureErrors == 3);
    }
    // values left on the stack when a scope closes; scope left open at the end of the stream
    {
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        b.Add(NodeType::PushConstant).constant = Int(9);
        b.End();
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));    // never closed
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(!t.ok() && t.counts.structureErrors == 1);
        CHECK(t.streams[0].unclosedFrames == 1 && t.streams[0].roots.size() == 2);
    }
    // an object created in a scope that already has one
    {
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        b.Create(NodeType::CreateTypeBeginInit, STy(e, "Button"));
        b.End();
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(t.counts.objects == 2 && t.counts.notImplementedOps == 1);
    }
    // a stream that starts without an opening scope (deferred streams do)
    {
        StreamBuilder b;
        b.Create(NodeType::CreateTypeBeginInit, STy(e, "Button"));
        b.End();
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(t.ok() && t.streams[0].roots.size() == 1);
    }
}

static void TestLimitsAndEmpty(const Env &e)
{
    xbf::File file = EmptyFile();
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);

    // empty decoded set, empty stream
    {
        xbf::Decoded d;
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(t.streams.empty() && t.nodes.empty() && t.ok());
        d.streams.emplace_back();
        BuildLogicalTree(d, r, t);
        CHECK(t.streams.size() == 1 && t.streams[0].roots.empty() && t.ok());
    }
    // depth limit
    {
        StreamBuilder b;
        for (int i = 0; i < 10; i++) b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        xbf::Decoded d = One(b);
        BuildLimits lim;
        lim.maxDepth = 4;
        LogicalTree t;
        BuildLogicalTree(d, r, lim, t);
        CHECK(!t.ok());
        CHECK(t.streams[0].unclosedFrames <= 4);
    }
    // node limit
    {
        StreamBuilder b;
        for (int i = 0; i < 20; i++)
        {
            b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
            b.End();
        }
        xbf::Decoded d = One(b);
        BuildLimits lim;
        lim.maxNodes = 5;
        LogicalTree t;
        BuildLogicalTree(d, r, lim, t);
        CHECK(t.nodes.size() == 5 && !t.ok());
    }
    // diagnostics are capped but counted
    {
        types::Catalog none;
        CHECK(none.Resolve().empty());
        RefResolver unk(nullptr, 0, file, none, &e.stable);
        StreamBuilder b;
        for (int i = 0; i < 50; i++)
        {
            b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
            b.End();
        }
        xbf::Decoded d = One(b);
        BuildLimits lim;
        lim.maxDiagnostics = 10;
        LogicalTree t;
        BuildLogicalTree(d, unk, lim, t);
        CHECK(t.diagnostics.size() == 10 && t.counts.typesUnknown == 50);
    }
    // a stream the decoder could not finish keeps its status and still yields what was decoded
    {
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        b.End();
        b.s.status = xbf::StreamStatus::Truncated;
        xbf::Decoded d = One(b);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(t.streams[0].status == xbf::StreamStatus::Truncated && t.streams[0].roots.size() == 1);
    }
    // two streams do not share frames or roots
    {
        StreamBuilder a, c;
        a.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));      // left open
        c.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        c.End();
        xbf::Decoded d;
        d.streams.push_back(a.s);
        d.streams.push_back(c.s);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        CHECK(t.streams.size() == 2 && t.streams[0].unclosedFrames == 1 && t.streams[1].unclosedFrames == 0);
        CHECK(t.streams[1].roots.size() == 1 && t.nodes[t.streams[1].roots[0]].stream == 1);
    }
}

// ---------------------------------------------------------------------------------------------------
struct Loaded
{
    std::vector<uint8_t> bytes;
    xbf::File file;
    xbf::Decoded decoded;
    bool ok = false;
};

static bool OpenFixture(Loaded &l, const std::string &dir, const char *name)
{
    l.bytes = Slurp(dir + "/" + name);
    if (l.bytes.empty()) return false;
    if (!xbf::Parse(l.bytes.data(), l.bytes.size(), l.file).ok()) return false;
    xbf::DecodeNodes(l.bytes.data(), l.bytes.size(), l.file, l.decoded);
    l.ok = true;
    return true;
}

static void CheckTreeInvariants(const LogicalTree &t)
{
    std::set<uint32_t> used;
    for (const LogicalNode &n : t.nodes)
    {
        CHECK(n.id < t.nodes.size() && &t.nodes[n.id] == &n);
        CHECK(n.parent == kNoNode || n.parent < t.nodes.size());
        for (const Assignment &a : n.properties)
            if (a.value.kind == ValueKind::Object || a.value.kind == ValueKind::MarkupExtension)
            {
                CHECK(a.value.object < t.nodes.size());
                CHECK(used.insert(a.value.object).second);
            }
        for (const Item &i : n.items)
            if (i.value.kind == ValueKind::Object || i.value.kind == ValueKind::MarkupExtension)
            {
                CHECK(i.value.object < t.nodes.size());
                CHECK(used.insert(i.value.object).second);
            }
    }
    for (const StreamTree &s : t.streams)
        for (uint32_t r : s.roots)
        {
            CHECK(r < t.nodes.size());
            CHECK(used.find(r) == used.end());      // a root is nobody's value
        }
}

static void TestFixtures(const Env &e, const std::string &dir)
{
    static const char *names[] = {"SampleXbf2.xbf", "CustomTypesPage.xbf", "XbfVsmTest.v2.1.xbf", "PageWithConditionalXaml.xbf",
                                  "SampleWithChecksum.xbf"};
    types::Catalog empty;
    CHECK(empty.Resolve().empty());

    for (const char *name : names)
    {
        Loaded l;
        CHECK(OpenFixture(l, dir, name));
        if (!l.ok) continue;
        RefResolver r(l.bytes.data(), l.bytes.size(), l.file, e.catalog, &e.stable);
        LogicalTree t;
        BuildLogicalTree(l.decoded, r, t);
        CheckTreeInvariants(t);
        for (const Diagnostic &x : t.diagnostics)
            if (x.kind == DiagKind::InvalidReference || x.kind == DiagKind::MalformedStructure)
                std::printf("      %s (%s) stream %u node %u offset %u: %s\n", name, DiagKindName(x.kind), x.pos.stream,
                            x.pos.nodeIndex, x.pos.offset, x.message.c_str());
        std::printf("   %-28s streams=%zu objects=%zu members=%zu items=%zu assignments=%zu types r/u/i/n=%zu/%zu/%zu/%zu "
                    "props r/u/i/n=%zu/%zu/%zu/%zu notimpl=%zu structure=%zu\n",
                    name, t.streams.size(), t.counts.objects, t.counts.memberNodes, t.counts.items, t.counts.assignments,
                    t.counts.typesResolved, t.counts.typesUnknown, t.counts.typesInvalid, t.counts.typesUnimplemented,
                    t.counts.propertiesResolved, t.counts.propertiesUnknown, t.counts.propertiesInvalid,
                    t.counts.propertiesUnimplemented, t.counts.notImplementedOps, t.counts.structureErrors);
        // the shape does not depend on the metadata
        RefResolver r2(l.bytes.data(), l.bytes.size(), l.file, empty, &e.stable);
        LogicalTree t2;
        BuildLogicalTree(l.decoded, r2, t2);
        CHECK(t.nodes.size() == t2.nodes.size() && t.counts.items == t2.counts.items &&
              t.counts.assignments == t2.counts.assignments && t.counts.structureErrors == t2.counts.structureErrors);
        // the shape does not depend on having a stable table either (only the outcome of trusted references does)
        RefResolver r3(l.bytes.data(), l.bytes.size(), l.file, e.catalog, nullptr);
        LogicalTree t3;
        BuildLogicalTree(l.decoded, r3, t3);
        CHECK(t.nodes.size() == t3.nodes.size() && t.counts.items == t3.counts.items);
        // no reference in a real file is invalid, and the stack discipline of every stream holds
        CHECK(t.counts.typesInvalid == 0 && t.counts.propertiesInvalid == 0);
        CHECK(t.counts.structureErrors == 0 && t.ok());
    }

    // SampleXbf2: the representative page
    Loaded l;
    CHECK(OpenFixture(l, dir, "SampleXbf2.xbf"));
    RefResolver r(l.bytes.data(), l.bytes.size(), l.file, e.catalog, &e.stable);
    LogicalTree t;
    BuildLogicalTree(l.decoded, r, t);
    CHECK(t.ok());
    CHECK(t.streams.size() == 1 && t.streams[0].status == xbf::StreamStatus::Ok);
    CHECK(t.streams[0].roots.size() == 1);
    CHECK(t.streams[0].namespaces.size() == 8);
    CHECK(t.streams[0].namespaces[1].uri == "http://schemas.microsoft.com/winfx/2006/xaml");
    CHECK(t.streams[0].hasPeerType && t.streams[0].peerType == u"Numbers.CalculatorScientificOperators");
    const LogicalNode &root = t.nodes[t.streams[0].roots[0]];
    CHECK(root.hasName);
    CHECK(t.counts.objects == 67 && t.counts.memberNodes == 4 && t.counts.items == 40 && t.counts.assignments == 216);
    CHECK(t.counts.typesUnknown > 0);                // the seed catalog is small: honest unknowns
    CHECK(t.counts.typesResolved > 0);               // ... but Grid and friends resolve
    CHECK(t.counts.structureErrors == 0);

    // XBF 2.1 resolver sees format version 2.1; a copy of the file claiming 3.0 loses stable resolution
    xbf::File v3 = l.file;
    v3.version.major = 3;
    RefResolver r3(l.bytes.data(), l.bytes.size(), v3, e.catalog, &e.stable);
    LogicalTree t3;
    BuildLogicalTree(l.decoded, r3, t3);
    CHECK(t3.counts.typesResolved < t.counts.typesResolved && t3.counts.typesUnimplemented > 0);
    CHECK(t3.nodes.size() == t.nodes.size());

    // CustomTypesPage: an Unknown-flagged custom type in the file's own type table
    Loaded c;
    CHECK(OpenFixture(c, dir, "CustomTypesPage.xbf"));
    RefResolver rc(c.bytes.data(), c.bytes.size(), c.file, e.catalog, &e.stable);
    LogicalTree tc;
    BuildLogicalTree(c.decoded, rc, tc);
    CHECK(tc.ok() && tc.nodes.size() > 0);
    CHECK(tc.counts.retiredStableRefs == 2);         // ProgressBar and one of its properties: retired slots of the pinned table

    // XbfVsmTest: three deferred streams start inside a scope of their parent: explicit external contexts
    Loaded v;
    CHECK(OpenFixture(v, dir, "XbfVsmTest.v2.1.xbf"));
    RefResolver rv(v.bytes.data(), v.bytes.size(), v.file, e.catalog, &e.stable);
    LogicalTree tv;
    BuildLogicalTree(v.decoded, rv, tv);
    CHECK(tv.ok() && tv.streams.size() == 7 && tv.counts.externalContexts == 3);
    int externals = 0;
    for (const LogicalNode &n : tv.nodes) externals += n.external;
    CHECK(externals == 3);
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 4)
    {
        std::printf("usage: tree_tests <xbf-fixture-dir> <winui-metadata-dir> <seed-catalog>\n");
        return 2;
    }
    Env e;
    if (!LoadEnv(e, argv[2], argv[3]))
    {
        std::printf("cannot load the stable tables or the seed catalog\n");
        return 2;
    }
    TestResolver(e);
    TestTreeStructure(e);
    TestTreeShape(e);
    TestUnknownDoesNotBreakStructure(e);
    TestInvalidReferences(e);
    TestNotImplemented(e);
    TestValueForms(e);
    TestMalformedStructure(e);
    TestLimitsAndEmpty(e);
    TestFixtures(e, argv[1]);
    std::printf("tree_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
