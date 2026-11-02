# LAML M2: type catalog and property system

Status: implemented, host-tested. Scope fixed by the owner decision of 2026-11-02 (card `levrt-m2`): a native, extensible type registry; no UI object instantiation, no rendering, no visual tree.

## What exists

| Piece | Files | Purpose |
|---|---|---|
| Type catalog | `src/types/type_catalog.{h,cpp}` | types (identity, namespace, kind, flags), inheritance, property metadata (instance and attached), enum values, content property, xmlns to type-namespace mapping, name resolution, hierarchy and property queries |
| Text metadata | `src/types/metadata_text.cpp`, `LoadText` | a plain, line-oriented format to declare all of the above; the seed catalog `data/xaml-core.types` is written in it |
| XBF bridge | `src/types/xbf_bridge.{h,cpp}` | read-only: for each type and property a parsed XBF (M1 reader) names, says Resolved / Unknown / Directive / Malformed against a catalog |
| Stable index import (optional) | `src/types/stable_index.{h,cpp}` | loads the WinUI stable XBF index tables from CSV files on demand and compares them with a catalog |

Nothing here instantiates an object, resolves a type to code, renders, or builds a tree. A type is a description.

## Design

- **Independent of WinUI.** The catalog has its own model (`TypeInfo`, `Property`, `Kind`, flags). No class, field or enum is taken from `Microsoft.UI.Xaml`; the only WinUI-shaped input is the optional stable index import, which is data read from files the caller supplies.
- **Two phases.** Declare (`AddType`, `AddProperty`, `AddEnumValue`, `MapXmlNamespace`), then `Resolve()`. Declaration order is free: a base type or a property value type may be declared later. `Resolve()` links bases, rejects missing bases, cycles and chains deeper than `Limits::maxHierarchyDepth` (256), checks that every content property is an instance property of the type or a base, and links property value types. After a successful `Resolve()` the catalog is closed to declarations (`Status::Frozen`); after a failed one declarations may be added and `Resolve()` called again. Hierarchy and property queries answer "no" on an unresolved catalog.
- **Identity.** A `TypeId` is the 1-based declaration order; the same input gives the same ids. A type's identity is its full name (`Namespace.Sub.Name`, namespace = everything before the last dot, case sensitive).
- **Unknown types.** `RecordUnknown(name)` adds a placeholder with `kTypeUnknown`; `Find` never returns it, `FindIncludingUnknown` does. Declaring the name later (before `Resolve()`) turns the placeholder into the real type with the same id. It is allowed on a resolved catalog because it records a miss, not a declaration.
- **Property resolution.** `FindProperty` walks the type then its bases, nearest first; a derived property hides a base property of the same name. Attached properties are kept apart: `FindAttachedProperty` finds them on the owner and its bases, `FindProperty` and `AllProperties` never return them. `AllProperties` lists base properties first with overrides replaced in place. `ContentProperty` walks the chain for the nearest declared content property.
- **xmlns resolution.** An xmlns URI maps to an ordered list of type namespaces; `ResolveXmlName(uri, local)` tries them in order and reports what it tried.
- **Value types are soft.** A property whose value type is not declared keeps `valueType == kNoType` and is listed in `Catalog::warnings()`; it does not stop `Resolve()`. A missing base or a cycle does.
- **Bounds.** Every count, name length, text line length and hierarchy depth has a limit (`Limits`); names are validated as dotted identifiers (ASCII letters, digits, `_`, any byte >= 0x80, not starting with a digit).

## Text metadata format

```
laml-types 1
xmlns <uri> <type-namespace>
source <id> <location> <revision> <license>
type <FullName> [kind=class|struct|enum|interface|primitive|directive] [abstract] [sealed] [flags] [base=<FullName>] [content=<Property>] [src=<source id>]
prop <OwnerFullName>.<Name> <ValueFullName> [attached] [readonly] [dependency] [nullable]
enumval <OwnerFullName> <Value>[=<number>]
```
M4 additions: `source` records say where metadata came from (`src=` ties a type to one; an undeclared source is `UnknownSource`); an enum is numbered completely (`Value=<decimal or 0x-hex>`) or not at all; `flags` marks an enum whose values are bit flags; `nullable` marks a property whose value may be absent. See [M4.md](M4.md).
`#` starts a comment at the start of a token; a UTF-8 BOM and CRLF are accepted; the first meaningful line must be `laml-types 1`. The loader stops at the first bad line and reports its number; lines before it stay declared.

## XBF bridge: what was learned from the real files

The M1 reader exposes the XBF type, property and namespace lists. Reading the five Microsoft fixtures shows:

1. **Only custom types are listed.** `CustomTypesPage.xbf` has 1 type, `SampleXbf2.xbf` has 2 types and 1 property, the other three have none. Built-in types (Button, Grid, ...) are not in the XBF lists: the node stream refers to them by *stable index* into a fixed table (see below). The bridge therefore resolves what the file declares; resolving the stable-indexed references is M3 work.
2. **Namespace index of an Unknown type points into the xmlns list.** `CustomTypesPage.xbf`: the type has flag `0x2` (unknown), `namespace 5`, and the file has no type namespaces but xmlns 5 is `using:Microsoft.ContactSupport.Views`. A known type (`SampleXbf2.xbf`) indexes the type namespace list. The bridge reads the Unknown case that way and strips `using:`. **This rests on one fixture file**; if the real rule differs (for example by XBF version) the Unknown-type full names would be wrong while the Resolved path stays correct.
3. A `using:` URI is read as a type namespace directly; other URIs go through the catalog's xmlns mapping.

## WinUI metadata tables: evaluation

Pinned upstream: `microsoft/microsoft-ui-xaml` commit `8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b`, MIT.

- **What the repository offers.** The type system of the XAML core is generated at build time (`dxaml/xcp/tools/XCPTypesAutoGen`, `dxaml/xcp/components/metadata/MetadataAPI.cpp`, `TypeTableStructs.h`) from IDL and C# model files; the generated C++ tables are bound to the core's classes. Two data files are checked in and self-contained: `StableXbfTypeIndexes.g.csv` (984 rows) and `StableXbfPropertyIndexes.g.csv` (2475 rows). They number the well-known types and properties an XBF may reference by index. They are the only part usable without the core.
- **What they contain, measured on the pinned files** (`stable_index_tests`): 851 live types and 1943 live properties; 133 type slots and 532 property slots are retired (kept so numbers stay stable). Names only: no namespace, no base type, no value type, no flags. A property is `<Type>_<Member>`; for every live property exactly one live type name is a prefix, so the split is unambiguous in this data.
- **What we did.** An optional importer (`StableTable`): loads the CSVs on demand, validates them strictly (indexes equal row numbers, names well-formed, no duplicates, retired slots kept), and `Compare` reports coverage of a catalog. The fixtures are byte-identical copies used only by tests. **LAML embeds none of this and needs none of it at run time.**
- **Coverage of the seed catalog** (`data/xaml-core.types`, hand-written from public documentation names): 25 of 851 live types and 21 of 1943 live properties. That number is the honest size of the gap; the seed is an example of the format, not a platform.
- **Not done, on purpose.** No generated catalog from the tables (they carry no inheritance, namespaces or value types, so a catalog built from them would be a list of names with nothing to resolve); no use of the generated C++ tables.

## Compatibility gaps and unresolved mappings

1. **Built-in types referenced by stable index** (the common case in real XBF) are not mapped to catalog types. Needs: the stable table plus a catalog that declares the platform types under the XAML presentation namespace. M3.
2. **No namespaces for the stable names.** The tables give `Button`, not `Microsoft.UI.Xaml.Controls.Button`. The mapping goes through the xmlns list of the catalog; which namespace a name belongs to is not in the data.
3. **No inheritance, value types, defaults, flags or enum values for the platform types** beyond the hand-written seed. They would have to come from public documentation or from the WinUI generator inputs (IDL, `XamlGen` model); not imported.
4. **Namespace index rule for Unknown XBF types** rests on one fixture (above).
5. **Generics, collections, nullable and array types** are not modelled (no type arguments); a generic type would be a plain name.
6. **Type converters, markup extensions, content-model rules (which child types a content property accepts), default values, property change callbacks, attribute metadata** are not modelled. `content=` records only which property takes element content.
7. **Case-sensitive, ordinal names**; XAML itself is case sensitive for types and properties, but no normalisation (Unicode, `x:` prefixes) is done.
8. **Attached property lookup walks the owner's bases**; whether the platform allows inherited attached lookups for every case is not verified.
9. **Directive names** (`x:Key`, `x:Name`) are reported by the bridge as `Directive` and never looked up.
10. **Version handling.** The bridge is tested on XBF 2.x files only (the M1 reader's range).

## Tests

| Binary | Checks | What |
|---|---|---|
| `types_tests` | 294 | lookup and identity, inheritance and distances, property resolution (hiding, attached, content, unresolved value type), unknown types, xmlns resolution, declaration errors, resolve errors (missing base, cycles, depth, bad content property, frozen), limits, text format (valid and about 20 malformed inputs, NUL, long lines, partial load), determinism, seed catalog |
| `types_xbf_tests` | 97 | bridge on the real fixtures (no types, Unknown-namespace form, known types and a property through a base type, recording), hand-damaged indexes, directive flags, UTF-16 to UTF-8 |
| `stable_index_tests` | 67 | the real stable tables (counts, entries, retired slots), about 30 malformed-table cases, coverage of the seed |

The M1 tests (`xbf_tests`, `xbf_nodes_tests`, `anim_tests`, `resource_tests`) still pass unchanged: `src/xbf` was not modified.
