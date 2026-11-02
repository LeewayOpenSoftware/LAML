// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of the optional stable XBF index import: the real WinUI tables (MIT fixtures), malformed
// tables, and the coverage comparison against the seed catalog.
// Usage: stable_index_tests <winui-metadata-fixture-dir> <seed-catalog-file>
#include <cstdio>
#include <string>

#include "../src/types/stable_index.h"

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

static std::string Slurp(const std::string &path)
{
    std::string s;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0)
        s.append(b, n);
    std::fclose(f);
    return s;
}

static Result LT(StableTable &t, const std::string &s) { return t.LoadTypes(s.data(), s.size()); }
static Result LP(StableTable &t, const std::string &s) { return t.LoadProperties(s.data(), s.size()); }

static void TestRealTables(const std::string &dir)
{
    std::string types = Slurp(dir + "/StableXbfTypeIndexes.g.csv");
    std::string props = Slurp(dir + "/StableXbfPropertyIndexes.g.csv");
    CHECK(!types.empty() && !props.empty());

    StableTable t;
    CHECK(LP(t, props).status == Status::UnknownOwner);   // types first
    Result r = LT(t, types);
    CHECK(r.ok());
    r = LP(t, props);
    if (!r.ok())
        std::printf("   props: %s line %zu: %s\n", StatusName(r.status), r.line, r.message.c_str());
    CHECK(r.ok());

    // counts, as measured on the pinned files
    CHECK(t.TypeSlots() == 984 && t.LiveTypes() == 851);
    CHECK(t.PropertySlots() == 2475 && t.LiveProperties() == 1943);

    // known entries
    CHECK(t.Type(0) && t.Type(0)->retired);
    CHECK(t.Type(1) && t.Type(1)->name == "Byte" && !t.Type(1)->retired);
    CHECK(t.Type(983) && t.Type(983)->name == "SplitMenuFlyoutItem");
    CHECK(!t.Type(984));
    CHECK(t.FindType("Byte") == 1 && t.FindType("SplitMenuFlyoutItem") == 983 && t.FindType("NoSuchType") == -1);
    CHECK(t.Property(301) && t.Property(301)->name == "UIElement_IsTapEnabled" && t.Property(301)->owner == "UIElement" &&
          t.Property(301)->member == "IsTapEnabled");
    CHECK(t.FindProperty("UIElement", "IsTapEnabled") == 301 && t.FindProperty("UIElement", "Nope") == -1);
    CHECK(t.Property(1) && t.Property(1)->owner == "GeneratorPosition" && t.Property(1)->member == "Index");
    CHECK(!t.Property(2475));

    // retired slots keep their number and the original name, and are not findable by a name
    CHECK(t.Type(15) && t.Type(15)->retired && t.Type(15)->original == "DataPackage_Deleted0" && t.Type(15)->name.empty());
    CHECK(t.FindType("DataPackage_Deleted0") == -1 && t.FindType("UnknownType") == -1);
    CHECK(t.Property(104) && t.Property(104)->retired);

    // every live property has a live owner type
    size_t orphans = 0;
    for (const StableProperty &p : t.properties())
        if (!p.retired && t.FindType(p.owner) < 0)
            orphans++;
    CHECK(orphans == 0);

    // reloading replaces the table
    CHECK(LT(t, "UnknownType,UnknownType,0\nFoo,Foo,1\n").ok());
    CHECK(t.TypeSlots() == 2 && t.PropertySlots() == 0 && t.FindType("Byte") == -1);
}

static void TestMalformed()
{
    const std::string ok0 = "UnknownType,UnknownType,0\n";
    StableTable t;
    CHECK(LT(t, "").status == Status::Syntax);
    CHECK(LT(t, "\n\n").status == Status::Syntax);
    CHECK(LT(t, "Byte,Byte,1\n").status == Status::Syntax);                       // index != row (and no row 0)
    CHECK(LT(t, "Foo,Foo,0\n").status == Status::Syntax);                         // row 0 must be Unknown
    CHECK(LT(t, ok0 + "A,A,2\n").status == Status::Syntax);                       // gap
    CHECK(LT(t, ok0 + "A,A,1\nB,B,1\n").status == Status::Syntax);                // repeat
    CHECK(LT(t, ok0 + "A,A\n").status == Status::Syntax);                         // two fields
    CHECK(LT(t, ok0 + "A,A,1,x\n").status == Status::Syntax);                     // four fields
    CHECK(LT(t, ok0 + "A,A,x\n").status == Status::Syntax);                       // non numeric
    CHECK(LT(t, ok0 + "A,A,\n").status == Status::Syntax);
    CHECK(LT(t, ok0 + "A,A,-1\n").status == Status::Syntax);
    CHECK(LT(t, ok0 + "A,A,1234567890\n").status == Status::Syntax);              // too long
    CHECK(LT(t, ok0 + "A B,A B,1\n").status == Status::BadName);
    CHECK(LT(t, ok0 + ",A,1\n").status == Status::BadName);
    CHECK(LT(t, ok0 + "A,A,1\nA,A,2\n").status == Status::Duplicate);
    CHECK(LT(t, ok0 + "A,A,1\nA_Deleted0,UnknownType,2\nA_Deleted1,UnknownType,3\n").ok());   // retired may repeat
    CHECK(t.TypeSlots() == 4 && t.LiveTypes() == 1);
    CHECK(LT(t, ok0 + "A,A,1\r\nB,B,2").ok() && t.TypeSlots() == 3);              // CRLF, no final newline
    CHECK(LT(t, std::string("\xEF\xBB\xBF") + ok0).ok());                          // BOM
    CHECK(LT(t, ok0 + std::string(2000, 'x') + ",a,1\n").status == Status::LimitExceeded);
    CHECK(t.TypeSlots() == 0);                                                     // failure leaves it empty

    // property table
    CHECK(LT(t, ok0 + "A,A,1\nAB,AB,2\n").ok());
    const std::string p0 = "UnknownType_UnknownProperty,UnknownType_UnknownProperty,0\n";
    CHECK(LP(t, "").status == Status::Syntax);
    CHECK(LP(t, "A_x,A_x,0\n").status == Status::Syntax);                          // row 0 must be Unknown
    CHECK(LP(t, p0 + "Zzz_x,Zzz_x,1\n").status == Status::UnknownOwner);           // no known type prefix
    CHECK(LP(t, p0 + "A_,A_,1\n").status == Status::UnknownOwner);                 // empty member
    CHECK(LP(t, p0 + "A_x,A_x,1\nA_x,A_x,2\n").status == Status::Duplicate);
    CHECK(t.PropertySlots() == 0);
    CHECK(LP(t, p0 + "A_x,A_x,1\nAB_y,AB_y,2\nA_B_z,A_B_z,3\n").ok());
    CHECK(t.Property(2) && t.Property(2)->owner == "AB" && t.Property(2)->member == "y");   // longest prefix
    CHECK(t.Property(3) && t.Property(3)->owner == "A" && t.Property(3)->member == "B_z");   // A_B not a type
    CHECK(LP(t, p0 + "A_x,A_x,1\nA_y_Deleted0,UnknownType_UnknownProperty,2\n").ok());
    CHECK(t.Property(2)->retired && t.LiveProperties() == 1);
}

static void TestCoverage(const std::string &dir, const std::string &seedPath)
{
    StableTable t;
    std::string types = Slurp(dir + "/StableXbfTypeIndexes.g.csv");
    std::string props = Slurp(dir + "/StableXbfPropertyIndexes.g.csv");
    CHECK(LT(t, types).ok() && LP(t, props).ok());

    std::string seed = Slurp(seedPath);
    Catalog c;
    CHECK(!seed.empty() && LoadText(c, seed.data(), seed.size()).ok());
    CHECK(c.Resolve().empty());

    const char *pres = "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
    Coverage cov = Compare(t, c, pres);
    std::printf("   coverage of the seed catalog: types %zu/%zu, properties %zu/%zu\n", cov.typesFound, cov.liveTypes,
                cov.propertiesFound, cov.liveProperties);
    CHECK(cov.liveTypes == 851 && cov.liveProperties == 1943);
    CHECK(cov.typesFound + cov.missingTypes.size() == cov.liveTypes);
    CHECK(cov.propertiesFound + cov.missingProperties.size() == cov.liveProperties);
    CHECK(cov.typesFound > 10 && cov.typesFound < 100);                  // a small seed, honestly small
    // a few that must be found and a few that must not
    bool buttonMissing = false, thicknessMissing = false;
    for (const std::string &n : cov.missingTypes)
    {
        if (n == "Button") buttonMissing = true;
        if (n == "Thickness") thicknessMissing = true;
    }
    CHECK(!buttonMissing && !thicknessMissing);
    bool heightMissing = false, tapMissing = false;
    for (const std::string &n : cov.missingProperties)
    {
        if (n == "FrameworkElement_Height") heightMissing = true;
        if (n == "UIElement_IsTapEnabled") tapMissing = true;
    }
    CHECK(!heightMissing);   // declared in the seed
    CHECK(tapMissing);       // not declared in the seed

    // wrong xmlns: nothing resolves
    Coverage none = Compare(t, c, "urn:nothing");
    CHECK(none.typesFound == 0 && none.propertiesFound == 0);
    // an empty catalog covers nothing
    Catalog e;
    CHECK(e.Resolve().empty());
    Coverage zero = Compare(t, e, pres);
    CHECK(zero.typesFound == 0 && zero.propertiesFound == 0 && zero.liveTypes == 851);
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3)
    {
        std::printf("usage: stable_index_tests <winui-metadata-dir> <seed-catalog>\n");
        return 2;
    }
    TestRealTables(argv[1]);
    TestMalformed();
    TestCoverage(argv[1], argv[2]);
    std::printf("stable_index_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
