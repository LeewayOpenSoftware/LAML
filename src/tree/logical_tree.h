// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML logical tree (M3): the structure a decoded XBF node stream describes, as plain data. It is a
// description of what a loader WOULD create and set, not a set of live objects: nothing is instantiated,
// nothing is rendered, no UI toolkit type is involved, and the tree has no reference to a window, a
// renderer or a visual tree. Types and properties appear as catalog ids plus the resolution outcome
// (see ref_resolver.h), so metadata (the catalog) and the tree stay separate.
//
// How the tree is built (grounded in the node sequences documented in the MIT Microsoft.UI.Xaml
// ObjectWriterNode comments, see docs/TREE.md and PROVENANCE.md): the stream is a command stream for a
// stack machine with scopes ("frames"). A frame holds the object being initialised and the values that
// are waiting to be consumed. Only the operations whose meaning is stated there are interpreted; every
// other operation is kept as an explicit "not implemented" entry, never guessed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tree/ref_resolver.h"

namespace laml::tree {

constexpr uint32_t kNoNode = 0xffffffffu;

enum class ValueKind : uint8_t
{
    None,
    Constant,          // a persisted constant, as stored (not interpreted)
    Object,            // another logical node
    StaticResource,    // reference to a resource by key: constant holds the key; not looked up
    ThemeResource,     // same, theme resource
    TemplateBinding,   // source property in `property`
    MarkupExtension,   // the object in `object` produces the value (not evaluated)
    ResolvedProperty,  // a property reference used as the value (type converted)
    ResolvedType,      // a type reference used as the value (type converted)
    Deferred,          // the value is in another node stream (`targetStream`)
    NotImplemented,    // an operation LAML does not interpret; `note` says which
};
const char *ValueKindName(ValueKind kind);

struct LogicalValue
{
    ValueKind kind = ValueKind::None;
    xbf::Constant constant;
    uint32_t object = kNoNode;
    PropertyResolution property;
    TypeResolution type;
    uint32_t targetStream = 0;
    bool typeConverted = false;   // the stream says the constant goes through a type converter (not applied)
    std::string note;
};

struct SourcePos
{
    uint32_t stream = 0;
    uint32_t nodeIndex = 0;     // index in the decoded stream
    uint32_t offset = 0;        // byte offset in the stream
    uint32_t line = 0, column = 0;
};

struct Assignment
{
    PropertyResolution property;
    LogicalValue value;
    SourcePos pos;
};

struct Item
{
    LogicalValue value;
    bool hasKey = false;        // AddToDictionaryWithKey
    xbf::Constant key;
    bool dictionary = false;    // AddToDictionary(WithKey) rather than AddToCollection
    SourcePos pos;
};

struct LogicalNode
{
    uint32_t id = 0;
    uint32_t stream = 0;
    TypeResolution type;                 // the object's type; for a member-collection node, see `fromMember`
    bool fromMember = false;             // not created: it is the value of `member` on its parent (PushScopeGetValue)
    bool external = false;               // stands for an object that lives in another stream (deferred streams)
    uint32_t contextNode = kNoNode;      // external only: the node of the referencing stream it stands for (kNoNode = not linked)
    std::vector<uint32_t> linkedStreams; // streams that this node references (SetDeferredProperty, SetCustomRuntimeData), in order
    PropertyResolution member;
    uint32_t parent = kNoNode;           // node whose frame this node was created or fetched in
    bool hasInitialValue = false;        // created "with constant"
    xbf::Constant initialValue;
    bool initialTypeConverted = false;
    std::vector<Assignment> properties;
    std::vector<Item> items;
    bool hasName = false;                // x:Name through SetName
    xbf::Constant name;
    bool hasConnectionId = false;
    xbf::Constant connectionId;
    uint32_t conditionalDepth = 0;       // created inside this many conditional scopes (predicates are not evaluated)
    std::vector<std::string> notes;      // operations seen on this node that LAML does not interpret
    SourcePos pos;
};

enum class DiagKind : uint8_t
{
    InvalidReference,    // the file is wrong (index outside a table, retired slot)
    UnknownType,         // valid reference, not in the catalog
    UnknownProperty,
    NotImplemented,      // LAML does not interpret this yet
    MalformedStructure,  // the stream breaks the stack discipline (value missing, no object to set on, ...)
};
const char *DiagKindName(DiagKind kind);

struct Diagnostic
{
    DiagKind kind = DiagKind::MalformedStructure;
    std::string message;
    SourcePos pos;
};

struct Namespace
{
    std::u16string prefix;       // as stored (may be empty)
    std::string uri;
    SourcePos pos;
};

// How a deferred stream is reached from an earlier one. A stream after the first is the body of something the
// stream that references it only names: a template or resource body (SetDeferredProperty) or the content that
// a runtime-data payload tokenises (SetCustomRuntimeData).
enum class LinkVia : uint8_t { None, DeferredProperty, RuntimeData };
const char *LinkViaName(LinkVia via);

struct StreamLink
{
    bool linked = false;
    uint32_t fromStream = 0;
    uint32_t node = kNoNode;     // the node of fromStream that references this stream
    LinkVia via = LinkVia::None;
    PropertyResolution property; // DeferredProperty: the property the stream is the value of
};

struct StreamTree
{
    StreamLink link;                    // stream 0 and unreferenced streams stay unlinked
    xbf::StreamStatus status = xbf::StreamStatus::Ok;   // the decoder's result for this stream
    std::vector<uint32_t> roots;        // objects that finished (or were created) with no enclosing frame
    std::vector<Namespace> namespaces;
    std::u16string peerType;            // CheckPeerType text
    bool hasPeerType = false;
    uint32_t unclosedFrames = 0;        // frames still open at the end of the stream
};

struct TreeCounts
{
    size_t objects = 0, memberNodes = 0, assignments = 0, items = 0;
    size_t typesResolved = 0, typesUnknown = 0, typesInvalid = 0, typesUnimplemented = 0;
    size_t propertiesResolved = 0, propertiesUnknown = 0, propertiesInvalid = 0, propertiesUnimplemented = 0;
    size_t directives = 0;
    size_t retiredStableRefs = 0;        // trusted references that hit a retired slot (resolved by the slot's original name)
    size_t externalContexts = 0;         // placeholders for objects of another stream that a deferred stream acts on
    size_t linkedContexts = 0;           // ... of which the referencing stream is known and the placeholder points at its node
    size_t linkedStreams = 0;            // deferred streams reached from an earlier stream
    size_t orphanStreams = 0;            // streams after the first that no earlier stream references (not linked)
    size_t notImplementedOps = 0;
    size_t structureErrors = 0;
};

struct LogicalTree
{
    std::vector<LogicalNode> nodes;       // all streams; ids index this vector
    std::vector<StreamTree> streams;      // same order as the decoded streams
    std::vector<Diagnostic> diagnostics;
    TreeCounts counts;
    bool ok() const { return counts.structureErrors == 0; }   // structural soundness only; unknown types are not errors
};

struct BuildLimits
{
    uint32_t maxNodes = 1u << 22;
    uint32_t maxDepth = 4096;
    uint32_t maxDiagnostics = 10000;
};

// Build the logical tree of every stream in `decoded`. Always fills `out` as far as it got; a stream the
// decoder could not finish is built from the nodes it did decode and keeps its decoder status.
void BuildLogicalTree(const xbf::Decoded &decoded, const RefResolver &resolver, const BuildLimits &limits, LogicalTree &out);
inline void BuildLogicalTree(const xbf::Decoded &decoded, const RefResolver &resolver, LogicalTree &out)
{
    BuildLogicalTree(decoded, resolver, BuildLimits(), out);
}

// Indented text rendering of the tree (UTF-8), for tests and the dump tool.
std::string DumpLogicalTree(const LogicalTree &tree);

}  // namespace laml::tree
