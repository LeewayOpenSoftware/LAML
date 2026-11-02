# LAML

LAML: Leeway Application Markup Language, XAML-compatible UI for LiberNT

## Purpose

LAML is the Leeway Application Markup Language: a declarative UI layer for LiberNT, a XAML-compatible dialect with its own `.laml` extension and namespace, built on LevRT.

LAML is developed as its own repository until it is ready to join the operating
system tree at [LiberNT-LDW](https://github.com/LeewayOpenSoftware/LiberNT-LDW),
the Leeway Open Software fork of LiberNT.

## Status

**Early code.** The design document for LevRT and LAML together is in
[docs/DESIGN.md](docs/DESIGN.md). The first code is the XBF structure reader (`src/xbf`,
`tools/xbfdump`, `tests/xbf_tests.cpp`, described in [docs/XBF.md](docs/XBF.md)): it parses,
validates and describes the container of a binary XAML file. It does not decode nodes and
activates nothing. A second step (`src/xbf/xbf_nodes`, `xbfdump --nodes`) decodes the node streams into an
inert list of records and validates them; it still creates no objects.

M2 adds the type catalog and property system (`src/types`, described in [docs/TYPES.md](docs/TYPES.md)): a
native registry of types, namespaces, inheritance and property metadata with a text metadata format, a
read-only bridge to the XBF reader, and an optional import of the WinUI stable index tables. It still
creates no objects, renders nothing and has no visual tree.

M3 (local branch `m3-logical-tree`, not merged: awaiting review) resolves XBF references against the catalog and
builds a GUI-independent logical tree from the node streams ([docs/TREE.md](docs/TREE.md)).

## Clean-room rule

Only public Microsoft documentation, Wine and ReactOS sources, observable behaviour and
Microsoft-authored source that is explicitly MIT-licensed (owner policy; provenance, notices and
license audit are recorded in [PROVENANCE.md](PROVENANCE.md)) are used as references. No
proprietary Microsoft code, strings, disassembly, Windows SDK or operating-system `.winmd` or
`.xbf` data is copied into this repository; the only `.xbf` files in it are the MIT-licensed test
resources listed in PROVENANCE.md.

## License

[LGPL-2.1-or-later](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). The full text
is in [COPYING](COPYING). Imported third-party components are listed in
[PROVENANCE.md](PROVENANCE.md) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
