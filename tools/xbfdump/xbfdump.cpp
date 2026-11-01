// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// xbfdump: print the container structure of a .xbf file (header, string table, metadata lists,
// node-stream index). It never decodes nodes and never loads or runs anything.
//
//   xbfdump <file.xbf>           exit 0 = valid, 1 = invalid or unsupported (reason printed), 2 = usage/IO
//   xbfdump --nodes <file.xbf>   also decode the node streams and print them as a tree (inert: nothing is
//                                created or activated); exit 1 if a node stream is malformed
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/xbf/xbf_nodes.h"
#include "../../src/xbf/xbf_reader.h"

int main(int argc, char **argv)
{
    bool nodes = argc == 3 && std::strcmp(argv[1], "--nodes") == 0;
    if (argc != 2 && !nodes)
    {
        std::fputs("usage: xbfdump [--nodes] <file.xbf>\n", stderr);
        return 2;
    }
    const char *path = argv[argc - 1];
    FILE *f = std::fopen(path, "rb");
    if (!f)
    {
        std::fprintf(stderr, "xbfdump: cannot open %s\n", path);
        return 2;
    }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    {
        data.insert(data.end(), buf, buf + n);
        if (data.size() > laml::xbf::Limits().maxFileSize)
            break;
    }
    std::fclose(f);

    laml::xbf::File file;
    laml::xbf::Result r = laml::xbf::Parse(data.data(), data.size(), file);
    if (!r.ok())
    {
        std::printf("INVALID: %s at file offset 0x%llx: %s\n", laml::xbf::StatusName(r.status),
                    (unsigned long long)r.errorOffset, r.message.c_str());
        if (file.version.major || file.version.minor)
            std::printf("version field: %u.%u\n", file.version.major, file.version.minor);
        return 1;
    }
    std::fputs(laml::xbf::Dump(data.data(), data.size(), file).c_str(), stdout);
    if (nodes)
    {
        laml::xbf::Decoded decoded;
        laml::xbf::DecodeNodes(data.data(), data.size(), file, decoded);
        std::fputs(laml::xbf::DumpNodes(data.data(), data.size(), file, decoded).c_str(), stdout);
        return decoded.ok() ? 0 : 1;
    }
    return 0;
}
