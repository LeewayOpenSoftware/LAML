# LAML M3: reference resolution and the logical tree

Status: implemented on the local branch `m3-logical-tree`, host-tested. **Not pushed, not merged**: the owner asked for review and a later authorization (decision of 2026-11-02, card `levrt-m3`, `shared/levrt/OWNER-LAML-M3-2026-11-02.md` in the hive). M3 instantiates no control, renders nothing, has no visual tree, touches no shell or other subsystem.

## What exists

| Piece | Files | Purpose |
|---|---|---|
| Reference resolver | `src/tree/ref_resolver.{h,cpp}` | turns the type and property references of a decoded node stream into catalog entries, and says exactly why one cannot be resolved |
| Logical tree | `src/tree/logical_tree.{h,cpp}` | folds the node stream of every sub-stream into a tree of plain data (objects, property assignments, collection items), independent of any UI toolkit |
| Stable-slot recovery | `src/types/stable_index.{h,cpp}` (extended) | a retired stable slot keeps its original name and, for properties, its original owner and member |

M1 (`src/xbf`) and M2 (`src/types`) are unchanged in behaviour.

## Requirement by requirement (owner conditions)

1. **Resolve XBF references against the catalog, respecting the index tables and the format versions.** Untrusted references index the file's own type and property tables (through the M2 bridge); trusted references index the stable tables. Trusted references are resolved only for XBF 2.x with a loaded stable table and a resolved catalog; otherwise they are `Unimplemented` with the reason. A stable name is a short name and is looked up through one configurable xmlns URI of the catalog (default: the XAML presentation namespace).
2. **Extensible catalog, metadata separate from runtime objects.** The resolver and the tree depend only on the catalog's public interface. The tree holds `TypeId`s and resolution results, never pointers to catalog internals, and no runtime object exists anywhere. A different catalog gives the same tree shape (tested).
3. **Logical tree fully independent of any GUI or renderer.** `laml_tree` links `laml_types` and `laml_xbf` only; nothing in it names a window, a renderer, a brush or a layout.
4. **Explicit difference between invalid references, unknown types or properties, and features not yet implemented.** See "Outcomes" below; counts and per-item diagnostics keep them apart.
5. **Positive, negative and limit tests, plus a representative XBF.** See "Tests".
6. **No invented XAML/WinUI semantics, no proprietary code.** See "What the tree does and does not interpret".

## Outcomes

| Outcome | Meaning | Examples |
|---|---|---|
| `Resolved` | found in the catalog | stable type `Grid` with the seed catalog |
| `InvalidReference` | the file is wrong | index outside a table; the table's slot 0 (the Unknown entry); a retired slot whose original name cannot be recovered |
| `UnknownType` / `UnknownProperty` | the reference is valid, the catalog does not describe it | stable type `Canvas` with the 25-type seed; a property of a type the catalog lacks |
| `Unimplemented` | LAML does not support what is needed | trusted reference with no stable table; XBF 3.x; an unresolved catalog |
| `Directive` | markup directive, not a catalog type or property | x:Key-style entries flagged in the file |
| `Absent` | the node has no such operand | |

Tree-level diagnostics add `MalformedStructure` (the stack discipline of the stream is broken: a value missing, no object to apply an operation to, a scope closed that was never opened, values left unconsumed) and `NotImplemented` (an operation the tree does not interpret). `LogicalTree::ok()` is about structure only: unknown types never make a tree "not ok".

**Retired stable slots.** The stable tables keep retired entries (their current name is `UnknownType`, the original column carries `_DeletedN`). The table does not say which XAML compiler wrote a file, so an older file can legitimately use a slot that was retired later. This is real: `CustomTypesPage.xbf` uses the slot that the pinned table lists as `ProgressBar_Deleted0` and one `ProgressBar_IsIndeterminate_Deleted0` property slot. Such a reference is resolved by the slot's original name, flagged `retiredSlot`, and counted (`retiredStableRefs`). Only the table's own recorded name is used; nothing is guessed.

## What the tree does and does not interpret

The node stream is a command stream for a stack machine with scopes. The meaning of the operations used below is stated in the MIT-licensed Microsoft.UI.Xaml sources (`ObjectWriterNode.cpp` comments with the node sequences, `ObjectWriterRuntime.cpp` for what each step does: `CreateType`, `GetValue`, `SetValue`, `AddToCollection`, `AddToDictionary`), and was checked against the five real fixtures (all five fold with no structure error). Read, not copied (see PROVENANCE).

Interpreted (a frame = one scope; it holds the object being initialised and the values waiting to be consumed):

| Operation | Effect on the tree |
|---|---|
| `PushScope`, `PopScope`, `PushScopeAddNamespace`, `AddNamespace` | open or close a frame; record the namespace |
| `CreateType*`, `PushScopeCreateType*` (with or without constant) | create an object node in the frame; "with constant" keeps the initial value and whether a type converter is named |
| `EndInitPopScope` | close the frame; the object becomes a value on the parent frame (or a root if there is none) |
| `PushScopeGetValue(prop)` | a member node: the current value of a property of the enclosing object (typically a collection), recorded as a property assignment of the parent |
| `AddToCollection`, `AddToDictionary`, `AddToDictionaryWithKey` | consume a value into the frame's collection node (with the key when given) |
| `PushConstant`, `SetValue(prop)` | push a constant; consume a value into a property of the enclosing object |
| `SetValueConstant`, `...TypeConvertedConstant`, `...TypeConvertedResolvedProperty/Type`, `...FromStaticResource`, `...FromThemeResource`, `...FromTemplateBinding`, `...FromMarkupExtension` | property assignments with a typed value (constant, resource key, template-binding source, ...); resource keys are kept, never looked up; type converters are named, never applied |
| `SetName`, `SetConnectionId`, `CheckPeerType`, `SetDeferredProperty` | name, connection id, peer type text, link to the deferred stream number |

Kept but not interpreted, each reported as `NotImplemented`: `SetCustomRuntimeData` payloads (decoded in M1, noted on the node), `GetResourcePropertyBag`, `ProvideValue`, conditional scopes (the objects inside are kept and marked `conditionalDepth`; predicates are not evaluated), an object created in a frame that already has one.

**Deferred streams.** A stream after the first continues a scope of its parent. When an operation needs an object and the stream has none, the tree creates an explicit placeholder node (`external = true`, counted in `externalContexts`, one `NotImplemented` diagnostic) instead of failing; linking it to the parent stream through `SetDeferredProperty` / `SetCustomRuntimeData` is M4 work. `XbfVsmTest.v2.1.xbf` has three such streams.

## Tests (`tree_tests`, 973 checks, all pass; `ctest` 8 of 8)

| Group | What |
|---|---|
| Resolver | absent; stable type/property resolved (including attached and inherited); stable type the seed lacks; retired slot (resolved by original name, flagged); slot 0; index beyond the table; untrusted index into an empty file; no stable table; XBF 3.x; unresolved catalog; a different stable xmlns |
| Tree shape | a Page > Grid > Children > 2 Buttons + TextBlock stream built the way real streams are: names, peer type, namespaces, parents, collection items, each node the value of at most one slot |
| Metadata independence | the same stream against an empty catalog and against the seed gives the same node, item and assignment counts; unknown types are diagnostics, not errors |
| Negative | invalid references; no stable table (`Unimplemented`, not unknown); `SetValue` with no value; `AddToCollection` with nothing to add and with nothing to add to; member operations with no object; scope closed that was never opened; values left on the stack; scope left open; object created in an occupied frame; stream with no opening scope |
| Not interpreted | property bag, runtime data, conditional scope, `ProvideValue`, deferred property |
| Value forms | constant, type-converted constant, static and theme resource, template binding, resolved type, connection id, dictionary item with and without key, initial value |
| Limits | empty input; depth limit; node limit; diagnostic cap with exact counts kept; a stream the decoder could not finish keeps its status; two streams do not share frames |
| Real files | the five MIT fixtures: invariants (each node the value of at most one slot, roots are nobody's value), no invalid reference, no structure error, same shape without a catalog and without a stable table; `SampleXbf2.xbf` (a real page: 67 objects, 4 member nodes, 40 collection items, 216 assignments, 8 namespaces, peer type `Numbers.CalculatorScientificOperators`); `XbfVsmTest.v2.1.xbf` (7 streams, 3 external contexts); `CustomTypesPage.xbf` (2 retired stable references); the same file claiming format 3.0 loses stable resolution but keeps its shape |

Coverage of the new code was exercised through these tests; no line-coverage tool was run (none is set up in this repository), so the table above lists behaviours, not percentages.

## Known limits

1. **The seed catalog is tiny** (25 types, 21 of 1943 stable properties). On the real fixtures most references come out `UnknownType`/`UnknownProperty`, which is the honest size of the catalog gap, not a defect of the resolver. `SampleXbf2.xbf`: 3 types resolved, 64 unknown; 98 properties resolved, 118 unknown.
2. **No namespace in the stable tables.** Stable names are resolved through one xmlns URI; a name that exists in several type namespaces resolves to the first mapped one.
3. **Stable indexes are tied to a compiler generation.** The tables are pinned to one WinUI commit; the XBF carries no table version. Retired slots are the visible symptom; a slot that was *reused* for a different type in another generation would resolve wrongly and cannot be detected.
4. **Enum constants** are kept as (stable type id, number); mapping numbers to value names is not done (the catalog stores names only).
5. **Values are not typed.** A constant is kept as stored; a type converter is named, never applied; no property value is checked against the property's value type.
6. **Collections vs. single-value members** are not told apart beyond the stream's own operations; a `SetValue` of an object into a collection-typed property is not flagged.
7. **Operations that rely on semantics not documented in the sources read** are `NotImplemented` (list above).
8. **Deferred streams are not linked** to the property that references them.
9. **Single format family:** XBF 2.0/2.1 only, as in M1.

## Proposal for M4

Smallest useful next step, still without instantiation or rendering:
1. Link deferred streams to their referencing property and object, so a template or resource dictionary becomes a subtree (removes the external placeholders).
2. Interpret `SetCustomRuntimeData` payloads (resource dictionary keys and tokens, style setters, visual states) as tree data, using the M1 decoded records and the stream-offset tokens.
3. Check property assignments against the catalog's value types and enum value names (needs enum numeric values in the catalog metadata), reporting mismatches as a new diagnostic kind.
4. Grow the catalog for the controls the fixtures use, from public documentation, and report resolution coverage per fixture as a regression number.
5. A text dump tool (`xbftree`) next to `xbfdump`.
