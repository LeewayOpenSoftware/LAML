// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML type catalog: a native, extensible registry of types, their namespaces, inheritance and
// property metadata, with type resolution from names and XML namespaces. It is independent of
// WinUI: nothing here depends on, or is shaped by, Microsoft.UI.Xaml internals. Metadata comes in
// through AddType / AddProperty or through the plain-text format in metadata_text.cpp.
//
// It instantiates nothing: no objects, no rendering, no visual tree. Types are descriptions.
//
// Two phases: declare (AddType, AddProperty, MapXmlNamespace), then Resolve(), which links base
// types and property value types by name, rejects cycles and reports what could not be resolved.
// Hierarchy queries (IsAssignable, FindProperty, AllProperties) need a resolved catalog.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace laml::types {

using TypeId = uint32_t;
constexpr TypeId kNoType = 0;

enum class Kind : uint8_t { Class, Struct, Enum, Interface, Primitive, MarkupDirective };
const char *KindName(Kind kind);

constexpr uint32_t kTypeAbstract = 0x01;
constexpr uint32_t kTypeSealed = 0x02;
constexpr uint32_t kTypeUnknown = 0x04;   // placeholder for a name seen in markup but not declared
constexpr uint32_t kTypeFlags = 0x08;     // enum whose values are bit flags (any OR of the values is valid)

constexpr uint32_t kPropAttached = 0x01;
constexpr uint32_t kPropReadOnly = 0x02;
constexpr uint32_t kPropDependency = 0x04;
constexpr uint32_t kPropNullable = 0x08;  // the value may be absent (a nullable wrapper around the value type)

enum class Status : uint8_t
{
    Ok,
    Frozen,           // declarations are closed after a successful Resolve()
    EmptyName,
    BadName,          // not a dotted identifier
    Duplicate,        // type, property, enum value or xml namespace mapping declared twice
    UnknownOwner,     // property or enum value for a type that was not declared
    LimitExceeded,
    BadKind,          // enum values on a non-enum, content property that does not exist, ...
    Unresolved,       // Resolve() found a base type that is not declared, or a cycle
    Syntax,           // text loader: malformed line
    UnknownSource,    // a type names a source that was not declared
};
const char *StatusName(Status status);

struct Result
{
    Status status = Status::Ok;
    std::string message;
    size_t line = 0;                // text loader only; 1-based
    bool ok() const { return status == Status::Ok; }
};

struct Limits
{
    uint32_t maxTypes = 1u << 20;
    uint32_t maxPropertiesPerType = 4096;
    uint32_t maxEnumValuesPerType = 4096;
    uint32_t maxNameChars = 1024;
    uint32_t maxHierarchyDepth = 256;
    uint32_t maxXmlMappings = 1u << 16;
    uint32_t maxSources = 1u << 12;
    uint32_t maxTextLineChars = 8192;
    uint32_t maxTextLines = 1u << 22;
};

struct TypeDecl
{
    std::string fullName;           // "Namespace.Sub.Name"; the namespace is everything before the last dot
    Kind kind = Kind::Class;
    uint32_t flags = 0;
    std::string baseName;           // full name, may be declared later; empty for a root
    std::string contentProperty;    // name of a property declared on this type or a base; empty for none
    std::string source;             // id of a declared Source (AddSource); empty = not stated
};

// Where the metadata of a type came from, so that every entry can be traced to a public document.
struct Source
{
    std::string id;                 // short identifier used by `src=` in the text format
    std::string location;           // URL or repository path of the public document
    std::string revision;           // commit, version or retrieval date that pins the document
    std::string license;            // licence of the document (e.g. MIT) or "public-documentation"
};

struct PropertyDecl
{
    std::string name;               // plain identifier
    std::string valueTypeName;      // full name; may stay unresolved (reported by Resolve)
    uint32_t flags = 0;
};

struct Property
{
    std::string name;
    TypeId owner = kNoType;
    std::string valueTypeName;
    TypeId valueType = kNoType;     // set by Resolve() when the value type is declared
    uint32_t flags = 0;
};

struct TypeInfo
{
    TypeId id = kNoType;
    std::string ns;
    std::string name;
    std::string fullName;
    Kind kind = Kind::Class;
    uint32_t flags = 0;
    std::string baseName;
    TypeId base = kNoType;
    std::string contentProperty;
    std::vector<Property> properties;       // declared on this type only
    std::vector<std::string> enumValues;    // Enum kind only, declaration order
    std::vector<int64_t> enumNumbers;       // parallel to enumValues when the numbers are declared; else empty
    std::string source;                     // id of the Source this entry was taken from; empty = not stated
    bool isUnknown() const { return (flags & kTypeUnknown) != 0; }
};

struct Problem
{
    Status status;
    std::string message;
};

// Result of resolving an XML name (xmlns URI + local name) to a type.
struct XmlResolution
{
    TypeId type = kNoType;
    std::string tried;              // the full names looked at, in order, for diagnostics
};

class Catalog
{
public:
    Catalog() : Catalog(Limits()) {}
    explicit Catalog(const Limits &limits);

    const Limits &limits() const { return limits_; }

    // ---- declaration phase -------------------------------------------------------------
    Result AddType(const TypeDecl &decl);
    Result AddProperty(const std::string &ownerFullName, const PropertyDecl &decl);
    Result AddEnumValue(const std::string &ownerFullName, const std::string &value);
    // The same with the value's number. An enum is either fully numbered or not at all: mixing the two
    // forms on one enum is BadKind.
    Result AddEnumValue(const std::string &ownerFullName, const std::string &value, int64_t number);
    // Declare where metadata comes from; ids are unique. TypeDecl::source must name a declared one.
    Result AddSource(const Source &source);
    // An xmlns URI maps to one or more type namespaces, searched in the order they were added.
    Result MapXmlNamespace(const std::string &uri, const std::string &typeNamespace);

    // Link bases and value types, check content properties, reject cycles and over-deep hierarchies.
    // Returns every problem found; the catalog counts as resolved only when there are none. After a
    // failed Resolve() declarations may still be added and Resolve() called again.
    std::vector<Problem> Resolve();
    bool IsResolved() const { return resolved_; }
    // Not errors: a property value type that is not declared (common while the catalog covers only
    // part of a platform) stays unresolved with valueType == kNoType and is listed here.
    const std::vector<Problem> &warnings() const { return warnings_; }

    // A placeholder with the Unknown flag for a name met in markup but not declared. Never found by
    // Find(); found by FindIncludingUnknown(). Allowed on a resolved catalog (it records a miss, it
    // is not a declaration) and idempotent.
    TypeId RecordUnknown(const std::string &fullName);

    // ---- lookup -------------------------------------------------------------------------
    const TypeInfo *Find(const std::string &fullName) const;                // declared types only
    const TypeInfo *FindIncludingUnknown(const std::string &fullName) const;
    const TypeInfo *Find(const std::string &typeNamespace, const std::string &name) const;
    const TypeInfo *Get(TypeId id) const;
    size_t TypeCount() const { return types_.size(); }                      // including placeholders

    // xmlns URI + local name, e.g. ("http://schemas.microsoft.com/winfx/2006/xaml/presentation", "Button").
    XmlResolution ResolveXmlName(const std::string &uri, const std::string &localName) const;

    const std::vector<Source> &sources() const { return sources_; }
    const Source *FindSource(const std::string &id) const;

    // ---- enum values (the enum must be declared; the catalog need not be resolved) ------
    // True when the enum declares numbers for its values.
    bool EnumIsNumbered(TypeId type) const;
    // A numbered enum accepts a number that is one of its values, or - for a flags enum - any OR of them
    // (zero included). An unnumbered enum accepts nothing: ask EnumIsNumbered first.
    bool EnumAccepts(TypeId type, int64_t number) const;
    // Name of the value with that number; nullptr if there is none.
    const std::string *EnumNameOf(TypeId type, int64_t number) const;

    // ---- resolved-catalog queries -------------------------------------------------------
    // True when `derived` is `base` or inherits from it. False for unknown ids or an unresolved catalog.
    bool IsAssignable(TypeId derived, TypeId base) const;
    // Property by name on the type or, nearest first, on its bases; `declaring` receives the type
    // that declares it. Attached properties are not instance properties and are not found here.
    const Property *FindProperty(TypeId type, const std::string &name, TypeId *declaring = nullptr) const;
    // Attached property, looked up on the owner type and its bases (declared with kPropAttached).
    const Property *FindAttachedProperty(TypeId owner, const std::string &name) const;
    // All instance properties visible on the type: base ones first; a derived property replaces a
    // base property of the same name in place.
    std::vector<const Property *> AllProperties(TypeId type) const;
    // The content property, walking the chain nearest first; nullptr if none.
    const Property *ContentProperty(TypeId type) const;
    // Distance in the chain (0 = same), -1 if not related or not resolved.
    int HierarchyDistance(TypeId derived, TypeId base) const;

private:
    Result CheckName(const std::string &name, bool dotted) const;
    Result AddEnumImpl(const std::string &ownerFullName, const std::string &value, bool numbered, int64_t number);

    Limits limits_;
    bool resolved_ = false;
    std::vector<Problem> warnings_;
    std::vector<Source> sources_;
    std::vector<TypeInfo> types_;                             // index = id - 1
    std::unordered_map<std::string, TypeId> byFullName_;
    std::map<std::string, std::vector<std::string>> xmlMap_;   // uri -> namespaces, in order
};

// ---- plain-text metadata -----------------------------------------------------------------
// Line format (UTF-8, '#' starts a comment, fields separated by spaces or tabs):
//   laml-types 1                                   (required first line)
//   xmlns <uri> <type-namespace>
//   source <id> <location> <revision> <license>    (words without spaces; declare before use)
//   type <FullName> [kind=class|struct|enum|interface|primitive|directive] [abstract] [sealed] [flags]
//        [base=<FullName>] [content=<Property>] [src=<source id>]
//   prop <OwnerFullName>.<Name> <ValueFullName> [attached] [readonly] [dependency] [nullable]
//   enumval <OwnerFullName> <Value>[=<number>]    (decimal, or hex with 0x; negative allowed)
// Declarations only; the caller runs Resolve(). Stops at the first bad line.
Result LoadText(Catalog &catalog, const char *text, size_t size);

}  // namespace laml::types
