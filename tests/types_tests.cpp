// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the type catalog and its text metadata. Expectations come from the documented rules in
// type_catalog.h, not from any upstream output. Deterministic; no files, no clocks.
#include <cstdio>
#include <string>

#include "../src/types/type_catalog.h"

using namespace laml::types;

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

static TypeDecl T(const char *name, const char *base = "", Kind kind = Kind::Class)
{
    TypeDecl d;
    d.fullName = name;
    d.baseName = base;
    d.kind = kind;
    return d;
}
static PropertyDecl P(const char *name, const char *valueType, uint32_t flags = 0)
{
    PropertyDecl d;
    d.name = name;
    d.valueTypeName = valueType;
    d.flags = flags;
    return d;
}

static const char *kXamlNs = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";

// A small hierarchy: Object <- DepObj <- UIElement <- FrameworkElement <- Control <- ContentControl <- Button
static void BuildHierarchy(Catalog &c)
{
    CHECK(c.AddType(T("Test.Object")).ok());
    CHECK(c.AddType(T("Test.DepObj", "Test.Object")).ok());
    CHECK(c.AddType(T("Test.UIElement", "Test.DepObj")).ok());
    CHECK(c.AddType(T("Test.FrameworkElement", "Test.UIElement")).ok());
    CHECK(c.AddType(T("Test.Control", "Test.FrameworkElement")).ok());
    TypeDecl cc = T("Test.ContentControl", "Test.Control");
    cc.contentProperty = "Content";
    CHECK(c.AddType(cc).ok());
    CHECK(c.AddType(T("Test.Button", "Test.ContentControl")).ok());
    CHECK(c.AddType(T("Test.String", "", Kind::Primitive)).ok());
    CHECK(c.AddType(T("Test.Double", "", Kind::Primitive)).ok());
    CHECK(c.AddProperty("Test.UIElement", P("Opacity", "Test.Double", kPropDependency)).ok());
    CHECK(c.AddProperty("Test.FrameworkElement", P("Width", "Test.Double", kPropDependency)).ok());
    CHECK(c.AddProperty("Test.FrameworkElement", P("Name", "Test.String")).ok());
    CHECK(c.AddProperty("Test.ContentControl", P("Content", "Test.Object", kPropDependency)).ok());
    CHECK(c.AddProperty("Test.Button", P("Width", "Test.Double", kPropDependency)).ok());   // hides the base Width
    CHECK(c.AddProperty("Test.Button", P("Missing", "Test.NotDeclared")).ok());
    CHECK(c.AddProperty("Test.Object", P("Row", "Test.Double", kPropAttached)).ok());
}

static void TestLookupAndIdentity()
{
    Catalog c;
    BuildHierarchy(c);
    CHECK(c.Resolve().empty());
    CHECK(c.IsResolved());

    const TypeInfo *b = c.Find("Test.Button");
    CHECK(b && b->name == "Button" && b->ns == "Test" && b->kind == Kind::Class);
    CHECK(c.Find("Test", "Button") == b);
    CHECK(!c.Find("Test", "Nope"));
    CHECK(!c.Find("button"));                 // case sensitive
    CHECK(c.Get(b->id) == b);
    CHECK(!c.Get(kNoType));
    CHECK(!c.Get(9999));
    CHECK(b->id != c.Find("Test.Control")->id);

    // a type without a namespace
    Catalog d;
    CHECK(d.AddType(T("Bare")).ok());
    CHECK(d.Find("Bare") && d.Find("Bare")->ns.empty());
    CHECK(d.Find("", "Bare") == d.Find("Bare"));
}

static void TestInheritance()
{
    Catalog c;
    BuildHierarchy(c);
    // before Resolve() nothing is linked: every hierarchy query answers "no"
    CHECK(!c.IsAssignable(c.Find("Test.Button")->id, c.Find("Test.Object")->id));
    CHECK(!c.IsAssignable(c.Find("Test.Button")->id, c.Find("Test.Button")->id));
    CHECK(c.Resolve().empty());
    CHECK(c.IsAssignable(c.Find("Test.Button")->id, c.Find("Test.Object")->id));
    CHECK(c.IsAssignable(c.Find("Test.Button")->id, c.Find("Test.Button")->id));
    CHECK(!c.IsAssignable(c.Find("Test.Object")->id, c.Find("Test.Button")->id));
    CHECK(!c.IsAssignable(c.Find("Test.Button")->id, c.Find("Test.Double")->id));
    CHECK(c.HierarchyDistance(c.Find("Test.Button")->id, c.Find("Test.Button")->id) == 0);
    CHECK(c.HierarchyDistance(c.Find("Test.Button")->id, c.Find("Test.Control")->id) == 2);
    CHECK(c.HierarchyDistance(c.Find("Test.Button")->id, c.Find("Test.Object")->id) == 6);
    CHECK(c.HierarchyDistance(c.Find("Test.Control")->id, c.Find("Test.Button")->id) == -1);
    CHECK(c.Find("Test.Button")->base == c.Find("Test.ContentControl")->id);
    CHECK(c.Find("Test.Object")->base == kNoType);
    CHECK(!c.IsAssignable(12345, c.Find("Test.Object")->id));
}

static void TestProperties()
{
    Catalog c;
    BuildHierarchy(c);
    CHECK(c.Resolve().empty());
    TypeId button = c.Find("Test.Button")->id;
    TypeId declaring = 0;

    const Property *opacity = c.FindProperty(button, "Opacity", &declaring);
    CHECK(opacity && declaring == c.Find("Test.UIElement")->id);
    CHECK(opacity && opacity->valueType == c.Find("Test.Double")->id && (opacity->flags & kPropDependency));

    const Property *w = c.FindProperty(button, "Width", &declaring);    // nearest wins
    CHECK(w && declaring == button);
    CHECK(c.FindProperty(c.Find("Test.Control")->id, "Width", &declaring) && declaring == c.Find("Test.FrameworkElement")->id);
    CHECK(!c.FindProperty(c.Find("Test.Object")->id, "Width"));
    CHECK(!c.FindProperty(button, "Nope"));
    CHECK(!c.FindProperty(button, "Row"));                       // attached are not instance properties
    CHECK(c.FindAttachedProperty(button, "Row"));                // but are found, through the base chain
    CHECK(!c.FindAttachedProperty(button, "Width"));
    CHECK(!c.FindProperty(99999, "Width"));

    // unresolved value type: stays kNoType, reported as a warning, not an error
    const Property *missing = c.FindProperty(button, "Missing");
    CHECK(missing && missing->valueType == kNoType && missing->valueTypeName == "Test.NotDeclared");
    bool warned = false;
    for (const Problem &p : c.warnings())
        if (p.message.find("Test.NotDeclared") != std::string::npos)
            warned = true;
    CHECK(warned);

    // AllProperties: base first, a derived property replaces the base one in place
    auto all = c.AllProperties(button);
    std::string names;
    for (const Property *p : all)
        names += p->name + ",";
    CHECK(names == "Opacity,Width,Name,Content,Missing,");
    CHECK(all.size() == 5 && all[1]->owner == button);   // Width is the Button one

    const Property *content = c.ContentProperty(button);
    CHECK(content && content->name == "Content");
    CHECK(!c.ContentProperty(c.Find("Test.Control")->id));
}

static void TestUnknownTypes()
{
    Catalog c;
    BuildHierarchy(c);
    CHECK(c.Resolve().empty());
    TypeId u = c.RecordUnknown("Vendor.Fancy");
    CHECK(u != kNoType);
    CHECK(c.RecordUnknown("Vendor.Fancy") == u);       // idempotent, also on a resolved catalog
    CHECK(!c.Find("Vendor.Fancy"));                    // not a declared type
    const TypeInfo *t = c.FindIncludingUnknown("Vendor.Fancy");
    CHECK(t && t->isUnknown() && t->id == u);
    CHECK(c.RecordUnknown("Test.Button") == c.Find("Test.Button")->id);   // declared name keeps its type
    CHECK(c.RecordUnknown("bad name") == kNoType);
    CHECK(c.RecordUnknown("") == kNoType);
    CHECK(!c.IsAssignable(u, c.Find("Test.Object")->id));
    CHECK(c.AllProperties(u).empty());

    // declaring a placeholder later turns it into a real type, same id (before Resolve)
    Catalog d;
    TypeId id = d.RecordUnknown("A.B");
    CHECK(d.AddType(T("A.B")).ok());
    CHECK(d.Find("A.B") && d.Find("A.B")->id == id && !d.Find("A.B")->isUnknown());
    CHECK(d.AddType(T("A.B")).status == Status::Duplicate);
    CHECK(d.AddType([] { TypeDecl x = T("A.C"); x.flags = kTypeUnknown; return x; }()).status == Status::BadKind);
}

static void TestXmlResolution()
{
    Catalog c;
    BuildHierarchy(c);
    CHECK(c.MapXmlNamespace(kXamlNs, "Other.Controls").ok());
    CHECK(c.MapXmlNamespace(kXamlNs, "Test").ok());
    CHECK(c.MapXmlNamespace(kXamlNs, "Test").status == Status::Duplicate);
    CHECK(c.AddType(T("Other.Controls.Button")).ok());           // first mapped namespace wins
    CHECK(c.Resolve().empty());
    XmlResolution r = c.ResolveXmlName(kXamlNs, "Button");
    CHECK(r.type == c.Find("Other.Controls.Button")->id);
    r = c.ResolveXmlName(kXamlNs, "Control");
    CHECK(r.type == c.Find("Test.Control")->id);
    CHECK(r.tried == "Other.Controls.Control, Test.Control");
    r = c.ResolveXmlName(kXamlNs, "Nope");
    CHECK(r.type == kNoType && r.tried == "Other.Controls.Nope, Test.Nope");
    r = c.ResolveXmlName("urn:unmapped", "Button");
    CHECK(r.type == kNoType && r.tried == "(xmlns not mapped)");
    CHECK(c.ResolveXmlName(kXamlNs, "bad.name").type == kNoType);
    CHECK(c.ResolveXmlName(kXamlNs, "").type == kNoType);
}

static void TestDeclarationErrors()
{
    Catalog c;
    CHECK(c.AddType(T("")).status == Status::EmptyName);
    CHECK(c.AddType(T("1abc")).status == Status::BadName);
    CHECK(c.AddType(T("A..B")).status == Status::BadName);
    CHECK(c.AddType(T(".A")).status == Status::BadName);
    CHECK(c.AddType(T("A.")).status == Status::BadName);
    CHECK(c.AddType(T("A B")).status == Status::BadName);
    CHECK(c.AddType(T("A.B", "C D")).status == Status::BadName);
    CHECK(c.AddType(T("Self", "Self")).status == Status::Unresolved);
    CHECK(c.AddType(T("X.Y")).ok());
    CHECK(c.AddType(T("X.Y")).status == Status::Duplicate);

    CHECK(c.AddProperty("X.Nope", P("p", "X.Y")).status == Status::UnknownOwner);
    CHECK(c.AddProperty("X.Y", P("", "X.Y")).status == Status::EmptyName);
    CHECK(c.AddProperty("X.Y", P("a.b", "X.Y")).status == Status::BadName);   // plain identifier
    CHECK(c.AddProperty("X.Y", P("p", "")).status == Status::EmptyName);
    CHECK(c.AddProperty("X.Y", P("p", "X.Y")).ok());
    CHECK(c.AddProperty("X.Y", P("p", "X.Y")).status == Status::Duplicate);

    CHECK(c.AddType(T("X.E", "", Kind::Enum)).ok());
    CHECK(c.AddEnumValue("X.E", "A").ok());
    CHECK(c.AddEnumValue("X.E", "A").status == Status::Duplicate);
    CHECK(c.AddEnumValue("X.Y", "A").status == Status::BadKind);
    CHECK(c.AddEnumValue("X.Nope", "A").status == Status::UnknownOwner);
    CHECK(c.AddEnumValue("X.E", "1x").status == Status::BadName);
    CHECK(c.AddProperty("X.E", P("p", "X.Y")).status == Status::BadKind);
    CHECK(c.MapXmlNamespace("", "X").status == Status::EmptyName);
    CHECK(c.MapXmlNamespace("urn:x", "X Y").status == Status::BadName);
    TypeDecl bad = T("X.Z");
    bad.contentProperty = "a b";
    CHECK(c.AddType(bad).status == Status::BadName);
}

static void TestResolveErrors()
{
    // missing base
    {
        Catalog c;
        CHECK(c.AddType(T("A.Child", "A.Missing")).ok());
        auto errs = c.Resolve();
        CHECK(errs.size() == 1 && errs[0].status == Status::Unresolved);
        CHECK(!c.IsResolved());
        CHECK(!c.IsAssignable(c.Find("A.Child")->id, c.Find("A.Child")->id));   // queries need a resolved catalog
        // declarations are still allowed after a failed Resolve, and fix it
        CHECK(c.AddType(T("A.Missing")).ok());
        CHECK(c.Resolve().empty());
        CHECK(c.IsAssignable(c.Find("A.Child")->id, c.Find("A.Missing")->id));
    }
    // cycle of two, and of three
    {
        Catalog c;
        CHECK(c.AddType(T("A.X", "A.Y")).ok());
        CHECK(c.AddType(T("A.Y", "A.X")).ok());
        auto errs = c.Resolve();
        CHECK(!errs.empty() && !c.IsResolved());
        Catalog d;
        CHECK(d.AddType(T("A.P", "A.Q")).ok());
        CHECK(d.AddType(T("A.Q", "A.R")).ok());
        CHECK(d.AddType(T("A.R", "A.P")).ok());
        CHECK(!d.Resolve().empty());
    }
    // depth limit
    {
        Limits lim;
        lim.maxHierarchyDepth = 3;
        Catalog c(lim);
        CHECK(c.AddType(T("D.T0")).ok());
        CHECK(c.AddType(T("D.T1", "D.T0")).ok());
        CHECK(c.AddType(T("D.T2", "D.T1")).ok());
        CHECK(c.AddType(T("D.T3", "D.T2")).ok());
        CHECK(c.Resolve().empty());                 // depth 3 is allowed
        CHECK(c.AddType(T("D.T4", "D.T3")).status == Status::Frozen);   // resolved: closed
        Catalog e(lim);
        CHECK(e.AddType(T("D.T0")).ok());
        CHECK(e.AddType(T("D.T1", "D.T0")).ok());
        CHECK(e.AddType(T("D.T2", "D.T1")).ok());
        CHECK(e.AddType(T("D.T3", "D.T2")).ok());
        CHECK(e.AddType(T("D.T4", "D.T3")).ok());
        CHECK(!e.Resolve().empty());                // depth 4 is not
    }
    // content property that is not an instance property
    {
        Catalog c;
        TypeDecl d = T("C.Holder");
        d.contentProperty = "Items";
        CHECK(c.AddType(d).ok());
        CHECK(c.AddType(T("C.Obj")).ok());
        auto errs = c.Resolve();
        CHECK(errs.size() == 1 && errs[0].status == Status::BadKind);
        CHECK(c.AddProperty("C.Holder", P("Items", "C.Obj", kPropAttached)).ok());   // attached does not count
        CHECK(!c.Resolve().empty());
    }
    // frozen after a successful resolve
    {
        Catalog c;
        CHECK(c.AddType(T("F.A")).ok());
        CHECK(c.Resolve().empty());
        CHECK(c.AddType(T("F.B")).status == Status::Frozen);
        CHECK(c.AddProperty("F.A", P("p", "F.A")).status == Status::Frozen);
        CHECK(c.AddEnumValue("F.A", "v").status == Status::Frozen);
        CHECK(c.MapXmlNamespace("urn:f", "F").status == Status::Frozen);
    }
}

static void TestLimits()
{
    Limits lim;
    lim.maxTypes = 2;
    lim.maxPropertiesPerType = 1;
    lim.maxEnumValuesPerType = 1;
    lim.maxNameChars = 8;
    lim.maxXmlMappings = 1;
    Catalog c(lim);
    CHECK(c.AddType(T("A")).ok());
    CHECK(c.AddType(T("B", "", Kind::Enum)).ok());
    CHECK(c.AddType(T("C")).status == Status::LimitExceeded);
    CHECK(c.RecordUnknown("D") == kNoType);
    CHECK(c.AddType(T("TooLongName")).status == Status::LimitExceeded);
    CHECK(c.AddProperty("A", P("p", "A")).ok());
    CHECK(c.AddProperty("A", P("q", "A")).status == Status::LimitExceeded);
    CHECK(c.AddEnumValue("B", "x").ok());
    CHECK(c.AddEnumValue("B", "y").status == Status::LimitExceeded);
    CHECK(c.MapXmlNamespace("u", "A").ok());
    CHECK(c.MapXmlNamespace("v", "A").status == Status::LimitExceeded);
}

static Result Load(Catalog &c, const std::string &s) { return LoadText(c, s.data(), s.size()); }

static void TestText()
{
    Catalog c;
    std::string text =
        "\xEF\xBB\xBF# a comment\r\n"
        "laml-types 1\r\n"
        "\r\n"
        "xmlns urn:t T   # trailing comment\n"
        "type T.Object\n"
        "type T.Str kind=primitive\n"
        "type T.Color kind=enum\n"
        "enumval T.Color Red\n"
        "enumval T.Color Green\n"
        "type T.Base base=T.Object abstract content=Body\n"
        "type T.Derived base=T.Base sealed\n"
        "prop T.Base.Body T.Object dependency\n"
        "prop T.Derived.Tag T.Str readonly\n"
        "prop T.Base.Slot T.Str attached\n";
    Result r = Load(c, text);
    CHECK(r.ok());
    CHECK(c.Resolve().empty());
    const TypeInfo *d = c.Find("T.Derived");
    CHECK(d && (d->flags & kTypeSealed) && d->baseName == "T.Base");
    CHECK(c.Find("T.Base") && (c.Find("T.Base")->flags & kTypeAbstract));
    CHECK(c.Find("T.Color")->kind == Kind::Enum && c.Find("T.Color")->enumValues.size() == 2);
    CHECK(c.ResolveXmlName("urn:t", "Derived").type == d->id);
    CHECK(c.FindProperty(d->id, "Body") && c.ContentProperty(d->id));
    CHECK(c.FindProperty(d->id, "Tag") && (c.FindProperty(d->id, "Tag")->flags & kPropReadOnly));
    CHECK(c.FindAttachedProperty(d->id, "Slot"));
}

static void ExpectBad(const char *text, Status status, size_t line)
{
    Catalog c;
    Result r = Load(c, text);
    if (r.status != status || r.line != line)
        std::printf("   got %s line %zu (%s) for: %.40s\n", StatusName(r.status), r.line, r.message.c_str(), text);
    CHECK(r.status == status);
    CHECK(r.line == line);
}

static void TestMalformedText()
{
    ExpectBad("", Status::Syntax, 0);                                  // empty
    ExpectBad("# only comments\n", Status::Syntax, 0);
    ExpectBad("laml-types 2\n", Status::Syntax, 1);                    // wrong version
    ExpectBad("type A.B\n", Status::Syntax, 1);                        // no header
    ExpectBad("laml-types 1\nbogus x\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype A kind=widget\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype A kind=class kind=enum\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype A base=B base=C\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype A frobnicate\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\ntype A\ntype A\n", Status::Duplicate, 3);
    ExpectBad("laml-types 1\ntype 9bad\n", Status::BadName, 2);
    ExpectBad("laml-types 1\nprop A.p B\n", Status::UnknownOwner, 2);
    ExpectBad("laml-types 1\ntype A\nprop Ap B\n", Status::Syntax, 3);       // no dot
    ExpectBad("laml-types 1\ntype A\nprop A. B\n", Status::Syntax, 3);
    ExpectBad("laml-types 1\ntype A\nprop A.p\n", Status::Syntax, 3);
    ExpectBad("laml-types 1\ntype A\nprop A.p A weird\n", Status::Syntax, 3);
    ExpectBad("laml-types 1\nenumval A\n", Status::Syntax, 2);
    ExpectBad("laml-types 1\nxmlns onlyuri\n", Status::Syntax, 2);
    {
        Catalog c;
        std::string withNul("laml-types 1\ntype A\0B\n", 22);
        Result r = Load(c, withNul);
        CHECK(r.status == Status::Syntax && r.line == 2);
    }
    {
        Limits lim;
        lim.maxTextLineChars = 16;
        Catalog c(lim);
        Result r = Load(c, "laml-types 1\ntype Aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
        CHECK(r.status == Status::LimitExceeded && r.line == 2);
        lim.maxTextLineChars = 100;
        lim.maxTextLines = 2;
        Catalog d(lim);
        r = Load(d, "laml-types 1\ntype A\ntype B\n");
        CHECK(r.status == Status::LimitExceeded && r.line == 3);
    }
    // a failure leaves the lines before it declared, nothing after
    {
        Catalog c;
        Result r = Load(c, "laml-types 1\ntype A\ntype B\nbogus\ntype C\n");
        CHECK(!r.ok() && c.Find("A") && c.Find("B") && !c.Find("C"));
    }
}

static void TestDeterminism()
{
    // Same input, same ids and the same ordering of everything observable.
    std::string text = "laml-types 1\ntype A.One\ntype A.Two base=A.One\nprop A.Two.x A.One\nxmlns urn:a A\n";
    Catalog a, b;
    CHECK(Load(a, text).ok() && Load(b, text).ok());
    CHECK(a.Resolve().empty() && b.Resolve().empty());
    CHECK(a.Find("A.Two")->id == b.Find("A.Two")->id && a.Find("A.Two")->id == 2);
    CHECK(a.ResolveXmlName("urn:a", "One").type == b.ResolveXmlName("urn:a", "One").type);
    CHECK(a.TypeCount() == b.TypeCount() && a.TypeCount() == 2);
}

static void TestSeed(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (!f)
        return;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    std::fclose(f);

    Catalog c;
    Result r = Load(c, text);
    if (!r.ok())
        std::printf("   seed load: %s line %zu: %s\n", StatusName(r.status), r.line, r.message.c_str());
    CHECK(r.ok());
    auto errs = c.Resolve();
    for (const Problem &p : errs)
        std::printf("   seed resolve: %s\n", p.message.c_str());
    CHECK(errs.empty());
    CHECK(c.warnings().empty());

    const char *kPres = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
    TypeId button = c.ResolveXmlName(kPres, "Button").type;
    TypeId control = c.ResolveXmlName(kPres, "Control").type;
    TypeId grid = c.ResolveXmlName(kPres, "Grid").type;
    TypeId page = c.ResolveXmlName(kPres, "Page").type;
    CHECK(button && control && grid && page);
    CHECK(c.IsAssignable(button, control) && c.IsAssignable(page, control) && !c.IsAssignable(control, button));
    CHECK(c.Find("Microsoft.UI.Xaml.DependencyObject") && c.IsAssignable(button, c.Find("Microsoft.UI.Xaml.DependencyObject")->id));
    CHECK(c.FindProperty(button, "Content") && c.FindProperty(button, "Width") && c.FindProperty(button, "Background"));
    CHECK(c.ContentProperty(button) && c.ContentProperty(button)->name == "Content");
    CHECK(c.ContentProperty(grid) && c.ContentProperty(grid)->name == "Children");
    CHECK(c.FindAttachedProperty(grid, "Row") && !c.FindProperty(grid, "Row"));
    CHECK(c.ResolveXmlName(kPres, "NotAControl").type == kNoType);
    const TypeInfo *vis = c.Find("Microsoft.UI.Xaml.Visibility");
    CHECK(vis && vis->kind == Kind::Enum && vis->enumValues.size() == 2);
    // value types resolve through the chain
    const Property *margin = c.FindProperty(button, "Margin");
    CHECK(margin && c.Find("Microsoft.UI.Xaml.Thickness") && margin->valueType == c.Find("Microsoft.UI.Xaml.Thickness")->id);
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestLookupAndIdentity();
    TestInheritance();
    TestProperties();
    TestUnknownTypes();
    TestXmlResolution();
    TestDeclarationErrors();
    TestResolveErrors();
    TestLimits();
    TestText();
    TestMalformedText();
    TestDeterminism();
    if (argc > 1)
        TestSeed(argv[1]);
    std::printf("types_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
