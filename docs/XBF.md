# XBF structure reader

Scope of this step (card levrt-m1): parse, validate, detect the format version and describe the
container structure of a binary XAML (`.xbf`) file. **No node decoding and no activation**: nothing
in a file is interpreted as code, types are never resolved or created, and no runtime is needed.

## Layout read by `laml::xbf::Parse`

All integers are little-endian. Offsets in the header are relative to file offset 12.

| Where | What |
|---|---|
| 0 | magic `XBF\0` |
| 4, 8 | size of the metadata stream, size of the node stream; `12 + both` must equal the file size |
| 12 | metadata stream: major and minor version (u32 each) |
| 20 | header: six u64 offsets (string table, assemblies, type namespaces, types, properties, xml namespaces), then a 64-byte hash field |
| 132.. | string table (offset 0x78): `u32 count`, then per string `u32 length` + UTF-16 units, plus a NUL unit from 2.1 on |
| lists | assemblies (8 bytes per item), type namespaces (8), types (12), properties (12), xml namespaces (4); each `u32 count` + items; they follow each other without gaps and the last ends at the metadata size |
| node stream | `u32 count` of sub-streams, `count` pairs (node offset, line offset) relative to the end of this index, then the sub-stream data (not decoded) |

Supported: 2.0 and 2.1. A version 1 file is recognised and reported as `UnsupportedVersion` with its
version filled in; any other version is `UnsupportedVersion` too.

## What is validated

Magic, version, size arithmetic in 64 bits, offsets (first list at 0x78, non-decreasing, inside the
metadata, contiguous), every count against `Limits`, every string against its region, list sizes
against their regions, the node-stream index (inside the stream, ordered, non-overlapping). Index
references (assembly, string, namespace) that point out of range are reported as warnings, not
errors, because the format also holds stable built-in type indexes that this step does not resolve.

## Node streams

`src/xbf/xbf_nodes.{h,cpp}` (`DecodeNodes`, `DumpNodes`) turn each sub-stream into a flat, inert list
of node records. The format is a command stream, not a XAML node tree: each node is an opcode byte
followed by its operands, and nesting is expressed by scope-opening and scope-closing opcodes.

* Node stream *i* is the byte range `[nodeOffset(i), lineOffset(i))` and its line stream
  `[lineOffset(i), nodeOffset(i+1))` (the last one ends at the end of the node-stream region), all
  relative to the end of the stream index table.
* Opcodes: 1 PushScope, 2 PopScope, 3 AddNamespace, 4 PushConstant, 7 SetValue, 8 AddToCollection,
  9 AddToDictionary, 10 AddToDictionaryWithKey, 11 CheckPeerType, 12 SetConnectionId, 13 SetName,
  14 GetResourcePropertyBag, 15 SetCustomRuntimeData, 17 SetDeferredProperty, 18-25 the combined
  push-scope / create-type forms, 26-32 and 36 the SetValue variants, 33 EndInitPopScope,
  34/35 ProvideStatic/ThemeResourceValue, 38/39 Begin/EndConditionalScope, 40
  EndInitProvideValuePopScope, 139 ProvideValue. Anything else (0, 5, 6, 16, 37, 41..138,
  140..255) is rejected: the optimiser-only values never occur in a file.
* Operands: a type or property reference is 16 bits, little endian: 15-bit id plus a "trusted" flag.
  Untrusted ids index the file's type/property tables; trusted ids are indexes into the fixed
  table of well-known types and properties, which this step cannot resolve, so they are only
  limited to below 0x2000. String and xml-namespace ids use the file tables only. Inline strings are a
  32-bit length plus UTF-16. Integers in counts and stream numbers are 7-bit encoded (at most 5
  bytes).
* Constants: a kind byte (1 false, 2 true, 3 float, 4 signed, 5 shared string, 6 thickness, 7 grid
  length, 8 color, 9 inline string, 10 null string, 11 enum) and its payload. A grid length is
  stored as the unit (32 bits) then the value (float); an enum as a 16-bit well-known type id plus a
  32-bit value.
* Line info: the line stream is triples of 7-bit integers (stream-offset delta, zigzag line delta,
  zigzag column delta); a node takes the last entry at or before its offset.

Validation per stream: known opcode; every operand in range of its stream; table references inside
their tables; deferred and runtime-data stream numbers inside the stream index and not the stream
itself; scopes never closed more often than opened; limits on node count, nesting depth and string
length. A stream ends in a `StreamStatus`; other streams are still decoded. A scope left open at
the end is a warning, not an error: in a multi-stream file the deferred streams continue a scope of
their parent (all four single-stream fixtures end balanced).

### Runtime data payloads

`SetCustomRuntimeData` is followed by a type number (7-bit) and a payload whose layout depends on it;
the decoder reads all of them into `RuntimeData` records (`Node::runtimeData`), so the stream goes on
at the right byte afterwards. Containers are a 7-bit count followed by the elements; a string is a
16-bit id into the string table; a "token" is a 7-bit stream-offset token kept as stored (it names a
point in a deferred stream and is not resolved); a bool is a persisted constant (true or false).

| Family | Type numbers (version) | Contents |
|---|---|---|
| ResourceDictionary | 371 (1), 7 (2), 10 (3), 12 (4) | resources: key -> token; strings to un-defer automatically; conditional resources (3, 4); implicit style keys (1, 2). Versions 1-3 store explicit and implicit maps separately; 4 stores one map whose key is a string id plus a fixed 64-bit hash with the type flag in bit 0. Versions 3 and 4 end with the conditionally declared table (token -> predicates of type reference + argument string). |
| Style | 2 (1), 8 (2), 11 (3) | setters: flags byte (bit 0 theme resource, 1 static resource, 2 string, 3 object, 4 property resolved, 5 container value, 6 token for itself, 7 mutable), the property (well-known/table reference, or name + owner type), then the value (string id, constant, or token). From version 3 a setter that is a token for itself has no property. Version 3 ends with the conditionally declared table. |
| VisualStateGroupCollection | 420 (1), 1 (2), 3 (3), 4 (4), 5 (5) | state-to-group map, states (name, storyboard token, flag; from 2: property setter tokens, trigger values; from 3: extensible and collection trigger tokens; 5: static resource trigger tokens), groups, transitions, the "unexpected tokens" flag, the transition lookup tables, the whole-collection token, and (4, 5) the seen name directives. |
| DeferredElement | 745 (1), 6 (2), 9 (3) | element name; from 2 the non-deferred properties (property, constant); from 3 the realize flag. |

Strict checks: a count larger than the bytes left is a truncation, more than the limit is rejected,
ids must be in their tables, a bool must be a bool, setter flags must fit in a byte. A type number
that is not in the table (including 0) stops that stream with status `OpaqueData`; this is the
documented list of unsupported versions: everything except the numbers above. In the corpus the only
versions present are ResourceDictionary 4 and VisualStateGroupCollection 5; the other versions are
covered by hand-built payloads in the tests (derived from the format description, not from a file),
so they are as reliable as that reading of the format.

Nothing is created, resolved or run: ids and tokens stay as stored. The text dump prints well-known ids as
`stable-type#N` / `stable-prop#N`.

## Tools and tests

* `xbfdump file.xbf` prints the header, tables and sub-stream index; exit 0 valid, 1 invalid.
* `xbfdump --nodes file.xbf` additionally decodes and prints the node streams as an indented tree,
  with the contents of each runtime data payload under its node.
* `tests/xbf_tests.cpp`: the five MIT fixtures (counts, versions, hash field, a string), header
  and offset errors, count and limit errors, every prefix of every fixture rejected, and a
  deterministic mutation sweep (about 22k mutated inputs) that must never crash and where an
  accepted file must have internally consistent tables.
* `tests/xbf_nodes_tests.cpp`: the five fixtures decoded (node counts, scope balance, conditional
  scopes, deferred and runtime-data nodes, the decoded payload contents), hand-built streams for every
  rejection (opcode, scope, reference, truncation, string and 7-bit limits, depth and node limits),
  hand-built runtime data of every family and version (each followed by another node to prove the
  framing, each cut at every byte), truncation of every fixture at every byte, 20,000 seeded byte
  mutations of the node streams, and stream-range damage.
  Also run under UBSan and ASan with no report.

## Not covered yet

The resolution of well-known (trusted) type and property indexes and of stream-offset tokens, version 1 files, the file-level hash check, and anything that creates objects. Those need
the activation infrastructure that is not there yet and are separate steps.
