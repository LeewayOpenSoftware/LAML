// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// Tests of resource lookup. Expectations come from the documented XAML rules, not from upstream output.
#include <cstdio>
#include <string>

#include "../src/resources/resource_dictionary.h"

using namespace laml::resources;

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

static Value S(const char *s) { return Value(std::string(s)); }
static bool IsStr(const std::optional<Value> &v, const char *s)
{
    return v && std::holds_alternative<std::string>(*v) && std::get<std::string>(*v) == s;
}
static Key K(const char *n) { return Key{n, false}; }

static void TestBasics()
{
    auto d = ResourceDictionary::Create();
    CHECK(d->Insert(K("a"), S("1")));
    CHECK(!d->Insert(K("a"), S("2")));   // duplicate key
    CHECK(d->Count() == 1);
    ThemeState t;
    CHECK(IsStr(d->Lookup("a", t, nullptr), "1"));
    CHECK(!d->Lookup("b", t, nullptr));
    CHECK(d->Insert(Key{"a", true}, S("type")));   // implicit key space is separate
    CHECK(IsStr(d->Lookup(Key{"a", true}, t), "type"));
    CHECK(IsStr(d->Lookup(K("a"), t), "1"));
    CHECK(d->Remove(K("a")));
    CHECK(!d->Remove(K("a")));
    CHECK(!d->Lookup("a", t, nullptr));
    Value n = int64_t(7);
    CHECK(d->Insert(K("n"), n));
    CHECK(std::get<int64_t>(*d->Lookup("n", t, nullptr)) == 7);
}

static void TestMerged()
{
    auto root = ResourceDictionary::Create(), m1 = ResourceDictionary::Create(), m2 = ResourceDictionary::Create();
    m1->Insert(K("x"), S("m1"));
    m1->Insert(K("only1"), S("o1"));
    m2->Insert(K("x"), S("m2"));
    CHECK(root->AddMerged(m1));
    CHECK(root->AddMerged(m2));
    CHECK(!root->AddMerged(m2));   // already merged
    ThemeState t;
    CHECK(IsStr(root->Lookup("x", t), "m2"));       // the last merged dictionary wins
    CHECK(IsStr(root->Lookup("only1", t), "o1"));
    root->Insert(K("x"), S("own"));
    CHECK(IsStr(root->Lookup("x", t), "own"));      // own entries beat merged ones
    DictionaryPtr where;
    CHECK(root->Lookup(K("only1"), t, nullptr, kAll, &where));
    CHECK(where == m1);
    CHECK(!root->Lookup(K("only1"), t, nullptr, kLocalOnly));   // local scope does not see merged
    CHECK(root->RemoveMerged(m2));
    CHECK(!root->RemoveMerged(m2));
    root->Remove(K("x"));
    CHECK(IsStr(root->Lookup("x", t), "m1"));
    CHECK(!root->AddMerged(nullptr));
}

static void TestCycles()
{
    auto a = ResourceDictionary::Create(), b = ResourceDictionary::Create(), c = ResourceDictionary::Create();
    CHECK(!a->AddMerged(a));
    CHECK(a->AddMerged(b));
    CHECK(b->AddMerged(c));
    CHECK(!c->AddMerged(a));   // a -> b -> c -> a
    CHECK(!b->AddMerged(a));
    CHECK(!c->SetThemeDictionary("Light", a));   // cycle through a theme dictionary
    // a long chain is allowed to be built (tail appends), but lookups only visit kMaxMergeDepth levels
    auto head = ResourceDictionary::Create();
    DictionaryPtr cur = head;
    for (int i = 0; i < 200; i++)
    {
        auto next = ResourceDictionary::Create();
        if (i == 10)
            next->Insert(K("near"), S("n"));
        if (i == 150)
            next->Insert(K("far"), S("f"));
        CHECK(cur->AddMerged(next));
        cur = next;
    }
    ThemeState t;
    CHECK(IsStr(head->Lookup("near", t), "n"));
    CHECK(!head->Lookup("far", t));   // deeper than the bound: not found, not a hang or a crash
    CHECK(!head->Lookup("nothing", t));
    // a subtree deeper than the bound cannot be merged in the first place
    auto deep = ResourceDictionary::Create();
    DictionaryPtr c2 = deep;
    bool refused = false;
    for (int i = 0; i < 100 && !refused; i++)
    {
        auto parent = ResourceDictionary::Create();
        if (!parent->AddMerged(c2))
            refused = true;
        c2 = parent;
    }
    CHECK(refused);
}

static DictionaryPtr Themed(const char *light, const char *dark, const char *def = nullptr)
{
    auto owner = ResourceDictionary::Create();
    if (light)
    {
        auto l = ResourceDictionary::Create();
        l->Insert(K("brush"), S(light));
        owner->SetThemeDictionary("Light", l);
    }
    if (dark)
    {
        auto d = ResourceDictionary::Create();
        d->Insert(K("brush"), S(dark));
        owner->SetThemeDictionary("Dark", d);
    }
    if (def)
    {
        auto d = ResourceDictionary::Create();
        d->Insert(K("brush"), S(def));
        owner->SetThemeDictionary("Default", d);
    }
    return owner;
}

static void TestThemes()
{
    auto owner = Themed("white", "black", "grey");
    ThemeState t;
    t.app = Theme::Light;
    CHECK(IsStr(owner->Lookup("brush", t), "white"));
    t.app = Theme::Dark;
    CHECK(IsStr(owner->Lookup("brush", t), "black"));
    t.subtree = Theme::Light;   // RequestedTheme on a subtree overrides the app theme
    CHECK(IsStr(owner->Lookup("brush", t), "white"));
    t.subtree.reset();

    auto onlyDefault = Themed(nullptr, nullptr, "grey");
    CHECK(IsStr(onlyDefault->Lookup("brush", t), "grey"));   // falls back to "Default"
    auto onlyLight = Themed("white", nullptr);
    t.app = Theme::Dark;
    CHECK(!onlyLight->Lookup("brush", t));                   // no Dark, no Default

    // own entries beat theme entries; theme entries beat nothing
    owner->Insert(K("brush"), S("own"));
    CHECK(IsStr(owner->Lookup("brush", t), "own"));

    // merged dictionaries are searched before this dictionary's own theme dictionaries
    auto root = Themed("rootL", "rootD");
    auto m = ResourceDictionary::Create();
    m->Insert(K("brush"), S("merged"));
    root->AddMerged(m);
    CHECK(IsStr(root->Lookup("brush", t), "merged"));
    // but a theme dictionary of a merged dictionary is found through the merge
    auto root2 = ResourceDictionary::Create();
    root2->AddMerged(Themed("mL", "mD"));
    t.app = Theme::Light;
    CHECK(IsStr(root2->Lookup("brush", t), "mL"));
    CHECK(!root2->Lookup(K("brush"), t, nullptr, kLocalOnly | kMerged));   // no theme scope: not found

    // high contrast picks the variant, then "HighContrast", then the plain theme
    auto hc = Themed("w", "b");
    auto hcb = ResourceDictionary::Create();
    hcb->Insert(K("brush"), S("hc-black"));
    hc->SetThemeDictionary("HighContrastBlack", hcb);
    auto hcg = ResourceDictionary::Create();
    hcg->Insert(K("brush"), S("hc-generic"));
    hc->SetThemeDictionary("HighContrast", hcg);
    t.highContrast = HighContrast::Black;
    CHECK(IsStr(hc->Lookup("brush", t), "hc-black"));
    t.highContrast = HighContrast::White;   // no HighContrastWhite: the generic one
    CHECK(IsStr(hc->Lookup("brush", t), "hc-generic"));
    t.subtree = Theme::Dark;                // subtree requests dark -> HighContrastBlack
    t.highContrast = HighContrast::White;
    CHECK(IsStr(hc->Lookup("brush", t), "hc-black"));
    t.highContrast = HighContrast::None;
    t.subtree.reset();
    CHECK(IsStr(hc->Lookup("brush", t), "w"));
    CHECK(!hc->SetThemeDictionary("", ResourceDictionary::Create()));
}

static void TestGlobal()
{
    GlobalResources g;
    g.themeDictionaries = Themed("sysL", "sysD");
    auto sysOnly = ResourceDictionary::Create();
    ThemeState dk;
    dk.app = Theme::Dark;
    g.themeDictionaries->ActiveThemeDictionary(ThemeState{})->Insert(K("sysonly"), S("s"));
    g.themeDictionaries->ActiveThemeDictionary(dk)->Insert(K("sysonly"), S("s"));
    ThemeState t;
    auto page = ResourceDictionary::Create();
    CHECK(IsStr(page->Lookup(K("brush"), t, &g), "sysL"));
    CHECK(IsStr(page->Lookup(K("sysonly"), t, &g), "s"));
    CHECK(!page->Lookup(K("brush"), t, nullptr));                       // no globals given
    CHECK(!page->Lookup(K("brush"), t, &g, kLocalOnly | kMerged | kLocalTheme));   // scope excludes the global theme
    t.app = Theme::Dark;
    CHECK(IsStr(page->Lookup(K("brush"), t, &g), "sysD"));

    // Application.Resources overrides the global theme dictionaries
    g.applicationResources = ResourceDictionary::Create();
    g.applicationResources->Insert(K("brush"), S("app"));
    CHECK(IsStr(page->Lookup(K("brush"), t, &g), "app"));
    CHECK(IsStr(page->Lookup(K("sysonly"), t, &g), "s"));

    // an own entry still beats the globals
    page->Insert(K("brush"), S("page"));
    CHECK(IsStr(page->Lookup(K("brush"), t, &g), "page"));

    // the global theme is consulted once, after the merged dictionaries
    auto p2 = ResourceDictionary::Create();
    auto m = ResourceDictionary::Create();
    m->Insert(K("brush"), S("fromMerged"));
    p2->AddMerged(m);
    CHECK(IsStr(p2->Lookup(K("brush"), t, &g), "fromMerged"));
}

int main()
{
    TestBasics();
    TestMerged();
    TestCycles();
    TestThemes();
    TestGlobal();
    std::printf("resource_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
