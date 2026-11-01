// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML XBF node-stream decoder: turns the node streams of an XBF 2.x file into an inert list of
// node records and validates them. It creates no objects, resolves no types to code and activates
// nothing; every reference is only range-checked against the tables xbf_reader already parsed.
//
// The opcode numbers, the operand layout of each node and the persisted constant kinds come from
// the MIT-licensed Microsoft.UI.Xaml sources listed in PROVENANCE.md. The decoder itself is written
// for LAML: every read is bounds-checked, every count is limited, and a malformed stream ends in a
// status, never in a crash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xbf_reader.h"

namespace laml::xbf {

// Serialized node opcodes (the values are part of the file format).
enum class NodeType : uint8_t
{
    PushScope = 1,
    PopScope = 2,
    AddNamespace = 3,
    PushConstant = 4,
    SetValue = 7,
    AddToCollection = 8,
    AddToDictionary = 9,
    AddToDictionaryWithKey = 10,
    CheckPeerType = 11,
    SetConnectionId = 12,
    SetName = 13,
    GetResourcePropertyBag = 14,
    SetCustomRuntimeData = 15,
    SetDeferredProperty = 17,
    PushScopeAddNamespace = 18,
    PushScopeGetValue = 19,
    PushScopeCreateTypeBeginInit = 20,
    PushScopeCreateTypeWithConstantBeginInit = 21,
    PushScopeCreateTypeWithTypeConvertedConstantBeginInit = 22,
    CreateTypeBeginInit = 23,
    CreateTypeWithConstantBeginInit = 24,
    CreateTypeWithTypeConvertedConstantBeginInit = 25,
    SetValueConstant = 26,
    SetValueTypeConvertedConstant = 27,
    SetValueTypeConvertedResolvedProperty = 28,
    SetValueTypeConvertedResolvedType = 29,
    SetValueFromStaticResource = 30,
    SetValueFromTemplateBinding = 31,
    SetValueFromMarkupExtension = 32,
    EndInitPopScope = 33,
    ProvideStaticResourceValue = 34,
    ProvideThemeResourceValue = 35,
    SetValueFromThemeResource = 36,
    BeginConditionalScope = 38,
    EndConditionalScope = 39,
    EndInitProvideValuePopScope = 40,
    ProvideValue = 139,   // written for markup-extension values; carries no operands
};

const char *NodeTypeName(NodeType t);

enum class ConstKind : uint8_t
{
    None = 0,
    BoolFalse,
    BoolTrue,
    Float,
    Signed,
    SharedString,   // index into the string table
    Thickness,
    GridLength,
    Color,
    UniqueString,   // UTF-16 text stored inline
    NullString,
    Enum,
};

// A persisted metadata reference: an index into a table of the file, or - when `trusted` - an index
// into the fixed table of well-known types/properties (not resolvable here, only range-limited).
struct Ref
{
    uint16_t id = 0;
    bool trusted = false;
    bool present = false;
};

struct Constant
{
    ConstKind kind = ConstKind::None;
    int64_t integer = 0;         // Signed, Color, SharedString index, Enum value
    uint16_t enumType = 0;       // Enum: stable type index
    float real[4] = {};          // Float [0]; GridLength [0]=value; Thickness [0..3]
    int32_t gridUnit = 0;        // GridLength unit as stored
    std::u16string text;         // UniqueString
};

// ---- SetCustomRuntimeData payloads -------------------------------------------------------------
// Inert copies of the four runtime-data families. String fields are string-table ids; "token"
// fields are stream-offset tokens exactly as stored (a token names a point inside a deferred stream,
// it is not resolved here).

enum class RuntimeDataKind : uint8_t { None, ResourceDictionary, Style, VisualStateGroupCollection, DeferredElement };
const char *RuntimeDataKindName(RuntimeDataKind k);

struct RdKey
{
    uint16_t stringId = 0;
    bool isType = false;        // implicit (type-keyed) resource
    uint64_t hashAndType = 0;   // version 4 stores the hash and the type flag (bit 0); older versions 0
};
struct RdEntry { RdKey key; uint32_t token = 0; };
struct RdConditional { RdKey key; std::vector<uint32_t> tokens; };
struct PredicateRef { Ref type; uint16_t argumentsString = 0; };
struct ConditionallyDeclared { uint32_t token = 0; std::vector<PredicateRef> predicates; };

struct SetterEssence
{
    uint8_t flags = 0;          // bit0 theme res, 1 static res, 2 string, 3 object, 4 property resolved, 5 container, 6 token for self, 7 mutable
    Ref property;               // when the property is resolved
    uint16_t propertyName = 0;  // otherwise: name (string id) ...
    Ref ownerType;              // ... and owner type
    bool hasPropertyName = false;
    Constant container;         // flags bit 5
    uint16_t stringValue = 0;   // flags bit 2
    uint32_t token = 0;         // static / theme / object / self
};

struct VisualStateEssence
{
    uint16_t name = 0;
    uint32_t storyboardToken = 0;
    bool hasStoryboard = false;
    std::vector<uint32_t> propertySetterTokens;
    std::vector<std::vector<uint32_t>> triggerValues;
    std::vector<uint32_t> extensibleTriggerTokens, triggerCollectionTokens, staticResourceTriggerTokens;
};
struct VisualStateGroupEssence { uint16_t name = 0; bool hasDynamicTimelines = false; uint32_t selfToken = 0; };
struct VisualTransitionEssence { uint16_t toState = 0, fromState = 0; uint32_t token = 0; };

struct RuntimeData
{
    RuntimeDataKind kind = RuntimeDataKind::None;
    uint32_t typeIndex = 0;     // CustomWriterRuntimeDataTypeIndex as stored
    uint32_t version = 0;       // 1-based version within the family

    // ResourceDictionary
    std::vector<RdEntry> resources;                  // explicit and implicit entries together
    std::vector<uint16_t> autoUndeferral;            // string ids
    std::vector<RdConditional> conditionalResources;
    std::vector<uint16_t> legacyImplicitTemplates, legacyImplicitStyleKeys;   // versions 1 and 2 only

    // Style
    std::vector<SetterEssence> setters;

    // VisualStateGroupCollection
    std::vector<uint32_t> visualStateToGroup;
    std::vector<VisualStateEssence> visualStates;
    std::vector<VisualStateGroupEssence> visualStateGroups;
    std::vector<VisualTransitionEssence> visualTransitions;
    bool unexpectedTokensDetected = false;
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, uint32_t>> transitionMap;   // (from, to) -> transition
    std::vector<uint32_t> defaultTransitionByGroup;
    uint32_t entireCollectionToken = 0;
    std::vector<uint16_t> seenNameDirectives;

    // DeferredElement
    uint16_t elementName = 0;
    std::vector<std::pair<Ref, Constant>> nonDeferredProperties;
    bool realize = false;

    // trailing table for ResourceDictionary 3/4 and Style 3
    std::vector<ConditionallyDeclared> conditionallyDeclared;
};

struct Node
{
    NodeType type = NodeType::PushScope;
    uint32_t offset = 0;         // offset of the opcode inside its node stream
    uint32_t line = 0, column = 0;   // from the line stream; 0 when there is none
    Ref type1, property1, property2, xmlNamespace;   // operands by role (see xbf_nodes.cpp)
    Constant constant;
    std::u16string text;         // namespace prefix, peer type name, conditional arguments (as stored)
    uint32_t targetStream = 0;   // SetDeferredProperty / SetCustomRuntimeData: referenced stream
    std::vector<uint16_t> resourceStrings;   // SetDeferredProperty: string ids; static first, then theme
    uint32_t staticResourceCount = 0, themeResourceCount = 0;
    uint32_t runtimeDataType = 0;            // SetCustomRuntimeData: CustomWriterRuntimeDataTypeIndex
    std::shared_ptr<const RuntimeData> runtimeData;   // SetCustomRuntimeData: the decoded payload
    uint32_t depth = 0;          // scope depth before the node
};

enum class StreamStatus : uint8_t
{
    Ok,
    BadRange,        // the stream's byte range is inconsistent with the stream index
    BadOpcode,       // an opcode that is not valid in a serialized stream
    Truncated,       // an operand runs past the end of the stream
    BadReference,    // a table index, stream index or constant kind is out of range
    BadScope,        // a scope is closed that was never opened
    LimitExceeded,
    OpaqueData,      // stops at data this decoder cannot frame (not produced for the known runtime data versions)
};

const char *StreamStatusName(StreamStatus s);

struct DecodeLimits
{
    uint32_t maxNodesPerStream = 1u << 22;
    uint64_t maxTotalNodes = 1ull << 24;
    uint32_t maxDepth = 4096;
    uint32_t maxStringChars = 1u << 20;
    uint32_t maxResourceStrings = 1u << 20;
};

struct DecodedStream
{
    StreamStatus status = StreamStatus::Ok;
    std::string message;
    uint32_t errorOffset = 0;        // inside the stream
    uint32_t nodeBytes = 0;
    uint32_t lineBytes = 0;
    uint32_t finalDepth = 0;         // scope depth at the end; non-zero is reported as a warning
    uint32_t finalConditionalDepth = 0;
    std::vector<Node> nodes;
};

struct Decoded
{
    std::vector<DecodedStream> streams;
    std::vector<std::string> warnings;
    size_t failedStreams = 0;        // streams that ended in an error status (OpaqueData is not an error)
    size_t opaqueStreams = 0;        // streams that stopped at data this decoder does not know
    bool ok() const { return failedStreams == 0; }
};

// Decode every node stream of an already parsed file. Always fills `out` as far as it got.
// A malformed stream ends in a status on that stream; other streams are still decoded.
void DecodeNodes(const uint8_t *data, size_t size, const File &file, const DecodeLimits &limits, Decoded &out);
inline void DecodeNodes(const uint8_t *data, size_t size, const File &file, Decoded &out)
{
    DecodeNodes(data, size, file, DecodeLimits(), out);
}

// Readable text tree of the decoded streams (UTF-8), one line per node, indented by scope depth.
std::string DumpNodes(const uint8_t *data, size_t size, const File &file, const Decoded &decoded);

}  // namespace laml::xbf
