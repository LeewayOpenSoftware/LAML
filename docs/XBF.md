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

`SetCustomRuntimeData` is followed by a payload (ResourceDictionary, Style, VisualStateGroupCollection
or DeferredElement data, several versions each) that this decoder does not read. The node and its
runtime data type number are kept and the stream's status becomes `OpaqueData`: decoding stops there,
which is reported and is not counted as a failure.

Nothing is created, resolved or run: ids stay ids. The text dump prints well-known ids as
`stable-type#N` / `stable-prop#N`.

## Tools and tests

* `xbfdump file.xbf` prints the header, tables and sub-stream index; exit 0 valid, 1 invalid.
* `xbfdump --nodes file.xbf` additionally decodes and prints the node streams as an indented tree.
* `tests/xbf_tests.cpp`: the five MIT fixtures (counts, versions, hash field, a string), header
  and offset errors, count and limit errors, every prefix of every fixture rejected, and a
  deterministic mutation sweep (about 22k mutated inputs) that must never crash and where an
  accepted file must have internally consistent tables.
* `tests/xbf_nodes_tests.cpp`: the five fixtures decoded (node counts, scope balance, conditional
  scopes, deferred and runtime-data nodes), hand-built streams for every rejection (opcode, scope,
  reference, truncation, string and 7-bit limits, depth and node limits), truncation of every
  fixture at every byte, 20,000 seeded byte mutations of the node streams, and stream-range damage.
  Also run under UBSan and ASan with no report.

## Not covered yet

The payload of `SetCustomRuntimeData`, the resolution of well-known (trusted) type and property
indexes, version 1 files, the file-level hash check, and anything that creates objects. Those need
the activation infrastructure that is not there yet and are separate steps.
