# Provenance

Every component imported into LAML from another project must be listed here,
with its license and with its copyright and license notices preserved in the
imported files. A component that is not listed here must not be in the tree.

Rules:

- Record the exact upstream commit (or release tag) the files were taken from.
- Keep the upstream copyright and license headers in every imported file.
- Describe every local modification; an unmodified import says "none".
- Add the component's required notices to [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
- Only clean-room sources are acceptable: public documentation, Wine, ReactOS
  and other free-software projects under compatible licenses.

| Component | Upstream URL | Commit / tag | License | Files | Modifications | Imported by / date |
|-----------|--------------|--------------|---------|-------|---------------|--------------------|
| Microsoft.UI.Xaml XBF container format (description, constants, structure names) | https://github.com/microsoft/microsoft-ui-xaml | `8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b` | MIT (root LICENSE, Copyright (c) Microsoft Corporation; the source files carry the MIT header) | none copied. `src/xbf/xbf_reader.{h,cpp}` is written for LAML; it reproduces the layout and the field and flag names read from `dxaml/xcp/core/inc/XamlBinaryMetadata.h`, `XamlSerializationHelper.h`, `dxaml/xcp/core/Parser/XamlBinaryMetadataReader2.cpp` and `XamlBinaryFormatReader2.cpp` | Not a code import: the reader is a new bounds-checked implementation; only the format knowledge (magic, version words, offset table, string table layout with the NUL terminator from 2.1, list item sizes, node-stream index, flag values) comes from the upstream. Dependencies: none. Integration status: standalone library, tool and tests; not connected to any runtime; the node stream is not decoded | Meredith (Leeway Open Software), 2026-11-02; owner policy option 3, god card levrt-m1 |
| Microsoft.UI.Xaml test resources `.xbf` | https://github.com/microsoft/microsoft-ui-xaml | `8da148dcd8ed65ea8b8ae664fbe45c9cc82e372b` | MIT (same root LICENSE; Microsoft-authored test files of that repository) | `tests/fixtures/winui-ui-xaml/{SampleXbf2,CustomTypesPage,XbfVsmTest.v2.1,SampleWithChecksum,PageWithConditionalXaml}.xbf`, byte-identical, hashes in `SHA256SUMS.txt`; origin paths in `tests/fixtures/winui-ui-xaml/README.txt` | none | Test inputs only for the structure reader. Dependencies: none. Integration status: tests only | Meredith, 2026-11-02 |
