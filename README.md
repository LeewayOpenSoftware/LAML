# LAML

LAML: Leeway Application Markup Language, XAML-compatible UI for LiberNT

## Purpose

LAML is the Leeway Application Markup Language: a declarative UI layer for LiberNT, a XAML-compatible dialect with its own `.laml` extension and namespace, built on LevRT.

LAML is developed as its own repository until it is ready to join the operating
system tree at [LiberNT-LDW](https://github.com/LeewayOpenSoftware/LiberNT-LDW),
the Leeway Open Software fork of LiberNT.

## Status

**Design.** No code yet. The design document for LevRT and LAML together is in
[docs/DESIGN.md](docs/DESIGN.md).

## Clean-room rule

Only public Microsoft documentation, Wine and ReactOS sources, and observable
behaviour are used as references. No Microsoft code, strings, disassembly,
`.winmd` or `.xbf` data is copied into this repository.

## License

[LGPL-2.1-or-later](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html). The full text
is in [COPYING](COPYING). Imported third-party components are listed in
[PROVENANCE.md](PROVENANCE.md) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
