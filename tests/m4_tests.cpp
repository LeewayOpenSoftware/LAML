// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// M4 tests: catalog sources and enum numbers, the extracted WinUI catalog, deferred-stream links, tree
// validation (format errors / unknown references / not implemented kept apart), the SampleXbf2 regression
// metric, and malformed-input safety. Expectations come from the documented rules, not from upstream output.
// Usage: m4_tests <xbf-fixture-dir> <winui-metadata-dir> <winui-api.types> <seed.types>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../src/tree/logical_tree.h"
#include "../src/tree/validate.h"

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

static bool LoadEnv(Env &e, const std::string &metaDir, const std::string &typesPath)
{
    std::vector<uint8_t> t = Slurp(metaDir + "/StableXbfTypeIndexes.g.csv");
    std::vector<uint8_t> p = Slurp(metaDir + "/StableXbfPropertyIndexes.g.csv");
    std::vector<uint8_t> s = Slurp(typesPath);
    if (t.empty() || p.empty() || s.empty()) return false;
    if (!e.stable.LoadTypes(reinterpret_cast<const char *>(t.data()), t.size()).ok()) return false;
    if (!e.stable.LoadProperties(reinterpret_cast<const char *>(p.data()), p.size()).ok()) return false;
    types::Result r = types::LoadText(e.catalog, reinterpret_cast<const char *>(s.data()), s.size());
    if (!r.ok())
    {
        std::printf("catalog %s line %zu: %s\n", typesPath.c_str(), r.line, r.message.c_str());
        return false;
    }
    std::vector<types::Problem> pr = e.catalog.Resolve();
    for (const types::Problem &x : pr) std::printf("catalog problem: %s\n", x.message.c_str());
    if (!pr.empty()) return false;
    e.ok = true;
    return true;
}

static xbf::Ref STy(const Env &e, const char *name)
{
    xbf::Ref r;
    long i = e.stable.FindType(name);
    CHECK(i >= 0);
    r.id = static_cast<uint16_t>(i < 0 ? 0x7ffe : i);
    r.trusted = true;
    r.present = true;
    return r;
}
static xbf::Ref SProp(const Env &e, const char *owner, const char *member)
{
    xbf::Ref r;
    long i = e.stable.FindProperty(owner, member);
    CHECK(i >= 0);
    r.id = static_cast<uint16_t>(i < 0 ? 0x7ffe : i);
    r.trusted = true;
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
static xbf::Constant Kind(xbf::ConstKind k)
{
    xbf::Constant c;
    c.kind = k;
    return c;
}
static xbf::Constant Flt(float v)
{
    xbf::Constant c;
    c.kind = xbf::ConstKind::Float;
    c.real[0] = v;
    return c;
}
static xbf::Constant EnumC(const Env &e, const char *enumName, int64_t v)
{
    xbf::Constant c;
    c.kind = xbf::ConstKind::Enum;
    long i = e.stable.FindType(enumName);
    CHECK(i >= 0);
    c.enumType = static_cast<uint16_t>(i < 0 ? 0 : i);
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

static xbf::File EmptyFile()
{
    xbf::File f;
    f.version.major = 2;
    f.version.minor = 1;
    return f;
}

// ---------------------------------------------------------------------------------------------------
// Catalog: sources, enum numbers, flags
static types::Result Load(types::Catalog &c, const std::string &text)
{
    return types::LoadText(c, text.data(), text.size());
}

static void TestCatalogSourcesAndEnums()
{
    {
        types::Catalog c;
        std::string t =
            "laml-types 1\n"
            "source doc https://example.org/idl abc123 MIT\n"
            "type A.Color kind=enum src=doc\n"
            "enumval A.Color Red=0\n"
            "enumval A.Color Green=1\n"
            "enumval A.Color Blue=0x10\n"
            "enumval A.Color Neg=-4\n"
            "type A.Mode kind=enum flags src=doc\n"
            "enumval A.Mode Bold=0x1\n"
            "enumval A.Mode Italic=0x2\n"
            "type A.Plain kind=enum\n"
            "enumval A.Plain X\n"
            "type A.Holder src=doc\n"
            "prop A.Holder.Opt A.Color nullable\n";
        types::Result r = Load(c, t);
        CHECK(r.ok());
        CHECK(c.Resolve().empty());
        const types::TypeInfo *col = c.Find("A.Color");
        CHECK(col && col->source == "doc" && col->enumNumbers.size() == 4 && col->enumNumbers[2] == 16 && col->enumNumbers[3] == -4);
        CHECK(c.FindSource("doc") && c.FindSource("doc")->license == "MIT" && c.FindSource("nope") == nullptr);
        CHECK(c.EnumIsNumbered(col->id) && c.EnumAccepts(col->id, 1) && c.EnumAccepts(col->id, 16) && !c.EnumAccepts(col->id, 2));
        CHECK(!c.EnumAccepts(col->id, 17) && !c.EnumAccepts(col->id, -1) && c.EnumAccepts(col->id, -4));
        CHECK(c.EnumNameOf(col->id, 1) && *c.EnumNameOf(col->id, 1) == "Green" && c.EnumNameOf(col->id, 2) == nullptr);
        const types::TypeInfo *flags = c.Find("A.Mode");
        CHECK(flags && (flags->flags & types::kTypeFlags));
        // a flags enum takes any OR of its values (and zero); a plain one does not
        CHECK(c.EnumAccepts(flags->id, 3) && c.EnumAccepts(flags->id, 0) && c.EnumAccepts(flags->id, 1) && !c.EnumAccepts(flags->id, 8));
        CHECK(!c.EnumAccepts(col->id, 17));                    // not a flags enum: 16|1 is not a member
        const types::TypeInfo *plain = c.Find("A.Plain");
        CHECK(plain && !c.EnumIsNumbered(plain->id) && !c.EnumAccepts(plain->id, 0));
        const types::Property *opt = c.FindProperty(c.Find("A.Holder")->id, "Opt");
        CHECK(opt && (opt->flags & types::kPropNullable));
        CHECK(!c.EnumIsNumbered(types::kNoType) && !c.EnumAccepts(9999, 1) && c.EnumNameOf(9999, 1) == nullptr);
    }
    // negative text
    struct Bad { const char *text; types::Status status; };
    const Bad bad[] = {
        {"laml-types 1\ntype A.T src=nope\n", types::Status::UnknownSource},
        {"laml-types 1\nsource a loc rev MIT\nsource a loc2 rev MIT\n", types::Status::Duplicate},
        {"laml-types 1\nsource a loc rev\n", types::Status::Syntax},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X=1\nenumval A.E Y\n", types::Status::BadKind},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X\nenumval A.E Y=1\n", types::Status::BadKind},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X=zz\n", types::Status::Syntax},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X=\n", types::Status::Syntax},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X=99999999999999999999\n", types::Status::Syntax},
        {"laml-types 1\ntype A.E kind=enum\nenumval A.E X=1\nenumval A.E X=2\n", types::Status::Duplicate},
        {"laml-types 1\ntype A.T flags\n", types::Status::BadKind},
        {"laml-types 1\ntype A.T src=\n", types::Status::Syntax},
        {"laml-types 1\nsource a loc rev MIT\ntype A.T src=a src=a\n", types::Status::Syntax},
    };
    for (const Bad &b : bad)
    {
        types::Catalog c;
        types::Result r = Load(c, b.text);
        CHECK(!r.ok() && r.status == b.status);
    }
    {
        // the largest and smallest numbers load; one beyond does not
        types::Catalog c;
        CHECK(Load(c, "laml-types 1\ntype A.E kind=enum\nenumval A.E Max=9223372036854775807\nenumval A.E Min=-9223372036854775808\n").ok());
        types::Catalog d;
        CHECK(!Load(d, "laml-types 1\ntype A.E kind=enum\nenumval A.E Max=9223372036854775808\n").ok());
        types::Catalog f;
        CHECK(!Load(f, "laml-types 1\ntype A.E kind=enum\nenumval A.E Min=-9223372036854775809\n").ok());
    }
    {
        types::Limits lim;
        lim.maxSources = 2;
        types::Catalog c(lim);
        CHECK(c.AddSource({"a", "l", "r", "MIT"}).ok() && c.AddSource({"b", "l", "r", "MIT"}).ok());
        CHECK(c.AddSource({"c", "l", "r", "MIT"}).status == types::Status::LimitExceeded);
        CHECK(c.AddSource({"", "l", "r", "MIT"}).status == types::Status::EmptyName);
        CHECK(c.Resolve().empty());
        CHECK(c.AddSource({"d", "l", "r", "MIT"}).status == types::Status::Frozen);
    }
}

// ---------------------------------------------------------------------------------------------------
// The extracted WinUI catalog: traceable, resolved, with documented values
static void TestApiCatalog(const Env &e)
{
    const types::Catalog &c = e.catalog;
    CHECK(c.sources().size() == 1);
    const types::Source *src = c.FindSource("winui-idl");
    CHECK(src && src->license == "MIT" && src->revision == "8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b");
    CHECK(src && src->location.find("github.com/microsoft/microsoft-ui-xaml") != std::string::npos &&
          src->location.find(src->revision) != std::string::npos);
    // every declared type states where it came from
    size_t types = 0, noSource = 0;
    for (types::TypeId id = 1; id <= c.TypeCount(); id++)
    {
        const types::TypeInfo *t = c.Get(id);
        if (!t || t->isUnknown()) continue;
        types++;
        if (t->source != "winui-idl") noSource++;
    }
    CHECK(types > 500 && noSource == 0);
    std::printf("   winui-api catalog: %zu types, %zu warnings (value types the IDL files only refer to)\n", types, c.warnings().size());

    // values documented on the public BindingMode page (learn.microsoft.com ... microsoft.ui.xaml.data.bindingmode):
    // OneWay 1, OneTime 2, TwoWay 3
    const types::TypeInfo *bm = c.Find("Microsoft.UI.Xaml.Data.BindingMode");
    CHECK(bm && bm->kind == types::Kind::Enum && c.EnumIsNumbered(bm->id));
    CHECK(bm && *c.EnumNameOf(bm->id, 1) == "OneWay" && *c.EnumNameOf(bm->id, 2) == "OneTime" && *c.EnumNameOf(bm->id, 3) == "TwoWay");
    CHECK(bm && !c.EnumAccepts(bm->id, 0) && !c.EnumAccepts(bm->id, 4));
    // implicit numbering of the IDL: Visibility is Visible=0, Collapsed=1
    const types::TypeInfo *vis = c.Find("Microsoft.UI.Xaml.Visibility");
    CHECK(vis && c.EnumAccepts(vis->id, 0) && c.EnumAccepts(vis->id, 1) && !c.EnumAccepts(vis->id, 2));
    // ToggleButton (public page: ToggleButton.IsChecked, IsThreeState; base ButtonBase)
    const types::TypeInfo *tb = c.Find("Microsoft.UI.Xaml.Controls.Primitives.ToggleButton");
    CHECK(tb && tb->baseName == "Microsoft.UI.Xaml.Controls.Primitives.ButtonBase");
    const types::Property *ic = tb ? c.FindProperty(tb->id, "IsChecked") : nullptr;
    CHECK(ic && ic->valueTypeName == "LamlCore.Boolean" && (ic->flags & types::kPropNullable));
    const types::Property *ts = tb ? c.FindProperty(tb->id, "IsThreeState") : nullptr;
    CHECK(ts && !(ts->flags & types::kPropNullable));
    // the Grid attached properties come from the static Get/Set pairs
    const types::TypeInfo *grid = c.Find("Microsoft.UI.Xaml.Controls.Grid");
    CHECK(grid && c.FindAttachedProperty(grid->id, "Row") && c.FindAttachedProperty(grid->id, "ColumnSpan"));
    const types::Property *rd = grid ? c.FindProperty(grid->id, "RowDefinitions") : nullptr;
    CHECK(rd && (rd->flags & types::kPropReadOnly));
    // flags enums are marked and accept combinations
    size_t flagsEnums = 0;
    for (types::TypeId id = 1; id <= c.TypeCount(); id++)
    {
        const types::TypeInfo *t = c.Get(id);
        if (t && (t->flags & types::kTypeFlags) && t->enumNumbers.size() >= 2)
        {
            flagsEnums++;
            CHECK(c.EnumAccepts(id, t->enumNumbers[0] | t->enumNumbers[1]));
        }
    }
    CHECK(flagsEnums >= 1);
    // how much of the stable table the catalog describes (the coverage metric of the catalog itself)
    types::Coverage cov = types::Compare(e.stable, c, kPres);
    std::printf("   stable coverage: types %zu/%zu, properties %zu/%zu\n", cov.typesFound, cov.liveTypes, cov.propertiesFound,
                cov.liveProperties);
    CHECK(cov.typesFound * 100 >= cov.liveTypes * 60);
}

// ---------------------------------------------------------------------------------------------------
// Deferred streams
static void TestDeferredLinks(const Env &e)
{
    xbf::File file = EmptyFile();
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);

    // stream 0: root Grid with a member (Children) that carries runtime data for stream 1; stream 1 acts on
    // the Grid again (its first operation fetches the member) - a stream that continues a scope of its parent
    StreamBuilder s0, s1;
    s0.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    s0.Prop(NodeType::PushScopeGetValue, SProp(e, "Panel", "Children"));
    s0.Add(NodeType::SetCustomRuntimeData).targetStream = 1;
    s0.Add(NodeType::PopScope);
    s0.End();
    s1.Prop(NodeType::PushScopeGetValue, SProp(e, "Panel", "Children"));
    s1.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    s1.End();
    s1.Add(NodeType::AddToCollection);
    s1.Add(NodeType::PopScope);
    xbf::Decoded d;
    d.streams.push_back(s0.s);
    d.streams.push_back(s1.s);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    CHECK(t.ok());
    CHECK(t.counts.linkedStreams == 1 && t.counts.orphanStreams == 0);
    CHECK(t.streams[1].link.linked && t.streams[1].link.fromStream == 0 && t.streams[1].link.via == LinkVia::RuntimeData);
    CHECK(!t.streams[0].link.linked);
    uint32_t member = t.streams[1].link.node;
    CHECK(member < t.nodes.size() && t.nodes[member].fromMember && t.nodes[member].linkedStreams.size() == 1 &&
          t.nodes[member].linkedStreams[0] == 1);
    // the placeholder of stream 1 stands for the Grid (the member's parent), not for the member
    CHECK(t.counts.externalContexts == 1 && t.counts.linkedContexts == 1);
    const LogicalNode *ext = nullptr;
    for (const LogicalNode &n : t.nodes) if (n.external) ext = &n;
    CHECK(ext && ext->contextNode == t.nodes[member].parent && t.nodes[ext->contextNode].type.name == "Microsoft.UI.Xaml.Controls.Grid");
    CHECK(t.counts.notImplementedOps == 1);     // only the runtime data payload itself; the context is no longer "not linked"
    std::string dump = DumpLogicalTree(t);
    CHECK(dump.find("deferred stream 1 (SetCustomRuntimeData)") != std::string::npos && dump.find("stands for node") != std::string::npos);

    // SetDeferredProperty on a created object: the deferred stream starts with its own root (no placeholder needed)
    StreamBuilder a, b;
    a.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
    xbf::Node &dp = a.Add(NodeType::SetDeferredProperty);
    dp.property1 = SProp(e, "ContentControl", "Content");
    dp.targetStream = 1;
    a.End();
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
    b.End();
    xbf::Decoded d2;
    d2.streams.push_back(a.s);
    d2.streams.push_back(b.s);
    LogicalTree t2;
    BuildLogicalTree(d2, r, t2);
    CHECK(t2.ok() && t2.counts.linkedStreams == 1 && t2.counts.externalContexts == 0);
    CHECK(t2.streams[1].link.via == LinkVia::DeferredProperty && t2.streams[1].link.property.name == "Content");
    CHECK(t2.streams[1].link.node == 0 && t2.streams[1].roots.size() == 1);

    // negative: self reference, backward reference, missing stream, referenced twice, orphan
    {
        StreamBuilder x;
        x.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        xbf::Node &n = x.Add(NodeType::SetDeferredProperty);
        n.property1 = SProp(e, "ContentControl", "Content");
        n.targetStream = 0;
        x.End();
        xbf::Decoded dx;
        dx.streams.push_back(x.s);
        LogicalTree tx;
        BuildLogicalTree(dx, r, tx);
        CHECK(!tx.ok() && tx.counts.structureErrors == 1 && tx.counts.linkedStreams == 0);
    }
    {
        StreamBuilder x, y;
        x.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        xbf::Node &n = x.Add(NodeType::SetDeferredProperty);
        n.property1 = SProp(e, "ContentControl", "Content");
        n.targetStream = 9;                     // the file has two streams
        x.End();
        y.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        y.End();
        xbf::Decoded dx;
        dx.streams.push_back(x.s);
        dx.streams.push_back(y.s);
        LogicalTree tx;
        BuildLogicalTree(dx, r, tx);
        CHECK(!tx.ok() && tx.counts.structureErrors == 1 && tx.counts.linkedStreams == 0 && tx.counts.orphanStreams == 1);
    }
    {
        // backward: stream 1 names stream 0
        StreamBuilder x, y;
        x.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        x.End();
        y.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        xbf::Node &n = y.Add(NodeType::SetDeferredProperty);
        n.property1 = SProp(e, "ContentControl", "Content");
        n.targetStream = 0;
        y.End();
        xbf::Decoded dx;
        dx.streams.push_back(x.s);
        dx.streams.push_back(y.s);
        LogicalTree tx;
        BuildLogicalTree(dx, r, tx);
        CHECK(!tx.ok() && tx.counts.structureErrors == 1 && tx.counts.linkedStreams == 0 && tx.counts.orphanStreams == 1);
    }
    {
        // two nodes name stream 1: the first reference is kept, the second is a structure error
        StreamBuilder x, y;
        x.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        for (int i = 0; i < 2; i++)
        {
            xbf::Node &n = x.Add(NodeType::SetDeferredProperty);
            n.property1 = SProp(e, "ContentControl", "Content");
            n.targetStream = 1;
        }
        x.End();
        y.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Grid"));
        y.End();
        xbf::Decoded dx;
        dx.streams.push_back(x.s);
        dx.streams.push_back(y.s);
        LogicalTree tx;
        BuildLogicalTree(dx, r, tx);
        CHECK(!tx.ok() && tx.counts.structureErrors == 1 && tx.counts.linkedStreams == 1 && tx.streams[1].link.linked);
        CHECK(tx.nodes[0].linkedStreams.size() == 1);
    }
    {
        // an unreferenced second stream that needs an outer object: placeholder kept, reported as not linked
        StreamBuilder x, y;
        x.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        x.End();
        y.Prop(NodeType::PushScopeGetValue, SProp(e, "Panel", "Children"));
        y.Add(NodeType::PopScope);
        xbf::Decoded dx;
        dx.streams.push_back(x.s);
        dx.streams.push_back(y.s);
        LogicalTree tx;
        BuildLogicalTree(dx, r, tx);
        CHECK(tx.counts.orphanStreams == 1 && tx.counts.externalContexts == 1 && tx.counts.linkedContexts == 0);
        CHECK(tx.counts.notImplementedOps >= 2);
    }
}

// ---------------------------------------------------------------------------------------------------
// Validation
struct Check1
{
    ValidationReport rep;
    LogicalTree tree;
};

static bool Has(const ValidationReport &r, FindingKind k, FindingCode c)
{
    for (const Finding &f : r.findings)
        if (f.kind == k && f.code == c) return true;
    return false;
}

// One Button (or another type) with one SetValueConstant-like assignment; returns the validation report.
static ValidationReport OneAssign(const Env &e, const char *type, xbf::Ref prop, xbf::Constant c, bool typeConverted = false,
                                  NodeType op = NodeType::SetValueConstant)
{
    xbf::File file = EmptyFile();
    RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
    StreamBuilder b;
    b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, type));
    if (typeConverted) op = NodeType::SetValueTypeConvertedConstant;
    xbf::Node &n = b.Add(op);
    n.property1 = prop;
    n.constant = c;
    b.End();
    xbf::Decoded d;
    d.streams.push_back(b.s);
    LogicalTree t;
    BuildLogicalTree(d, r, t);
    ValidationReport rep;
    Validate(t, e.catalog, r, rep);
    return rep;
}

static void ExpectOutcome(const ValidationReport &r, size_t valid, size_t format, size_t unknown, size_t notimpl)
{
    CHECK(r.counts.assignments == 1);
    CHECK(r.counts.assignmentsValid == valid && r.counts.assignmentsFormatError == format &&
          r.counts.assignmentsUnknown == unknown && r.counts.assignmentsNotImplemented == notimpl);
}

static void TestValidation(const Env &e, const Env &seed)
{
    xbf::Ref width = SProp(e, "FrameworkElement", "Width");          // Double
    xbf::Ref hit = SProp(e, "UIElement", "IsHitTestVisible");        // Boolean
    xbf::Ref row = SProp(e, "Grid", "Row");                          // Int32, attached
    xbf::Ref vis = SProp(e, "UIElement", "Visibility");              // enum Visibility
    xbf::Ref margin = SProp(e, "FrameworkElement", "Margin");        // Thickness
    xbf::Ref bg = SProp(e, "Panel", "Background");                   // Brush

    // numbers
    ExpectOutcome(OneAssign(e, "Button", width, Flt(5)), 1, 0, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", width, Int(5)), 1, 0, 0, 0);             // an integer widens to Double
    {
        ValidationReport r = OneAssign(e, "Button", width, Kind(xbf::ConstKind::BoolTrue));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::ConstantKindMismatch) && !r.ok());
    }
    ExpectOutcome(OneAssign(e, "Button", width, Str(u"wide")), 0, 1, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", width, Kind(xbf::ConstKind::Thickness)), 0, 1, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", hit, Kind(xbf::ConstKind::BoolTrue)), 1, 0, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", hit, Flt(1)), 0, 1, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", hit, Int(1)), 0, 1, 0, 0);
    // integer range and null
    ExpectOutcome(OneAssign(e, "Button", row, Int(3)), 1, 0, 0, 0);
    {
        ValidationReport r = OneAssign(e, "Button", row, Int(5000000000LL));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::ConstantOutOfRange));
    }
    ExpectOutcome(OneAssign(e, "Button", row, Flt(1)), 0, 1, 0, 0);
    {
        ValidationReport r = OneAssign(e, "Button", row, Kind(xbf::ConstKind::NullString));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::NullNotAllowed));
    }
    // nullable Boolean takes null; a String-like property takes null too
    ExpectOutcome(OneAssign(e, "ToggleButton", SProp(e, "ToggleButton", "IsChecked"), Kind(xbf::ConstKind::NullString)), 1, 0, 0, 0);
    ExpectOutcome(OneAssign(e, "ToggleButton", SProp(e, "ToggleButton", "IsChecked"), Kind(xbf::ConstKind::BoolTrue)), 1, 0, 0, 0);
    // structs and colours
    ExpectOutcome(OneAssign(e, "Button", margin, Kind(xbf::ConstKind::Thickness)), 1, 0, 0, 0);
    ExpectOutcome(OneAssign(e, "Button", margin, Kind(xbf::ConstKind::GridLength)), 0, 1, 0, 0);
    ExpectOutcome(OneAssign(e, "Grid", bg, Kind(xbf::ConstKind::Color)), 1, 0, 0, 0);           // a Brush property takes a colour
    ExpectOutcome(OneAssign(e, "Button", width, Kind(xbf::ConstKind::Color)), 0, 1, 0, 0);
    ExpectOutcome(OneAssign(e, "GradientStop", SProp(e, "GradientStop", "Color"), Kind(xbf::ConstKind::Color)), 1, 0, 0, 0);
    // enums
    ExpectOutcome(OneAssign(e, "Button", vis, EnumC(e, "Visibility", 1)), 1, 0, 0, 0);
    {
        ValidationReport r = OneAssign(e, "Button", vis, EnumC(e, "Visibility", 7));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::EnumValueNotMember));
    }
    {
        ValidationReport r = OneAssign(e, "Button", vis, EnumC(e, "HorizontalAlignment", 1));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::EnumTypeMismatch));
    }
    // the same enum against the seed catalog, which declares no numbers: not an error, not implemented
    {
        ValidationReport r = OneAssign(seed, "Button", SProp(seed, "UIElement", "Visibility"), EnumC(seed, "Visibility", 1));
        ExpectOutcome(r, 0, 0, 0, 1);
        CHECK(Has(r, FindingKind::NotImplemented, FindingCode::EnumNumbersNotDeclared));
    }
    // a property on an object that does not derive from its owner
    {
        ValidationReport r = OneAssign(e, "Binding", SProp(e, "ToggleButton", "IsChecked"), Kind(xbf::ConstKind::BoolTrue));
        ExpectOutcome(r, 0, 1, 0, 0);
        CHECK(Has(r, FindingKind::FormatError, FindingCode::PropertyNotOnType));
    }
    // the attached property needs no owner relation (the catalog does not record its target type)
    ExpectOutcome(OneAssign(e, "Binding", row, Int(1)), 1, 0, 0, 0);
    // a read-only property given a constant
    {
        ValidationReport r = OneAssign(e, "Grid", SProp(e, "Panel", "Children"), Str(u"x"));
        CHECK(Has(r, FindingKind::FormatError, FindingCode::ReadOnlyAssigned));
        CHECK(r.counts.assignmentsFormatError == 1);
    }
    // not implemented: resource references and type-converted text
    {
        ValidationReport r = OneAssign(e, "Button", width, Str(u"key"), false, NodeType::SetValueFromStaticResource);
        ExpectOutcome(r, 0, 0, 0, 1);
        CHECK(Has(r, FindingKind::NotImplemented, FindingCode::ValueFormNotChecked));
    }
    ExpectOutcome(OneAssign(e, "Button", width, Str(u"12"), true), 0, 0, 0, 1);
    // unknown: the object's type is not in the catalog (the seed has no Canvas) although the property resolves
    {
        ValidationReport r = OneAssign(seed, "Canvas", SProp(seed, "FrameworkElement", "Width"), Flt(1));
        ExpectOutcome(r, 0, 0, 1, 0);
        CHECK(Has(r, FindingKind::UnknownReference, FindingCode::UnknownObjectType));
    }
    // unknown: the property is not in the catalog
    {
        ValidationReport r = OneAssign(seed, "Button", SProp(seed, "UIElement", "IsTapEnabled"), Kind(xbf::ConstKind::BoolTrue));
        ExpectOutcome(r, 0, 0, 1, 0);
        CHECK(Has(r, FindingKind::UnknownReference, FindingCode::UnknownProperty));
    }

    // object values: UserControl.Content takes a UIElement
    {
        xbf::File file = EmptyFile();
        RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
        for (int bad = 0; bad < 2; bad++)
        {
            StreamBuilder b;
            b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "UserControl"));
            b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, bad ? "Binding" : "TextBlock"));
            b.End();
            b.Prop(NodeType::SetValue, SProp(e, "UserControl", "Content"));
            b.End();
            xbf::Decoded d;
            d.streams.push_back(b.s);
            LogicalTree t;
            BuildLogicalTree(d, r, t);
            CHECK(t.ok());
            ValidationReport rep;
            Validate(t, e.catalog, r, rep);
            CHECK(rep.counts.assignments == 1);
            if (bad)
                CHECK(rep.counts.assignmentsFormatError == 1 && Has(rep, FindingKind::FormatError, FindingCode::ObjectTypeMismatch));
            else
                CHECK(rep.counts.assignmentsValid == 1 && rep.ok());
        }
    }

    // finding cap: counts stay exact
    {
        xbf::File file = EmptyFile();
        RefResolver r(nullptr, 0, file, e.catalog, &e.stable);
        StreamBuilder b;
        b.Create(NodeType::PushScopeCreateTypeBeginInit, STy(e, "Button"));
        for (int i = 0; i < 20; i++) b.SetConst(width, Kind(xbf::ConstKind::BoolTrue));
        b.End();
        xbf::Decoded d;
        d.streams.push_back(b.s);
        LogicalTree t;
        BuildLogicalTree(d, r, t);
        ValidateLimits lim;
        lim.maxFindings = 5;
        ValidationReport rep;
        Validate(t, e.catalog, r, lim, rep);
        CHECK(rep.findings.size() == 5 && rep.findingsDropped == 15 && rep.counts.assignmentsFormatError == 20);
        CHECK(rep.counts.byCode[static_cast<size_t>(FindingCode::ConstantKindMismatch)] == 20);
        CHECK(DumpValidation(rep, 2).find("formatError=20") != std::string::npos);
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

static void CheckOutcomesAddUp(const ValidationReport &v)
{
    const ValidationCounts &c = v.counts;
    CHECK(c.assignmentsValid + c.assignmentsFormatError + c.assignmentsUnknown + c.assignmentsNotImplemented == c.assignments);
    CHECK(c.itemsValid + c.itemsFormatError + c.itemsUnknown + c.itemsNotImplemented == c.items);
}

static void TestFixtures(const Env &api, const Env &seed, const std::string &dir)
{
    static const char *names[] = {"SampleXbf2.xbf", "CustomTypesPage.xbf", "XbfVsmTest.v2.1.xbf", "PageWithConditionalXaml.xbf",
                                  "SampleWithChecksum.xbf"};
    for (const char *name : names)
    {
        Loaded l;
        CHECK(OpenFixture(l, dir, name));
        if (!l.ok) continue;
        for (const Env *env : {&api, &seed})
        {
            RefResolver r(l.bytes.data(), l.bytes.size(), l.file, env->catalog, &env->stable);
            LogicalTree t;
            BuildLogicalTree(l.decoded, r, t);
            ValidationReport v;
            Validate(t, env->catalog, r, v);
            CheckOutcomesAddUp(v);
            // Microsoft's own test files are valid: no format error may be reported on them, with either catalog
            CHECK(t.ok() && v.ok());
            if (env == &api)
                std::printf("   %-28s objects=%zu types resolved/unknown=%zu/%zu props resolved/unknown=%zu/%zu | assignments valid/format/unknown/notimpl=%zu/%zu/%zu/%zu\n",
                            name, t.counts.objects, t.counts.typesResolved, t.counts.typesUnknown, t.counts.propertiesResolved,
                            t.counts.propertiesUnknown, v.counts.assignmentsValid, v.counts.assignmentsFormatError,
                            v.counts.assignmentsUnknown, v.counts.assignmentsNotImplemented);
        }
    }

    // ---- the regression metric: SampleXbf2 objects whose type resolves ----
    Loaded l;
    CHECK(OpenFixture(l, dir, "SampleXbf2.xbf"));
    {
        RefResolver r(l.bytes.data(), l.bytes.size(), l.file, seed.catalog, &seed.stable);
        LogicalTree t;
        BuildLogicalTree(l.decoded, r, t);
        CHECK(t.counts.objects == 67 && t.counts.typesResolved == 3);               // M3 baseline with the 25-type seed
    }
    {
        RefResolver r(l.bytes.data(), l.bytes.size(), l.file, api.catalog, &api.stable);
        LogicalTree t;
        BuildLogicalTree(l.decoded, r, t);
        CHECK(t.counts.objects == 67);
        CHECK(t.counts.typesResolved == 44 && t.counts.typesUnknown == 23);          // M4
        // the 23 that stay unknown are the page's own types, declared by the file and by no public document
        std::set<std::string> unknownNames;
        for (const LogicalNode &n : t.nodes)
            if (!n.fromMember && !n.external && n.type.status == RefStatus::UnknownType) unknownNames.insert(n.type.name);
        CHECK(unknownNames == (std::set<std::string>{"Numbers.Controls.CalculatorButton", "Numbers.NumbersAndOperatorsEnum"}));
        CHECK(t.counts.propertiesResolved == 194 && t.counts.propertiesUnknown == 22);
        ValidationReport v;
        Validate(t, api.catalog, r, v);
        CHECK(v.ok() && v.counts.assignmentsValid == 86);
        // the enum constants of the page are checked against the numbers of the catalog (HorizontalAlignment etc.)
        CHECK(v.counts.assignmentsValid >= 40);
    }

    // ---- XbfVsmTest: every deferred stream is reached, the three scope continuations are linked ----
    Loaded v;
    CHECK(OpenFixture(v, dir, "XbfVsmTest.v2.1.xbf"));
    RefResolver rv(v.bytes.data(), v.bytes.size(), v.file, api.catalog, &api.stable);
    LogicalTree tv;
    BuildLogicalTree(v.decoded, rv, tv);
    CHECK(tv.ok() && tv.streams.size() == 7);
    CHECK(tv.counts.linkedStreams == 6 && tv.counts.orphanStreams == 0);
    CHECK(tv.counts.externalContexts == 3 && tv.counts.linkedContexts == 3);
    CHECK(tv.counts.notImplementedOps == 3);                                         // the three runtime data payloads
    const LinkVia expectVia[7] = {LinkVia::None, LinkVia::RuntimeData, LinkVia::DeferredProperty, LinkVia::RuntimeData,
                                  LinkVia::DeferredProperty, LinkVia::RuntimeData, LinkVia::DeferredProperty};
    const uint32_t expectFrom[7] = {0, 0, 1, 2, 1, 4, 1};
    for (uint32_t s = 0; s < 7; s++)
    {
        CHECK(tv.streams[s].link.linked == (s != 0));
        if (s == 0) continue;
        CHECK(tv.streams[s].link.via == expectVia[s] && tv.streams[s].link.fromStream == expectFrom[s]);
        CHECK(tv.streams[s].link.fromStream < s);
    }
    for (const LogicalNode &n : tv.nodes)
        if (n.external)
        {
            CHECK(n.contextNode < tv.nodes.size() && !tv.nodes[n.contextNode].external);
            CHECK(tv.nodes[n.contextNode].stream == tv.streams[n.stream].link.fromStream);
        }
    std::string dump = DumpLogicalTree(tv);
    CHECK(dump.find("not linked") == std::string::npos);
    ValidationReport vv;
    Validate(tv, api.catalog, rv, vv);
    CheckOutcomesAddUp(vv);
    CHECK(vv.ok());
    // with the catalog the page's whole object model resolves
    CHECK(tv.counts.typesUnknown == 0);
    // and without any catalog the links are the same (they do not depend on metadata)
    types::Catalog none;
    CHECK(none.Resolve().empty());
    RefResolver rn(v.bytes.data(), v.bytes.size(), v.file, none, &api.stable);
    LogicalTree tn;
    BuildLogicalTree(v.decoded, rn, tn);
    CHECK(tn.counts.linkedStreams == 6 && tn.counts.linkedContexts == 3);
}

// ---------------------------------------------------------------------------------------------------
// Malformed input: mutate real files and run the whole pipeline; nothing may crash, loop or run away
static uint32_t g_seed = 0x2545F491u;
static uint32_t Next()
{
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

static void TestMalformed(const Env &api, const std::string &dir)
{
    static const char *names[] = {"SampleXbf2.xbf", "XbfVsmTest.v2.1.xbf", "CustomTypesPage.xbf"};
    size_t parsed = 0, built = 0, formatErrors = 0, structureErrors = 0;
    for (const char *name : names)
    {
        std::vector<uint8_t> orig = Slurp(dir + "/" + name);
        CHECK(!orig.empty());
        for (int iter = 0; iter < 600; iter++)
        {
            std::vector<uint8_t> b = orig;
            int edits = 1 + static_cast<int>(Next() % 4);
            for (int k = 0; k < edits; k++)
                b[Next() % b.size()] = static_cast<uint8_t>(Next());
            if (iter % 7 == 0) b.resize(Next() % b.size());                    // truncation
            xbf::File f;
            if (!xbf::Parse(b.data(), b.size(), f).ok()) continue;
            parsed++;
            xbf::Decoded d;
            xbf::DecodeNodes(b.data(), b.size(), f, d);
            RefResolver r(b.data(), b.size(), f, api.catalog, &api.stable);
            LogicalTree t;
            BuildLogicalTree(d, r, t);
            built++;
            ValidationReport v;
            Validate(t, api.catalog, r, v);
            // the tree is always internally consistent whatever the input was
            for (const LogicalNode &n : t.nodes)
            {
                CHECK(n.parent == kNoNode || n.parent < t.nodes.size());
                CHECK(!n.external || n.contextNode == kNoNode || n.contextNode < t.nodes.size());
                for (uint32_t s : n.linkedStreams) CHECK(s < t.streams.size() && s > n.stream);
            }
            CheckOutcomesAddUp(v);
            formatErrors += v.counts.assignmentsFormatError;
            structureErrors += t.counts.structureErrors;
        }
    }
    std::printf("   mutated files: %zu parsed, %zu folded and validated, %zu format errors and %zu structure errors reported\n",
                parsed, built, formatErrors, structureErrors);
    CHECK(built > 100);
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 5)
    {
        std::printf("usage: m4_tests <xbf-fixture-dir> <winui-metadata-dir> <winui-api.types> <seed.types>\n");
        return 2;
    }
    Env api, seed;
    if (!LoadEnv(api, argv[2], argv[3]) || !LoadEnv(seed, argv[2], argv[4]))
    {
        std::printf("cannot load the stable tables or a catalog\n");
        return 2;
    }
    TestCatalogSourcesAndEnums();
    TestApiCatalog(api);
    TestDeferredLinks(api);
    TestValidation(api, seed);
    TestFixtures(api, seed, argv[1]);
    TestMalformed(api, argv[1]);
    std::printf("m4_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
