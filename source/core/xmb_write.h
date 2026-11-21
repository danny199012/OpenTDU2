#pragma once

// XMB WRITER (M5 last, docs/re/scene_editor.md 8): the write side of
// core/xmb.cpp. The composed exterior bank build (export_bank) authors the
// two XMB files every bank carries - entities.xmb (the model list the spot
// loader walks) and hashcodes.xmb (the rsc -> bank entry index) - and the
// bnk_tool roundtrip proves the layout laws against retail files.
//
// Container law (the reader's mirror, core/xmb.cpp):
//   0x00 char[4] "XMBF", u8 versionMajor (1), u8 versionMinor (1), u16 0
//   0x08 u32 string table offset (0x1c: sorted unique NUL-terminated names,
//        padded with 'A' (0x41) bytes to the type table, which starts at the
//        next 8-byte boundary)
//   0x0c u32 type table offset
//   0x10 u32 data offset
//   0x14 u32 unknown (not read by the port; copied verbatim on a rebuild)
//   0x18 u32 root type reference (record index * 2)
//   Type records (8 bytes each, until an 8-byte "AAAAAAAA" terminator):
//     - primitive:  { u32 TypeWord; u32 NameOffset } with
//       TypeWord = Hi<<16 | Lo, Lo = primitive code (eXmbPrimitive)
//     - composite:  TypeWord = (Size/2)<<16 | (NumFields<<4); Size odd -> Lo
//       bit 0x8000. Followed by one record per field, stored SWAPPED in
//       little endian files ({ u32 NameOffset; u32 TypeWord }), TypeWord =
//       (record index of the field's type) * 2 | 0x80000000 for arrays.
//     Every record (fields included) holds a type-table slot: references are
//     record indices. A composite field may reference a type declared after
//     it (the retail tables do).
//   Data: a packed memory image. Composite fields are laid out in field
//   order (arrays occupy 8 bytes: { u32 count; u32 offset }, strings 4:
//   { u32 offset }); the ARRAY ELEMENTS and STRING BYTES are appended by a
//   FIFO queue after the containing composite finishes - the offsets written
//   into the headers are the queue's pop positions. That law is what makes
//   the retail files' bytes (verified: the element data of a composite array
//   starts right after the whole array of composites, an element's own
//   arrays' data after it, a string right before the array queued behind it).
#include <cstdint>
#include <string>
#include <vector>

class XmbDocument;

// One node of the document tree (a field value or an array element).
struct XmbOutNode {
    enum class Kind {
        Composite, // Fields in declaration order (TypeName names the type)
        Array,     // Elements, all of the element type (TypeName)
        Bool,
        Int8,
        Int16,
        Int32,
        Int64,
        UInt8,
        UInt16,
        UInt32,
        UInt64,
        Float,
        Double,
        String,
        Virtual // u32 (the typeWord/target slot the reader sees)
    };

    Kind kind = Kind::Composite;
    std::string Name;     // field name ("" for array elements and the root)
    std::string TypeName; // composite type name; for arrays the ELEMENT type's
                          // name (a composite type or a primitive name - the
                          // element type must be declared even when empty)

    bool bBool = false;
    int64_t Int = 0;
    uint64_t UInt = 0;
    double Real = 0.0;
    std::string Str;
    std::vector<XmbOutNode> Children; // composite fields (in order) or array elements

    static XmbOutNode MakeComposite( const char *pTypeName ) {
        XmbOutNode node;
        node.kind = Kind::Composite;
        node.TypeName = pTypeName;
        return node;
    }
    static XmbOutNode MakeArray( const char *pElementTypeName ) {
        XmbOutNode node;
        node.kind = Kind::Array;
        node.TypeName = pElementTypeName;
        return node;
    }
    static XmbOutNode MakeString( const char *pValue ) {
        XmbOutNode node;
        node.kind = Kind::String;
        node.Str = pValue;
        return node;
    }
    static XmbOutNode MakeUInt32( uint32_t value ) {
        XmbOutNode node;
        node.kind = Kind::UInt32;
        node.Int = value; // the scalar writers read Int (UInt is the 64-bit lane)
        node.UInt = value;
        return node;
    }
    static XmbOutNode MakeUInt64( uint64_t value ) {
        XmbOutNode node;
        node.kind = Kind::UInt64;
        node.UInt = value;
        return node;
    }
    static XmbOutNode MakeFloat( float value ) {
        XmbOutNode node;
        node.kind = Kind::Float;
        node.Real = value;
        return node;
    }
    static XmbOutNode MakeBool( bool value ) {
        XmbOutNode node;
        node.kind = Kind::Bool;
        node.bBool = value;
        return node;
    }
    // Fields / elements are appended by the caller (Children.push_back).
};

// Serializes the tree (the root node must be a Composite). Returns false
// when a composite type name is reused with different fields.
bool BuildXmb( const XmbOutNode &root, std::vector<uint8_t> &outBytes, std::string &outError );

// Rebuilds a parsed document byte for byte: the type table is replayed in
// the file's own record order (the parse preserves it), the strings re-sorted
// (the retail tables are sorted), the data re-serialized with the law above.
// The unknown header field rides verbatim. `roundtripxmb` in otdu2_bnk_tool
// compares the result against the source file. pRaw is the file the document
// was parsed from (the record replay reads its type-table bytes).
bool RebuildXmb( const XmbDocument &document, const uint8_t *pRaw, uint32_t rawSize,
                 std::vector<uint8_t> &outBytes, std::string &outError );
