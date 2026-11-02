// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// xbftree: fold a .xbf file into the logical tree (inert: nothing is created, activated or rendered)
// and print what the type catalog resolves.
//
//   xbftree [--stable <dir>] [--types <catalog.types> ...] [--tree] <file.xbf>
//
// --stable <dir>   directory with StableXbfTypeIndexes.g.csv / StableXbfPropertyIndexes.g.csv
// --types <file>   text catalog (may be repeated; loaded in order)
// --tree           also print the indented tree
// Prints the counts, then every distinct type and property reference with its outcome and how often
// it occurs. Exit 0 = tree built, 1 = the file or a stream is malformed, 2 = usage/IO.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tree/logical_tree.h"
#include "tree/validate.h"

using namespace laml;

static bool Slurp(const std::string &path, std::vector<uint8_t> &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t b[65536];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) out.insert(out.end(), b, b + n);
    std::fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    std::string stableDir, input;
    std::vector<std::string> catalogs;
    bool tree = false;
    for (int i = 1; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "--stable") && i + 1 < argc) stableDir = argv[++i];
        else if (!std::strcmp(argv[i], "--types") && i + 1 < argc) catalogs.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--tree")) tree = true;
        else if (argv[i][0] == '-') { input.clear(); break; }
        else input = argv[i];
    }
    if (input.empty())
    {
        std::fputs("usage: xbftree [--stable <dir>] [--types <catalog>]... [--tree] <file.xbf>\n", stderr);
        return 2;
    }

    types::Catalog catalog;
    for (const std::string &c : catalogs)
    {
        std::vector<uint8_t> t;
        if (!Slurp(c, t)) { std::fprintf(stderr, "xbftree: cannot read %s\n", c.c_str()); return 2; }
        types::Result r = types::LoadText(catalog, reinterpret_cast<const char *>(t.data()), t.size());
        if (!r.ok()) { std::fprintf(stderr, "xbftree: %s line %zu: %s\n", c.c_str(), r.line, r.message.c_str()); return 2; }
    }
    std::vector<types::Problem> problems = catalog.Resolve();
    for (const types::Problem &p : problems) std::fprintf(stderr, "xbftree: catalog: %s\n", p.message.c_str());
    if (!problems.empty()) return 2;

    types::StableTable stable;
    bool haveStable = false;
    if (!stableDir.empty())
    {
        std::vector<uint8_t> t, p;
        if (!Slurp(stableDir + "/StableXbfTypeIndexes.g.csv", t) || !Slurp(stableDir + "/StableXbfPropertyIndexes.g.csv", p))
        {
            std::fputs("xbftree: cannot read the stable index tables\n", stderr);
            return 2;
        }
        haveStable = stable.LoadTypes(reinterpret_cast<const char *>(t.data()), t.size()).ok() &&
                     stable.LoadProperties(reinterpret_cast<const char *>(p.data()), p.size()).ok();
        if (!haveStable) { std::fputs("xbftree: the stable index tables are invalid\n", stderr); return 2; }
    }

    std::vector<uint8_t> data;
    if (!Slurp(input, data)) { std::fprintf(stderr, "xbftree: cannot open %s\n", input.c_str()); return 2; }
    xbf::File file;
    xbf::Result pr = xbf::Parse(data.data(), data.size(), file);
    if (!pr.ok())
    {
        std::printf("INVALID: %s: %s\n", xbf::StatusName(pr.status), pr.message.c_str());
        return 1;
    }
    xbf::Decoded decoded;
    xbf::DecodeNodes(data.data(), data.size(), file, decoded);
    tree::RefResolver resolver(data.data(), data.size(), file, catalog, haveStable ? &stable : nullptr);
    tree::LogicalTree t;
    tree::BuildLogicalTree(decoded, resolver, t);

    const tree::TreeCounts &c = t.counts;
    std::printf("streams=%zu objects=%zu members=%zu items=%zu assignments=%zu externalContexts=%zu\n", t.streams.size(),
                c.objects, c.memberNodes, c.items, c.assignments, c.externalContexts);
    std::printf("types: resolved=%zu unknown=%zu invalid=%zu unimplemented=%zu\n", c.typesResolved, c.typesUnknown,
                c.typesInvalid, c.typesUnimplemented);
    std::printf("properties: resolved=%zu unknown=%zu invalid=%zu unimplemented=%zu\n", c.propertiesResolved,
                c.propertiesUnknown, c.propertiesInvalid, c.propertiesUnimplemented);
    std::printf("notImplemented=%zu structureErrors=%zu\n", c.notImplementedOps, c.structureErrors);

    std::map<std::string, size_t> typeCount, propCount;
    for (const tree::LogicalNode &n : t.nodes)
    {
        if (!n.fromMember && !n.external)
            typeCount[std::string(tree::RefStatusName(n.type.status)) + "\t" + n.type.name]++;
        for (const tree::Assignment &a : n.properties)
            propCount[std::string(tree::RefStatusName(a.property.status)) + "\t" + a.property.ownerName + "." + a.property.name]++;
    }
    std::puts("-- types (status, name, objects)");
    for (const auto &kv : typeCount) std::printf("%s\t%zu\n", kv.first.c_str(), kv.second);
    std::puts("-- properties (status, owner.name, assignments)");
    for (const auto &kv : propCount) std::printf("%s\t%zu\n", kv.first.c_str(), kv.second);
    tree::ValidationReport vr;
    tree::Validate(t, catalog, resolver, vr);
    std::puts("-- validation");
    std::fputs(tree::DumpValidation(vr, 40).c_str(), stdout);
    if (tree) std::fputs(tree::DumpLogicalTree(t).c_str(), stdout);
    return t.ok() ? 0 : 1;
}
