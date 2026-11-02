// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the XBF -> type catalog bridge on the real MIT fixtures from Microsoft.UI.Xaml, plus
// hand-damaged structures. Usage: types_xbf_tests <fixture-dir>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/types/xbf_bridge.h"

using namespace laml::types;
namespace xbf = laml::xbf;

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

static std::vector<uint8_t> LoadFile(const std::string &path)
{
    std::vector<uint8_t> v;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return v;
    uint8_t b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0)
        v.insert(v.end(), b, b + n);
    std::fclose(f);
    return v;
}

static TypeDecl T(const std::string &name, const std::string &base = "")
{
    TypeDecl d;
    d.fullName = name;
    d.baseName = base;
    return d;
}

struct Loaded
{
    std::vector<uint8_t> bytes;
    xbf::File file;
    bool ok = false;
};

static Loaded Open(const std::string &dir, const char *name)
{
    Loaded l;
    l.bytes = LoadFile(dir + "/" + name);
    xbf::Result r = xbf::Parse(l.bytes.data(), l.bytes.size(), l.file);
    l.ok = !l.bytes.empty() && r.ok();
    return l;
}

static void TestNoTypesInFile(const std::string &dir)
{
    // Files that name no custom types: nothing to bind, nothing unknown.
    const char *names[] = {"XbfVsmTest.v2.1.xbf", "PageWithConditionalXaml.xbf", "SampleWithChecksum.xbf"};
    Catalog c;
    CHECK(c.Resolve().empty());
    for (const char *n : names)
    {
        Loaded l = Open(dir, n);
        CHECK(l.ok);
        XbfBinding b;
        CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, c, b));
        CHECK(b.types.size() == l.file.types.size() && b.properties.size() == l.file.properties.size());
        CHECK(b.unknownTypes == 0 && b.unknownProperties == 0 && b.malformed == 0);
    }
}

static void TestUnknownNamespaceForm(const std::string &dir)
{
    // CustomTypesPage: one type flagged Unknown whose namespace index points into the xmlns list.
    Loaded l = Open(dir, "CustomTypesPage.xbf");
    CHECK(l.ok);
    Catalog empty;
    CHECK(empty.Resolve().empty());
    XbfBinding b;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, empty, b));
    CHECK(b.types.size() == 1);
    CHECK(b.types[0].fullName == "Microsoft.ContactSupport.Views.ContactSupportBasePage");
    CHECK(b.types[0].binding == Binding::Unknown && b.unknownTypes == 1);
    CHECK((b.types[0].xbfFlags & xbf::kTypeIsUnknown) != 0);

    // A catalog that declares it resolves it, whatever the XBF flag says.
    Catalog c;
    CHECK(c.AddType(T("Microsoft.ContactSupport.Views.ContactSupportBasePage")).ok());
    CHECK(c.Resolve().empty());
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, c, b));
    CHECK(b.types[0].binding == Binding::Resolved && b.types[0].type == c.Find("Microsoft.ContactSupport.Views.ContactSupportBasePage")->id);
    CHECK(b.unknownTypes == 0);

    // The same type reached through an xmlns mapping.
    Catalog m;
    CHECK(m.AddType(T("Elsewhere.ContactSupportBasePage")).ok());
    CHECK(m.MapXmlNamespace("using:Microsoft.ContactSupport.Views", "Elsewhere").ok());
    CHECK(m.Resolve().empty());
    // "using:" URIs are read as type namespaces first, so the mapping is not consulted for them:
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, m, b));
    CHECK(b.types[0].binding == Binding::Unknown);
}

static void TestKnownTypesAndProperty(const std::string &dir)
{
    Loaded l = Open(dir, "SampleXbf2.xbf");
    CHECK(l.ok);
    CHECK(l.file.types.size() == 2 && l.file.properties.size() == 1);

    Catalog empty;
    CHECK(empty.Resolve().empty());
    XbfBinding b;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, empty, b));
    CHECK(b.unknownTypes == 2 && b.unknownProperties == 1 && b.malformed == 0);
    CHECK(b.types[0].fullName.size() > std::string("CalculatorButton").size());
    CHECK(b.types[0].fullName.size() >= 16 && b.types[0].fullName.substr(b.types[0].fullName.size() - 16) == "CalculatorButton");
    CHECK(b.properties[0].name == "ButtonId" && b.properties[0].ownerFullName == b.types[0].fullName);
    CHECK(b.properties[0].binding == Binding::Unknown);

    // Declare exactly what the file names (taking the names from the first pass) and bind again.
    Catalog c;
    CHECK(c.AddType(T("Test.Control")).ok());
    CHECK(c.AddType(T(b.types[0].fullName, "Test.Control")).ok());
    CHECK(c.AddType(T(b.types[1].fullName)).ok());
    PropertyDecl pd;
    pd.name = "ButtonId";
    pd.valueTypeName = b.types[1].fullName;
    CHECK(c.AddProperty("Test.Control", pd).ok());                   // declared on the BASE type
    CHECK(c.Resolve().empty());
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, c, b));
    CHECK(b.unknownTypes == 0 && b.unknownProperties == 0 && b.malformed == 0);
    CHECK(b.types[0].binding == Binding::Resolved && b.types[1].binding == Binding::Resolved);
    CHECK(b.properties[0].binding == Binding::Resolved && b.properties[0].property);
    CHECK(b.properties[0].owner == b.types[0].type);
    CHECK(b.properties[0].declaring == c.Find("Test.Control")->id);   // inherited: found through the base chain
    CHECK(b.properties[0].property->valueType == b.types[1].type);

    // Type declared but the property is not: type resolves, property does not.
    Catalog d;
    CHECK(d.AddType(T(b.types[0].fullName)).ok());
    CHECK(d.Resolve().empty());
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, d, b));
    CHECK(b.types[0].binding == Binding::Resolved && b.properties[0].binding == Binding::Unknown);
    CHECK(b.unknownTypes == 1 && b.unknownProperties == 1);
}

static void TestRecording(const std::string &dir)
{
    Loaded l = Open(dir, "SampleXbf2.xbf");
    CHECK(l.ok);
    Catalog c;
    CHECK(c.Resolve().empty());
    XbfBinding b;
    CHECK(BindXbfRecording(l.bytes.data(), l.bytes.size(), l.file, c, b));
    CHECK(c.TypeCount() == 2);
    CHECK(c.FindIncludingUnknown(b.types[0].fullName) && c.FindIncludingUnknown(b.types[0].fullName)->isUnknown());
    CHECK(!c.Find(b.types[0].fullName));
    // a second pass changes nothing
    CHECK(BindXbfRecording(l.bytes.data(), l.bytes.size(), l.file, c, b));
    CHECK(c.TypeCount() == 2);
    // plain BindXbf never records
    Catalog e;
    CHECK(e.Resolve().empty());
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), l.file, e, b));
    CHECK(e.TypeCount() == 0);
}

static void TestMalformed(const std::string &dir)
{
    Loaded l = Open(dir, "SampleXbf2.xbf");
    CHECK(l.ok);
    Catalog c;
    CHECK(c.Resolve().empty());
    XbfBinding b;

    // catalog not resolved: refuses, empty binding
    Catalog unresolved;
    CHECK(unresolved.AddType(T("A.B", "A.Missing")).ok());
    CHECK(!unresolved.Resolve().empty());
    CHECK(!BindXbf(l.bytes.data(), l.bytes.size(), l.file, unresolved, b));
    CHECK(b.types.empty() && b.properties.empty());

    // type namespace index out of range
    xbf::File f = l.file;
    f.types[0].namespaceIndex = 99;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.types[0].binding == Binding::Malformed && b.malformed >= 1);
    // a property whose owner is that malformed type is malformed too
    CHECK(b.properties[0].binding == Binding::Malformed);

    // name string index out of range
    f = l.file;
    f.types[1].nameIndex = 0xFFFFFFFFu;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.types[1].binding == Binding::Malformed);

    // namespace name string index out of range
    f = l.file;
    f.typeNamespaces[0].nameIndex = 0xFFFFFFFFu;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.types[0].binding == Binding::Malformed);

    // property owner index out of range, property name index out of range
    f = l.file;
    f.properties[0].typeIndex = 7;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.properties[0].binding == Binding::Malformed);
    f = l.file;
    f.properties[0].nameIndex = 1000000;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.properties[0].binding == Binding::Malformed);

    // directives are neither resolved nor unknown
    f = l.file;
    f.types[0].flags = xbf::kTypeIsMarkupDirective;
    f.properties[0].flags = xbf::kPropIsMarkupDirective;
    CHECK(BindXbf(l.bytes.data(), l.bytes.size(), f, c, b));
    CHECK(b.types[0].binding == Binding::Directive && b.properties[0].binding == Binding::Directive);
    CHECK(b.unknownTypes == 1 && b.unknownProperties == 0);   // only type 1 is left unknown

    // a damaged XBF file never reaches the bridge: the M1 reader rejects it, and keeps doing so
    std::vector<uint8_t> bad = l.bytes;
    bad[0] = 'Z';
    xbf::File bf;
    CHECK(xbf::Parse(bad.data(), bad.size(), bf).status == xbf::Status::BadMagic);
    // ... and a binding of its partial output is safe (empty lists)
    CHECK(BindXbf(bad.data(), bad.size(), bf, c, b));
    CHECK(b.types.empty() && b.properties.empty());
}

static void TestUtf8()
{
    CHECK(Utf16ToUtf8(u"abc") == "abc");
    CHECK(Utf16ToUtf8(u"é") == "\xC3\xA9");
    CHECK(Utf16ToUtf8(u"€") == "\xE2\x82\xAC");
    CHECK(Utf16ToUtf8(u"\xD83D\xDE00") == "\xF0\x9F\x98\x80");
    CHECK(Utf16ToUtf8(u"\xD83D") == "\xEF\xBF\xBD");        // unpaired high surrogate
    CHECK(Utf16ToUtf8(u"\xDE00x") == "\xEF\xBF\xBD" "x");   // unpaired low surrogate
    CHECK(Utf16ToUtf8(u"").empty());
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::printf("usage: types_xbf_tests <fixture-dir>\n");
        return 2;
    }
    std::string dir = argv[1];
    TestUtf8();
    TestNoTypesInFile(dir);
    TestUnknownNamespaceForm(dir);
    TestKnownTypesAndProperty(dir);
    TestRecording(dir);
    TestMalformed(dir);
    std::printf("types_xbf_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
