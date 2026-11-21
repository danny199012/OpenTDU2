#include "shared.h"
#include "xmb_write.h"

#include "core/xmb.h"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr uint32_t kXmbMagic = 0x46424d58; // XMBF
constexpr uint32_t kPaddingRecord = 0x41414141;

// The 14 standard primitives in the order the retail tables declare them
// (hashcodes.xmb's block: the eXmbPrimitive order). A fresh document declares
// all 14 up front; a rebuild replays the file's own record order instead.
struct PrimitiveDecl {
    eXmbPrimitive Code;
    const char *pName;
    uint32_t Size;
};
constexpr PrimitiveDecl kPrimitives[] = {
    { eXmbPrimitive::XP_Object, "OBJECT", 4 },   { eXmbPrimitive::XP_Bool, "BOOL", 1 },
    { eXmbPrimitive::XP_Int8, "SINT8", 1 },      { eXmbPrimitive::XP_Int16, "SINT16", 2 },
    { eXmbPrimitive::XP_Int32, "SINT32", 4 },    { eXmbPrimitive::XP_Int64, "SINT64", 8 },
    { eXmbPrimitive::XP_UInt8, "UINT8", 1 },     { eXmbPrimitive::XP_UInt16, "UINT16", 2 },
    { eXmbPrimitive::XP_UInt32, "UINT32", 4 },   { eXmbPrimitive::XP_UInt64, "UINT64", 8 },
    { eXmbPrimitive::XP_Float, "FLOAT", 4 },     { eXmbPrimitive::XP_Double, "DOUBLE", 8 },
    { eXmbPrimitive::XP_String, "STRING", 4 },   { eXmbPrimitive::XP_Virtual, "VIRTUAL", 8 },
};
// NOTE: VIRTUAL is 8 bytes in the retail type tables ({ u32 typeWord;
// u32 target } - the __StructScDataCondition { if; then } composite declares
// 16 = 2 x 8). The port's reader (core/xmb.cpp) sizes it 4, which misaligns
// composites holding them; the writer uses the retail law.

XmbOutNode::Kind KindOfPrimitive( eXmbPrimitive code ) {
    switch ( code ) {
    case eXmbPrimitive::XP_Bool: return XmbOutNode::Kind::Bool;
    case eXmbPrimitive::XP_Int8: return XmbOutNode::Kind::Int8;
    case eXmbPrimitive::XP_Int16: return XmbOutNode::Kind::Int16;
    case eXmbPrimitive::XP_Int32: return XmbOutNode::Kind::Int32;
    case eXmbPrimitive::XP_Int64: return XmbOutNode::Kind::Int64;
    case eXmbPrimitive::XP_UInt8: return XmbOutNode::Kind::UInt8;
    case eXmbPrimitive::XP_UInt16: return XmbOutNode::Kind::UInt16;
    case eXmbPrimitive::XP_UInt32: return XmbOutNode::Kind::UInt32;
    case eXmbPrimitive::XP_UInt64: return XmbOutNode::Kind::UInt64;
    case eXmbPrimitive::XP_Float: return XmbOutNode::Kind::Float;
    case eXmbPrimitive::XP_Double: return XmbOutNode::Kind::Double;
    case eXmbPrimitive::XP_String: return XmbOutNode::Kind::String;
    case eXmbPrimitive::XP_Virtual: return XmbOutNode::Kind::Virtual;
    default: return XmbOutNode::Kind::Composite;
    }
}

eXmbPrimitive PrimitiveOfKind( XmbOutNode::Kind kind ) {
    switch ( kind ) {
    case XmbOutNode::Kind::Bool: return eXmbPrimitive::XP_Bool;
    case XmbOutNode::Kind::Int8: return eXmbPrimitive::XP_Int8;
    case XmbOutNode::Kind::Int16: return eXmbPrimitive::XP_Int16;
    case XmbOutNode::Kind::Int32: return eXmbPrimitive::XP_Int32;
    case XmbOutNode::Kind::Int64: return eXmbPrimitive::XP_Int64;
    case XmbOutNode::Kind::UInt8: return eXmbPrimitive::XP_UInt8;
    case XmbOutNode::Kind::UInt16: return eXmbPrimitive::XP_UInt16;
    case XmbOutNode::Kind::UInt32: return eXmbPrimitive::XP_UInt32;
    case XmbOutNode::Kind::UInt64: return eXmbPrimitive::XP_UInt64;
    case XmbOutNode::Kind::Float: return eXmbPrimitive::XP_Float;
    case XmbOutNode::Kind::Double: return eXmbPrimitive::XP_Double;
    case XmbOutNode::Kind::String: return eXmbPrimitive::XP_String;
    case XmbOutNode::Kind::Virtual: return eXmbPrimitive::XP_Virtual;
    default: return eXmbPrimitive::XP_Object;
    }
}

// --- the type table ---------------------------------------------------------

// One type record. The record kinds follow the reader's state machine: a
// composite record is followed by NumFields field records, and every record
// (fields included) holds a type-table slot referenced by index.
struct TypeRecord {
    enum class Kind { Primitive, Composite, Field };
    Kind kind = Kind::Primitive;
    std::string Name;
    // Primitive
    eXmbPrimitive Code = eXmbPrimitive::XP_Object;
    uint32_t Size = 0;
    // Composite
    uint32_t NumFields = 0;
    // Field
    uint32_t RefIndex = 0;
    bool bArray = false;
    // Rebuild mode: the record's two u32s verbatim (the retail primitive
    // words carry the size in the high half - OBJECT is plain 0 - so a
    // byte-exact rebuild replays them and patches only the name offset).
    bool bVerbatim = false;
    uint32_t WordA = 0;
    uint32_t WordB = 0;
};

// A composite type's field metadata (fresh mode): enough to emit the field
// records once every composite is registered (field type references may be
// forward - they patch by name).
struct FieldMeta {
    std::string Name;
    bool bArray = false;
    bool bCompositeElement = false;
    std::string TypeName; // composite name, or the primitive's name for scalars
    XmbOutNode::Kind scalarKind = XmbOutNode::Kind::UInt32;
};

struct TypeTable {
    std::vector<TypeRecord> records;
    bool bHasTerminator = true; // the "AAAAAAAA" record (absent in some retail tables)
    std::unordered_map<std::string, uint32_t> compositeIndex; // composite name -> record index
    std::unordered_map<std::string, uint32_t> primitiveIndex; // primitive name -> record index
    std::unordered_map<std::string, std::vector<FieldMeta>> compositeFields;
    mutable std::unordered_map<uint32_t, int32_t> sizeOf; // record index -> byte size

    int32_t getSize( uint32_t recordIndex ) const {
        auto it = sizeOf.find( recordIndex );
        if ( it != sizeOf.end() ) {
            return it->second;
        }
        const TypeRecord &record = records[ recordIndex ];
        int32_t size = 0;
        if ( record.kind == TypeRecord::Kind::Primitive ) {
            size = static_cast<int32_t>( record.Size );
        } else if ( record.kind == TypeRecord::Kind::Field ) {
            size = 4; // a field record's own slot is never sized
        } else {
            // Composite: the sum of the field sizes (arrays occupy 8).
            int64_t total = 0;
            for ( uint32_t i = 0; i < record.NumFields; i++ ) {
                const TypeRecord &field = records[ recordIndex + 1 + i ];
                total += field.bArray ? 8 : getSize( field.RefIndex );
            }
            size = static_cast<int32_t>( total );
        }
        sizeOf[ recordIndex ] = size;
        return size;
    }
};

const PrimitiveDecl *FindPrimitiveByKind( XmbOutNode::Kind kind ) {
    const eXmbPrimitive code = PrimitiveOfKind( kind );
    for ( const PrimitiveDecl &decl : kPrimitives ) {
        if ( decl.Code == code ) {
            return &decl;
        }
    }
    return nullptr;
}
const PrimitiveDecl *FindPrimitiveByName( const std::string &name ) {
    for ( const PrimitiveDecl &decl : kPrimitives ) {
        if ( name == decl.pName ) {
            return &decl;
        }
    }
    return nullptr;
}

// Registers the tree's composite types (fresh mode), first encounter order.
bool CollectTypes( const XmbOutNode &node, TypeTable &table, std::string &outError ) {
    auto collectArrayElement = [&]( const XmbOutNode &arrayNode ) -> bool {
        // The element type: declared through the elements when composite; a
        // primitive by name (legal even when the array is empty).
        if ( FindPrimitiveByName( arrayNode.TypeName ) == nullptr &&
             table.compositeFields.find( arrayNode.TypeName ) == table.compositeFields.end() ) {
            for ( const XmbOutNode &element : arrayNode.Children ) {
                if ( element.kind != XmbOutNode::Kind::Composite ) {
                    continue;
                }
                if ( !CollectTypes( element, table, outError ) ) {
                    return false;
                }
            }
            if ( table.compositeFields.find( arrayNode.TypeName ) == table.compositeFields.end() ) {
                if ( !arrayNode.Children.empty() ) {
                    outError = "array element type '" + arrayNode.TypeName + "' is not a composite or a primitive";
                    return false;
                }
                // An empty array of an undeclared composite: the type must be
                // declared elsewhere in the tree; surfaced at the field record.
            }
        }
        return true;
    };

    switch ( node.kind ) {
    case XmbOutNode::Kind::Composite: {
        auto fieldsIt = table.compositeFields.find( node.TypeName );
        if ( fieldsIt != table.compositeFields.end() ) {
            // Re-encounter: the field list must match (types are global).
            if ( fieldsIt->second.size() != node.Children.size() ) {
                outError = "composite type '" + node.TypeName + "' re-declared with different fields";
                return false;
            }
            for ( size_t i = 0; i < node.Children.size(); i++ ) {
                if ( fieldsIt->second[ i ].Name != node.Children[ i ].Name ) {
                    outError = "composite type '" + node.TypeName + "' field " + std::to_string( i ) +
                               " name mismatch ('" + node.Children[ i ].Name + "' vs '" +
                               fieldsIt->second[ i ].Name + "')";
                    return false;
                }
            }
            return true;
        }
        if ( FindPrimitiveByName( node.TypeName ) != nullptr ) {
            outError = "composite type '" + node.TypeName + "' collides with a primitive name";
            return false;
        }
        std::vector<FieldMeta> fields;
        for ( const XmbOutNode &field : node.Children ) {
            FieldMeta meta;
            meta.Name = field.Name;
            if ( field.kind == XmbOutNode::Kind::Composite ) {
                meta.bCompositeElement = true;
                meta.TypeName = field.TypeName;
            } else if ( field.kind == XmbOutNode::Kind::Array ) {
                meta.bArray = true;
                meta.bCompositeElement = FindPrimitiveByName( field.TypeName ) == nullptr;
                meta.TypeName = field.TypeName;
            } else {
                const PrimitiveDecl *pDecl = FindPrimitiveByKind( field.kind );
                if ( pDecl == nullptr ) {
                    outError = "field '" + field.Name + "' has no primitive kind";
                    return false;
                }
                meta.TypeName = pDecl->pName;
            }
            fields.push_back( std::move( meta ) );
        }
        table.compositeFields[ node.TypeName ] = std::move( fields );
        for ( const XmbOutNode &field : node.Children ) {
            if ( field.kind == XmbOutNode::Kind::Composite ) {
                if ( !CollectTypes( field, table, outError ) ) {
                    return false;
                }
            } else if ( field.kind == XmbOutNode::Kind::Array ) {
                if ( !collectArrayElement( field ) ) {
                    return false;
                }
            }
        }
        return true;
    }
    case XmbOutNode::Kind::Array:
        return collectArrayElement( node );
    default:
        return true;
    }
}

// Emits the records: the 14 primitives, then the composites in first-
// encounter order (the declaration walk's insertion order of the map is not
// stable, so the walk collects an explicit order list alongside).
void CollectCompositeOrder( const XmbOutNode &node, std::vector<std::string> &outOrder,
                            std::unordered_set<std::string> &seen ) {
    if ( node.kind == XmbOutNode::Kind::Composite ) {
        if ( seen.insert( node.TypeName ).second ) {
            outOrder.push_back( node.TypeName );
        }
    }
    for ( const XmbOutNode &child : node.Children ) {
        CollectCompositeOrder( child, outOrder, seen );
    }
}

bool FinalizeFreshTypes( const XmbOutNode &root, TypeTable &table, std::string &outError ) {
    for ( const PrimitiveDecl &decl : kPrimitives ) {
        TypeRecord record;
        record.kind = TypeRecord::Kind::Primitive;
        record.Name = decl.pName;
        record.Code = decl.Code;
        record.Size = decl.Size;
        table.primitiveIndex[ decl.pName ] = static_cast<uint32_t>( table.records.size() );
        table.records.push_back( record );
    }

    std::vector<std::string> order;
    std::unordered_set<std::string> seen;
    CollectCompositeOrder( root, order, seen );

    struct RefPatch {
        uint32_t recordIndex;
        std::string TypeName;
    };
    std::vector<RefPatch> patches;
    for ( const std::string &typeName : order ) {
        auto fieldsIt = table.compositeFields.find( typeName );
        if ( fieldsIt == table.compositeFields.end() ) {
            continue;
        }
        if ( table.compositeIndex.find( typeName ) != table.compositeIndex.end() ) {
            continue; // already emitted as another field's element type
        }
        TypeRecord record;
        record.kind = TypeRecord::Kind::Composite;
        record.Name = typeName;
        record.NumFields = static_cast<uint32_t>( fieldsIt->second.size() );
        const uint32_t index = static_cast<uint32_t>( table.records.size() );
        table.records.push_back( record );
        table.compositeIndex[ typeName ] = index;
        for ( const FieldMeta &meta : fieldsIt->second ) {
            TypeRecord fieldRecord;
            fieldRecord.kind = TypeRecord::Kind::Field;
            fieldRecord.Name = meta.Name;
            fieldRecord.bArray = meta.bArray;
            if ( meta.bCompositeElement ) {
                auto compIt = table.compositeIndex.find( meta.TypeName );
                if ( compIt != table.compositeIndex.end() ) {
                    fieldRecord.RefIndex = compIt->second;
                } else {
                    patches.push_back( { static_cast<uint32_t>( table.records.size() ), meta.TypeName } );
                }
            } else {
                auto primIt = table.primitiveIndex.find( meta.TypeName );
                if ( primIt == table.primitiveIndex.end() ) {
                    outError = "field '" + meta.Name + "' has unknown type '" + meta.TypeName + "'";
                    return false;
                }
                fieldRecord.RefIndex = primIt->second;
            }
            table.records.push_back( fieldRecord );
        }
    }
    // The forward references (an array's composite element type declared by a
    // later field): patch by name now that every composite is emitted.
    for ( const RefPatch &patch : patches ) {
        auto compIt = table.compositeIndex.find( patch.TypeName );
        if ( compIt == table.compositeIndex.end() ) {
            outError = "undeclared composite type '" + patch.TypeName + "'";
            return false;
        }
        table.records[ patch.recordIndex ].RefIndex = compIt->second;
    }
    return true;
}

// --- the data section -------------------------------------------------------

void AppendU32( std::vector<uint8_t> &out, uint32_t value ) {
    const size_t at = out.size();
    out.resize( at + 4 );
    memcpy( out.data() + at, &value, 4 );
}
void AppendU64( std::vector<uint8_t> &out, uint64_t value ) {
    const size_t at = out.size();
    out.resize( at + 8 );
    memcpy( out.data() + at, &value, 8 );
}

struct DataWriter {
    std::vector<uint8_t> out;
    struct Pending {
        enum class Kind { Elements, String };
        Kind kind = Kind::Elements;
        uint32_t patchAt = 0; // the u32 offset field filled with the pop cursor
        const XmbOutNode *pArray = nullptr;
        const std::string *pStr = nullptr;
    };
    std::deque<Pending> queue;
    const TypeTable *pTable = nullptr; // composite element sizes (the 8-byte pad law)
    std::string error;

    static uint32_t ScalarSize( XmbOutNode::Kind kind ) {
        switch ( kind ) {
        case XmbOutNode::Kind::Bool:
        case XmbOutNode::Kind::Int8:
        case XmbOutNode::Kind::UInt8: return 1;
        case XmbOutNode::Kind::Int16:
        case XmbOutNode::Kind::UInt16: return 2;
        case XmbOutNode::Kind::Int64:
        case XmbOutNode::Kind::UInt64:
        case XmbOutNode::Kind::Double: return 8;
        case XmbOutNode::Kind::Virtual: return 8;
        default: return 4; // Int32 / UInt32 / Float / String slot
        }
    }

    void writeScalar( const XmbOutNode &node ) {
        switch ( node.kind ) {
        case XmbOutNode::Kind::Bool:
            out.push_back( node.bBool ? 1 : 0 );
            break;
        case XmbOutNode::Kind::Int8:
        case XmbOutNode::Kind::UInt8:
            out.push_back( static_cast<uint8_t>( node.Int ) );
            break;
        case XmbOutNode::Kind::Int16:
        case XmbOutNode::Kind::UInt16: {
            const size_t at = out.size();
            out.resize( at + 2 );
            const uint16_t v = static_cast<uint16_t>( node.Int );
            memcpy( out.data() + at, &v, 2 );
            break;
        }
        case XmbOutNode::Kind::Int32:
        case XmbOutNode::Kind::UInt32:
            AppendU32( out, static_cast<uint32_t>( node.Int ) );
            break;
        case XmbOutNode::Kind::Int64:
            AppendU64( out, static_cast<uint64_t>( node.Int ) );
            break;
        case XmbOutNode::Kind::UInt64:
            AppendU64( out, node.UInt );
            break;
        case XmbOutNode::Kind::Float: {
            const float v = static_cast<float>( node.Real );
            AppendU32( out, *reinterpret_cast<const uint32_t *>( &v ) );
            break;
        }
        case XmbOutNode::Kind::Double: {
            const double v = node.Real;
            AppendU64( out, *reinterpret_cast<const uint64_t *>( &v ) );
            break;
        }
        case XmbOutNode::Kind::Virtual:
            AppendU64( out, node.UInt ); // { u32 typeWord; u32 target }, the retail 8-byte slot
            break;
        default:
            break;
        }
    }

    void writeComposite( const XmbOutNode &node ) {
        for ( const XmbOutNode &field : node.Children ) {
            if ( field.kind == XmbOutNode::Kind::Composite ) {
                writeComposite( field );
            } else if ( field.kind == XmbOutNode::Kind::Array ) {
                Pending pending;
                pending.kind = Pending::Kind::Elements;
                pending.patchAt = static_cast<uint32_t>( out.size() ) + 4;
                pending.pArray = &field;
                AppendU32( out, static_cast<uint32_t>( field.Children.size() ) );
                AppendU32( out, 0 ); // patched when the queue pops
                queue.push_back( pending );
            } else if ( field.kind == XmbOutNode::Kind::String ) {
                Pending pending;
                pending.kind = Pending::Kind::String;
                pending.patchAt = static_cast<uint32_t>( out.size() );
                pending.pStr = &field.Str;
                AppendU32( out, 0 );
                queue.push_back( pending );
            } else {
                writeScalar( field );
            }
        }
    }

    bool drain() {
        while ( !queue.empty() ) {
            const Pending pending = queue.front();
            queue.pop_front();
            if ( pending.kind == Pending::Kind::String ) {
                const uint32_t cursor = static_cast<uint32_t>( out.size() );
                memcpy( out.data() + pending.patchAt, &cursor, 4 );
                out.insert( out.end(), pending.pStr->begin(), pending.pStr->end() );
                out.push_back( 0 );
            } else {
                // The 8-byte pad law (the retail files): the element data of
                // an array of 8-byte elements starts 8-aligned, the gap
                // filled with 'A' bytes (hashcodes.xmb's empty dangling
                // offsets and u64 arrays prove it; 1/2/4-byte and composite
                // element arrays stay packed).
                // The element type: the array node's TypeName (a primitive
                // name or a composite type - known even when the array is
                // empty, which is exactly where the retail files prove the
                // pad: hashcodes.xmb's empty u64 array still shifted to the
                // 8-boundary).
                uint32_t elementSize = 0;
                const std::string &elementTypeName = pending.pArray->TypeName;
                if ( !elementTypeName.empty() && elementTypeName != "OBJECT" ) {
                    static const char *const k8[] = { "UINT64", "SINT64", "DOUBLE" };
                    for ( const char *pName : k8 ) {
                        if ( elementTypeName == pName ) {
                            elementSize = 8;
                            break;
                        }
                    }
                    if ( elementSize == 0 && pTable != nullptr ) {
                        auto it = pTable->compositeIndex.find( elementTypeName );
                        if ( it != pTable->compositeIndex.end() ) {
                            elementSize = static_cast<uint32_t>( pTable->getSize( it->second ) );
                        }
                    }
                }
                if ( elementSize == 8 && ( out.size() & 7 ) != 0 ) {
                    out.resize( out.size() + ( 8u - ( out.size() & 7u ) ), 0x41 );
                }
                const uint32_t cursor = static_cast<uint32_t>( out.size() );
                memcpy( out.data() + pending.patchAt, &cursor, 4 );
                for ( const XmbOutNode &element : pending.pArray->Children ) {
                    if ( element.kind == XmbOutNode::Kind::Composite ) {
                        writeComposite( element );
                    } else if ( element.kind == XmbOutNode::Kind::Array ) {
                        error = "arrays of arrays are not part of the format";
                        return false;
                    } else {
                        writeScalar( element );
                    }
                }
            }
        }
        return true;
    }
};

// The sorted unique string table ('A'-padded to the type table's 8-byte
// boundary). A rebuild passes the file's own raw strings: the retail tables
// hold ORPHAN strings no record references (the exporters' leftovers), so
// the union of raw + record names is what byte-matches.
// The string table blob AND the per-name offsets (one list for both: the
// retail tables hold ORPHAN strings no record references, so a rebuild takes
// the file's own strings; a fresh document's records carry every name).
std::vector<uint8_t> BuildStringTable( const std::vector<TypeRecord> &records, uint32_t &outTypeTableOffset,
                                       std::unordered_map<std::string, uint32_t> &outOffsets,
                                       const std::vector<std::string> *pRawStrings = nullptr ) {
    std::vector<std::string> names;
    auto add = [&]( const std::string &name ) {
        if ( !name.empty() && std::find( names.begin(), names.end(), name ) == names.end() ) {
            names.push_back( name );
        }
    };
    if ( pRawStrings != nullptr ) {
        for ( const std::string &name : *pRawStrings ) {
            add( name );
        }
    }
    for ( const TypeRecord &record : records ) {
        add( record.Name );
    }
    std::sort( names.begin(), names.end() );
    std::vector<uint8_t> out;
    uint32_t offset = 0x1c;
    for ( const std::string &name : names ) {
        outOffsets[ name ] = offset;
        out.insert( out.end(), name.begin(), name.end() );
        out.push_back( 0 );
        offset += static_cast<uint32_t>( name.size() ) + 1;
    }
    const uint32_t end = 0x1c + static_cast<uint32_t>( out.size() );
    outTypeTableOffset = ( end + 7 ) & ~7u;
    out.resize( outTypeTableOffset - 0x1c, 0x41 );
    return out;
}

// The file's own strings (the padding after the last NUL has none itself, so
// the NUL walk stops there).
std::vector<std::string> ReadRawStrings( const uint8_t *pRaw, uint32_t stringTableOffset,
                                         uint32_t typeTableOffset ) {
    std::vector<std::string> out;
    uint32_t pos = stringTableOffset;
    while ( pos < typeTableOffset ) {
        const uint32_t end = static_cast<uint32_t>(
            std::find( pRaw + pos, pRaw + typeTableOffset, 0 ) - pRaw );
        if ( end == pos || end >= typeTableOffset ) {
            break;
        }
        out.emplace_back( reinterpret_cast<const char *>( pRaw + pos ), end - pos );
        pos = end + 1;
    }
    return out;
}

std::vector<uint8_t> BuildTypeTable( const TypeTable &table ) {
    std::vector<uint8_t> out;
    for ( size_t i = 0; i < table.records.size(); i++ ) {
        const TypeRecord &record = table.records[ i ];
        if ( record.bVerbatim ) {
            // Rebuild mode: the file's own two u32s (the name offset patched
            // below; fields keep the file's { name; type } LE order).
            AppendU32( out, record.WordA );
            AppendU32( out, record.WordB );
            continue;
        }
        if ( record.kind == TypeRecord::Kind::Field ) {
            // Fresh field record: { NameOffset; TypeWord } (the LE law).
            const uint32_t typeWord = ( record.RefIndex * 2 ) | ( record.bArray ? 0x80000000u : 0u );
            AppendU32( out, 0 ); // patched below
            AppendU32( out, typeWord );
        } else if ( record.kind == TypeRecord::Kind::Composite ) {
            const int32_t size = table.getSize( static_cast<uint32_t>( i ) );
            const uint32_t typeWord = ( static_cast<uint32_t>( size ) / 2 ) << 16 |
                                      ( record.NumFields << 4 ) |
                                      ( ( size & 1 ) ? 0x8000u : 0u );
            AppendU32( out, typeWord );
            AppendU32( out, 0 );
        } else {
            // Fresh primitive: the retail shape (the parser reads only the
            // low 15 bits): OBJECT is plain 0, the others carry size/2 in
            // the high half and the odd-size flag in bit 15.
            uint32_t typeWord = static_cast<uint32_t>( record.Code );
            if ( record.Code != eXmbPrimitive::XP_Object ) {
                typeWord = ( static_cast<uint32_t>( record.Size ) / 2 ) << 16 |
                           ( ( record.Size & 1 ) ? 0x8000u : 0u ) |
                           static_cast<uint32_t>( record.Code );
            }
            AppendU32( out, typeWord );
            AppendU32( out, 0 );
        }
    }
    // The "AAAAAAAA" terminator record (the retail tables that carry one).
    if ( table.bHasTerminator ) {
        AppendU32( out, kPaddingRecord );
        AppendU32( out, kPaddingRecord );
    }
    return out;
}

void PatchNameOffsets( std::vector<uint8_t> &typeTable, const TypeTable &table,
                       const std::unordered_map<std::string, uint32_t> &stringOffsets ) {
    size_t at = 0;
    for ( const TypeRecord &record : table.records ) {
        // The file stores the offset relative to the string table (the
        // reader adds stringTableOffset). Field records keep the name in the
        // FIRST u32 (the LE law); every other record in the second.
        const uint32_t nameOffset = stringOffsets.at( record.Name ) - 0x1c;
        if ( record.kind == TypeRecord::Kind::Field ) {
            memcpy( typeTable.data() + at, &nameOffset, 4 );
        } else {
            memcpy( typeTable.data() + at + 4, &nameOffset, 4 );
        }
        at += 8;
    }
}

// Replays the file's own type records (the parse's state machine over the raw
// bytes: the parse preserves the record order, the rebuild emits it back).
bool ReplayTypeRecords( const uint8_t *pRaw, uint32_t typeTableOffset, uint32_t dataOffset, TypeTable &table ) {
    table.bHasTerminator = false; // set when the walk meets the AAAA record
    uint32_t remainingFields = 0;
    for ( uint32_t offset = typeTableOffset; offset + 8 <= dataOffset; offset += 8 ) {
        uint32_t a, b;
        memcpy( &a, pRaw + offset, 4 );
        memcpy( &b, pRaw + offset + 4, 4 );
        uint32_t typeWord = a, nameOffset = b;
        if ( remainingFields > 0 ) {
            std::swap( typeWord, nameOffset ); // field records are swapped in LE files
        }
        if ( typeWord == kPaddingRecord ) {
            table.bHasTerminator = true; // the "AAAAAAAA" terminator
            break;
        }
        if ( 0x1c + nameOffset >= typeTableOffset ) {
            return false; // a name offset outside the string table
        }

        TypeRecord record;
        record.Name.assign( reinterpret_cast<const char *>( pRaw + 0x1c + nameOffset ),
                            strnlen( reinterpret_cast<const char *>( pRaw + 0x1c + nameOffset ),
                                     typeTableOffset - 0x1c - nameOffset ) );
        record.WordA = a; // the file's own two u32s, verbatim (the emission
        record.WordB = b; // replay patches only the name offset)
        if ( remainingFields > 0 ) {
            record.kind = TypeRecord::Kind::Field;
            record.RefIndex = ( typeWord & 0x7fffffff ) / 2;
            record.bArray = ( typeWord & 0x80000000 ) != 0;
            remainingFields--;
        } else {
            const uint16_t lo = static_cast<uint16_t>( typeWord & 0xffff );
            const uint32_t numFields = ( lo >> 4 ) & 0x7ff;
            if ( ( lo & 0xf ) == 0 && numFields > 0 ) {
                record.kind = TypeRecord::Kind::Composite;
                record.NumFields = numFields;
                remainingFields = numFields;
            } else {
                record.kind = TypeRecord::Kind::Primitive;
                record.Code = static_cast<eXmbPrimitive>( lo & 0x7fff );
                for ( const PrimitiveDecl &decl : kPrimitives ) {
                    if ( decl.Code == record.Code ) {
                        record.Size = decl.Size;
                        break;
                    }
                }
            }
        }
        record.bVerbatim = true;
        table.records.push_back( std::move( record ) );
        if ( table.records.size() > 0x10000 ) {
            return false; // runaway table
        }
    }
    return true;
}

// The field slot size in a composite: 8 for arrays, the record's size with
// the retail VIRTUAL law (8). Composite sizes use their declared Size.
uint32_t RecordSize( const XmbDocument &doc, uint32_t typeIndex ) {
    if ( typeIndex >= doc.getTypes().size() ) {
        return 4;
    }
    const XmbType &type = doc.getType( typeIndex );
    if ( type.bComposite ) {
        return type.Size;
    }
    return type.Primitive == eXmbPrimitive::XP_Virtual ? 8u : static_cast<uint32_t>( type.Size );
}
uint32_t SlotSize( const XmbDocument &doc, uint32_t typeIndex, bool bArray ) {
    return bArray ? 8u : RecordSize( doc, typeIndex );
}

// Reads a parsed value back into the tree (the reader's own traversal law).
bool ReadValueTree( const XmbDocument &doc, uint32_t typeIndex, uint32_t offset, const std::string &fieldName,
                    XmbOutNode &outNode, std::string &outError ) {
    if ( typeIndex >= doc.getTypes().size() ) {
        outError = "type index out of range";
        return false;
    }
    const XmbType &type = doc.getType( typeIndex );
    outNode.Name = fieldName;
    if ( type.bComposite ) {
        outNode.kind = XmbOutNode::Kind::Composite;
        outNode.TypeName = type.Name;
        // Field offsets recomputed with the retail size law (VIRTUAL is 8,
        // not the port parser's 4): the parser's own field.Offset values
        // misalign composites that hold virtual fields.
        uint32_t fieldOffset = 0;
        for ( const XmbType::Field &field : type.Fields ) {
            const uint32_t slot = SlotSize( doc, field.TypeIndex, field.bArray );
            XmbOutNode child;
            if ( field.bArray ) {
                const XmbType &elementType = field.TypeIndex < doc.getTypes().size()
                                                 ? doc.getType( field.TypeIndex )
                                                 : XmbType();
                child.kind = XmbOutNode::Kind::Array;
                child.TypeName = elementType.Name;
                const uint32_t count = doc.readU32( offset + fieldOffset );
                const uint32_t arrayOffset = doc.readU32( offset + fieldOffset + 4 );
                for ( uint32_t i = 0; i < count; i++ ) {
                    XmbOutNode element;
                    if ( !ReadValueTree( doc, field.TypeIndex,
                                         arrayOffset + i * RecordSize( doc, field.TypeIndex ), "",
                                         element, outError ) ) {
                        return false;
                    }
                    child.Children.push_back( std::move( element ) );
                }
            } else if ( !ReadValueTree( doc, field.TypeIndex, offset + fieldOffset, field.Name, child,
                                        outError ) ) {
                return false;
            }
            outNode.Children.push_back( std::move( child ) );
            fieldOffset += slot;
        }
        return true;
    }

    outNode.kind = KindOfPrimitive( type.Primitive );
    switch ( type.Primitive ) {
    case eXmbPrimitive::XP_Bool:
    case eXmbPrimitive::XP_UInt8:
        outNode.Int = doc.readU8( offset );
        break;
    case eXmbPrimitive::XP_Int8:
        outNode.Int = static_cast<int8_t>( doc.readU8( offset ) );
        break;
    case eXmbPrimitive::XP_Int16:
        outNode.Int = static_cast<int16_t>( doc.readU16( offset ) );
        break;
    case eXmbPrimitive::XP_UInt16:
        outNode.Int = doc.readU16( offset );
        break;
    case eXmbPrimitive::XP_Int32:
        outNode.Int = static_cast<int32_t>( doc.readU32( offset ) );
        break;
    case eXmbPrimitive::XP_UInt32:
        outNode.Int = doc.readU32( offset );
        break;
    case eXmbPrimitive::XP_Int64:
        outNode.Int = static_cast<int64_t>( doc.readU64( offset ) );
        break;
    case eXmbPrimitive::XP_UInt64:
        outNode.UInt = doc.readU64( offset );
        break;
    case eXmbPrimitive::XP_Float: {
        const uint32_t bits = doc.readU32( offset );
        float value;
        memcpy( &value, &bits, 4 );
        outNode.Real = value;
        break;
    }
    case eXmbPrimitive::XP_Double: {
        const uint64_t bits = doc.readU64( offset );
        double value;
        memcpy( &value, &bits, 8 );
        outNode.Real = value;
        break;
    }
    case eXmbPrimitive::XP_String:
        outNode.Str = doc.readString( offset );
        break;
    case eXmbPrimitive::XP_Virtual:
        outNode.UInt = doc.readU64( offset ); // { u32 typeWord; u32 target }
        break;
    default:
        outError = "unsupported primitive";
        return false;
    }
    return true;
}
} // namespace

bool BuildXmb( const XmbOutNode &root, std::vector<uint8_t> &outBytes, std::string &outError ) {
    outBytes.clear();
    if ( root.kind != XmbOutNode::Kind::Composite ) {
        outError = "the XMB root must be a composite";
        return false;
    }

    // 1. The types: the 14 standard primitives, then the composites in
    // first-encounter order (field references may be forward).
    TypeTable table;
    if ( !CollectTypes( root, table, outError ) || !FinalizeFreshTypes( root, table, outError ) ) {
        return false;
    }

    // 2. The string table + the data section (the FIFO law).
    uint32_t typeTableOffset = 0;
    std::unordered_map<std::string, uint32_t> stringOffsets;
    std::vector<uint8_t> stringTable = BuildStringTable( table.records, typeTableOffset, stringOffsets );
    DataWriter writer;
    writer.pTable = &table;
    writer.writeComposite( root );
    if ( !writer.drain() ) {
        outError = writer.error;
        return false;
    }

    // 3. The type table bytes + the header.
    std::vector<uint8_t> typeTable = BuildTypeTable( table );
    PatchNameOffsets( typeTable, table, stringOffsets );
    auto rootIt = table.compositeIndex.find( root.TypeName );
    if ( rootIt == table.compositeIndex.end() ) {
        outError = "the root type did not declare";
        return false;
    }
    const uint32_t dataOffset = typeTableOffset + static_cast<uint32_t>( typeTable.size() );

    outBytes.resize( 0x08 ); // magic + version bytes, then the five header u32s
    memcpy( outBytes.data(), &kXmbMagic, 4 );
    outBytes[ 4 ] = 1;
    outBytes[ 5 ] = 1;
    outBytes[ 6 ] = 0;
    outBytes[ 7 ] = 0;
    AppendU32( outBytes, 0x1c );
    AppendU32( outBytes, typeTableOffset );
    AppendU32( outBytes, dataOffset );
    AppendU32( outBytes, 0 ); // unknown (not read by the port)
    AppendU32( outBytes, rootIt->second * 2 );
    outBytes.insert( outBytes.end(), stringTable.begin(), stringTable.end() );
    outBytes.insert( outBytes.end(), typeTable.begin(), typeTable.end() );
    outBytes.insert( outBytes.end(), writer.out.begin(), writer.out.end() );
    return true;
}

bool RebuildXmb( const XmbDocument &document, const uint8_t *pRaw, uint32_t rawSize, std::vector<uint8_t> &outBytes,
                 std::string &outError ) {
    outBytes.clear();
    if ( document.isBigEndian() ) {
        outError = "big endian XMB files are not rebuilt";
        return false;
    }
    if ( pRaw == nullptr || rawSize < 0x1c ) {
        outError = "truncated file";
        return false;
    }
    auto u32At = [ & ]( uint32_t at ) {
        uint32_t v;
        memcpy( &v, pRaw + at, 4 );
        return v;
    };
    const uint32_t typeTableOffset = u32At( 0x0c );
    const uint32_t dataOffset = u32At( 0x10 );
    const uint32_t unknown = u32At( 0x14 );
    const uint32_t rootRef = u32At( 0x18 );

    // The file's own type records (the emission order the roundtrip preserves)
    // and its strings (the orphans included - the union is what byte-matches).
    TypeTable table;
    if ( !ReplayTypeRecords( pRaw, typeTableOffset, dataOffset, table ) ) {
        outError = "the type table did not replay";
        return false;
    }
    for ( size_t i = 0; i < table.records.size(); i++ ) {
        const TypeRecord &record = table.records[ i ];
        if ( record.kind == TypeRecord::Kind::Composite ) {
            table.compositeIndex[ record.Name ] = static_cast<uint32_t>( i );
        } else if ( record.kind == TypeRecord::Kind::Primitive ) {
            table.primitiveIndex[ record.Name ] = static_cast<uint32_t>( i );
        }
    }
    const std::vector<std::string> rawStrings = ReadRawStrings( pRaw, 0x1c, typeTableOffset );

    // The parsed value tree (the reader's traversal law), then the shared
    // serializer.
    XmbOutNode root;
    if ( !ReadValueTree( document, rootRef / 2, 0, "", root, outError ) ) {
        return false;
    }

    uint32_t typeTableOffsetOut = 0;
    std::unordered_map<std::string, uint32_t> stringOffsets;
    std::vector<uint8_t> stringTable =
        BuildStringTable( table.records, typeTableOffsetOut, stringOffsets, &rawStrings );
    DataWriter writer;
    writer.pTable = &table;
    writer.writeComposite( root );
    if ( !writer.drain() ) {
        outError = writer.error;
        return false;
    }

    std::vector<uint8_t> typeTable = BuildTypeTable( table );
    PatchNameOffsets( typeTable, table, stringOffsets );

    outBytes.resize( 0x08 );
    memcpy( outBytes.data(), &kXmbMagic, 4 );
    outBytes[ 4 ] = pRaw[ 4 ]; // the version bytes ride verbatim
    outBytes[ 5 ] = pRaw[ 5 ];
    AppendU32( outBytes, 0x1c );
    AppendU32( outBytes, typeTableOffsetOut );
    AppendU32( outBytes, typeTableOffsetOut + static_cast<uint32_t>( typeTable.size() ) );
    AppendU32( outBytes, unknown );
    AppendU32( outBytes, rootRef );
    outBytes.insert( outBytes.end(), stringTable.begin(), stringTable.end() );
    outBytes.insert( outBytes.end(), typeTable.begin(), typeTable.end() );
    outBytes.insert( outBytes.end(), writer.out.begin(), writer.out.end() );
    return true;
}
