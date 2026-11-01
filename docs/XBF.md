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

## Tools and tests

* `xbfdump file.xbf` prints the header, tables and sub-stream index; exit 0 valid, 1 invalid.
* `tests/xbf_tests.cpp`: the five MIT fixtures (counts, versions, hash field, a string), header
  and offset errors, count and limit errors, every prefix of every fixture rejected, and a
  deterministic mutation sweep (about 22k mutated inputs) that must never crash and where an
  accepted file must have internally consistent tables.

## Not covered yet

Node stream decoding, the type/property index resolution against the stable XBF index tables,
version 1 files, the file-level hash check. They need the activation infrastructure that is not
there yet and are separate steps.
