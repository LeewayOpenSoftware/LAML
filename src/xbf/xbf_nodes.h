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
#include <string>
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
    OpaqueData,      // stops at custom runtime data, whose payload this decoder does not decode (not an error)
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
    size_t opaqueStreams = 0;
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
