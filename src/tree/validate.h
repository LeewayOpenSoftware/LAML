// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 Leeway Open Software
//
// LAML tree validation (M4): checks the property assignments of a logical tree against the type catalog
// and keeps three different things apart, so that none of them can hide another:
//
//   FormatError      the file contradicts the catalog's description: a constant of a kind the property's
//                    value type cannot take, an enum number that is not a member (or an enum of another
//                    type), a number out of the range of its type, an instance property used on an object
//                    that does not derive from the property's declaring type, a read-only property that is
//                    given a constant, an object whose type does not derive from the property's value type.
//   UnknownReference the check cannot be made because the catalog does not describe something it needs
//                    (the property, the object's type, the property's value type). Not an error of the file.
//   NotImplemented   LAML has no check for this form of value (resource references, markup extensions,
//                    type-converted text, struct text, collection element types, ...). Not an error.
//
// Every assignment and every collection item ends in exactly one of: valid, one of these three. "Valid"
// means: a check was made and it passed - never "nothing found". Nothing is instantiated and no value is
// converted; the tree and the catalog are only read.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tree/logical_tree.h"

namespace laml::tree {

enum class FindingKind : uint8_t { FormatError, UnknownReference, NotImplemented };
const char *FindingKindName(FindingKind kind);

enum class FindingCode : uint8_t
{
    // FormatError
    ConstantKindMismatch,   // e.g. a Thickness constant for a Boolean property
    ConstantOutOfRange,     // an integer that does not fit the property's integer type
    NullNotAllowed,         // a null constant for a value type that cannot be null
    EnumTypeMismatch,       // an enum constant of another enum than the property's
    EnumValueNotMember,     // an enum number that is none of the enum's values
    PropertyNotOnType,      // instance property of a type the object does not derive from
    ReadOnlyAssigned,       // a constant given to a read-only property
    ObjectTypeMismatch,     // an object whose type does not derive from the property's value type
    InvalidReference,       // the property reference itself is invalid (also reported by the tree)
    // UnknownReference
    UnknownProperty,
    UnknownObjectType,
    UnknownValueType,
    // NotImplemented
    ValueFormNotChecked,    // resource / template binding / markup extension / deferred / converter text
    EnumNumbersNotDeclared, // the enum exists but the catalog declares no numbers for it
    CollectionItemNotChecked,
    StructTextNotChecked,
};
const char *FindingCodeName(FindingCode code);

struct Finding
{
    FindingKind kind = FindingKind::FormatError;
    FindingCode code = FindingCode::ConstantKindMismatch;
    uint32_t node = kNoNode;       // the node the assignment or item belongs to
    bool item = false;             // a collection item rather than a property assignment
    uint32_t index = 0;            // index in node.properties or node.items
    std::string message;
    SourcePos pos;
};

struct ValidationCounts
{
    // one outcome per assignment / item
    size_t assignments = 0, assignmentsValid = 0, assignmentsFormatError = 0, assignmentsUnknown = 0, assignmentsNotImplemented = 0;
    size_t items = 0, itemsValid = 0, itemsFormatError = 0, itemsUnknown = 0, itemsNotImplemented = 0;
    // findings by code (a single assignment may fail more than one check)
    size_t byCode[24] = {};
};

struct ValidationReport
{
    std::vector<Finding> findings;     // capped by ValidateLimits::maxFindings; the counts stay exact
    ValidationCounts counts;
    size_t findingsDropped = 0;
    bool ok() const { return counts.assignmentsFormatError == 0 && counts.itemsFormatError == 0; }
};

struct ValidateLimits
{
    uint32_t maxFindings = 10000;
};

// `catalog` must be resolved. `resolver` is used to resolve the trusted type reference of an enum constant.
void Validate(const LogicalTree &tree, const types::Catalog &catalog, const RefResolver &resolver, const ValidateLimits &limits,
              ValidationReport &out);
inline void Validate(const LogicalTree &tree, const types::Catalog &catalog, const RefResolver &resolver, ValidationReport &out)
{
    Validate(tree, catalog, resolver, ValidateLimits(), out);
}

std::string DumpValidation(const ValidationReport &report, size_t maxFindings);

}  // namespace laml::tree
