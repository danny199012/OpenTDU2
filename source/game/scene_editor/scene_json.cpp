#include "shared.h"
#include "scene_json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Parser {
    const char *pText;
    size_t size;
    size_t pos = 0;
    std::string error;

    bool fail( const char *pMessage ) {
        if ( error.empty() ) {
            char buffer[ 128 ];
            snprintf( buffer, sizeof( buffer ), "%s at byte %u", pMessage, static_cast<uint32_t>( pos ) );
            error = buffer;
        }
        return false;
    }

    void skipSpace() {
        while ( pos < size ) {
            const char c = pText[ pos ];
            if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) {
                pos++;
            } else {
                break;
            }
        }
    }

    bool readValue( JsonValue &out ) {
        skipSpace();
        if ( pos >= size ) {
            return fail( "unexpected end of document" );
        }
        const char c = pText[ pos ];
        if ( c == '{' ) {
            return readObject( out );
        }
        if ( c == '[' ) {
            return readArray( out );
        }
        if ( c == '"' ) {
            out.type = JsonValue::Type::String;
            return readString( out.string );
        }
        if ( c == 't' || c == 'f' ) {
            const size_t start = pos;
            out.type = JsonValue::Type::Bool;
            if ( pos + 4 <= size && strncmp( pText + pos, "true", 4 ) == 0 ) {
                pos += 4;
                out.boolean = true;
                return true;
            }
            if ( pos + 5 <= size && strncmp( pText + pos, "false", 5 ) == 0 ) {
                pos += 5;
                out.boolean = false;
                return true;
            }
            pos = start;
            return fail( "bad literal" );
        }
        if ( c == 'n' ) {
            if ( pos + 4 <= size && strncmp( pText + pos, "null", 4 ) == 0 ) {
                pos += 4;
                out.type = JsonValue::Type::Null;
                return true;
            }
            return fail( "bad literal" );
        }
        return readNumber( out );
    }

    bool readNumber( JsonValue &out ) {
        const size_t start = pos;
        if ( pos < size && ( pText[ pos ] == '-' || pText[ pos ] == '+' ) ) {
            pos++;
        }
        bool bDigits = false;
        while ( pos < size ) {
            const char c = pText[ pos ];
            if ( ( c >= '0' && c <= '9' ) || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+' ) {
                if ( c >= '0' && c <= '9' ) {
                    bDigits = true;
                }
                pos++;
            } else {
                break;
            }
        }
        if ( !bDigits ) {
            pos = start;
            return fail( "expected a number" );
        }
        char buffer[ 64 ];
        const size_t length = pos - start;
        if ( length >= sizeof( buffer ) ) {
            return fail( "number too long" );
        }
        memcpy( buffer, pText + start, length );
        buffer[ length ] = 0;
        char *pEnd = nullptr;
        const double value = strtod( buffer, &pEnd );
        if ( pEnd == nullptr || *pEnd != '\0' || !std::isfinite( value ) ) {
            return fail( "bad number" );
        }
        out.type = JsonValue::Type::Number;
        out.number = value;
        return true;
    }

    bool readString( std::string &out ) {
        if ( pos >= size || pText[ pos ] != '"' ) {
            return fail( "expected a string" );
        }
        pos++;
        out.clear();
        while ( pos < size ) {
            const char c = pText[ pos ];
            if ( c == '"' ) {
                pos++;
                return true;
            }
            if ( c == '\\' ) {
                pos++;
                if ( pos >= size ) {
                    return fail( "unterminated escape" );
                }
                const char e = pText[ pos ];
                switch ( e ) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    // \uXXXX: BMP only (the scene strings are ASCII/UTF-8
                    // names; a surrogate pair is not worth the branch).
                    if ( pos + 4 >= size ) {
                        return fail( "bad \\u escape" );
                    }
                    char hex[ 5 ] = {};
                    memcpy( hex, pText + pos + 1, 4 );
                    const unsigned long code = strtoul( hex, nullptr, 16 );
                    pos += 4;
                    if ( code < 0x80 ) {
                        out += static_cast<char>( code );
                    } else if ( code < 0x800 ) {
                        out += static_cast<char>( 0xC0 | ( code >> 6 ) );
                        out += static_cast<char>( 0x80 | ( code & 0x3F ) );
                    } else {
                        out += static_cast<char>( 0xE0 | ( code >> 12 ) );
                        out += static_cast<char>( 0x80 | ( ( code >> 6 ) & 0x3F ) );
                        out += static_cast<char>( 0x80 | ( code & 0x3F ) );
                    }
                    break;
                }
                default:
                    return fail( "unknown escape" );
                }
                pos++;
                continue;
            }
            out += c;
            pos++;
        }
        return fail( "unterminated string" );
    }

    bool readObject( JsonValue &out ) {
        out.type = JsonValue::Type::Object;
        pos++; // '{'
        skipSpace();
        if ( pos < size && pText[ pos ] == '}' ) {
            pos++;
            return true;
        }
        while ( true ) {
            skipSpace();
            std::string key;
            if ( !readString( key ) ) {
                return false;
            }
            skipSpace();
            if ( pos >= size || pText[ pos ] != ':' ) {
                return fail( "expected ':'" );
            }
            pos++;
            JsonValue value;
            if ( !readValue( value ) ) {
                return false;
            }
            out.object.emplace_back( std::move( key ), std::move( value ) );
            skipSpace();
            if ( pos >= size ) {
                return fail( "unterminated object" );
            }
            if ( pText[ pos ] == ',' ) {
                pos++;
                continue;
            }
            if ( pText[ pos ] == '}' ) {
                pos++;
                return true;
            }
            return fail( "expected ',' or '}'" );
        }
    }

    bool readArray( JsonValue &out ) {
        out.type = JsonValue::Type::Array;
        pos++; // '['
        skipSpace();
        if ( pos < size && pText[ pos ] == ']' ) {
            pos++;
            return true;
        }
        while ( true ) {
            JsonValue value;
            if ( !readValue( value ) ) {
                return false;
            }
            out.array.push_back( std::move( value ) );
            skipSpace();
            if ( pos >= size ) {
                return fail( "unterminated array" );
            }
            if ( pText[ pos ] == ',' ) {
                pos++;
                continue;
            }
            if ( pText[ pos ] == ']' ) {
                pos++;
                return true;
            }
            return fail( "expected ',' or ']'" );
        }
    }
};

void WriteEscaped( std::string &out, const std::string &text ) {
    out += '"';
    for ( const char c : text ) {
        switch ( c ) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ( static_cast<unsigned char>( c ) < 0x20 ) {
                char buffer[ 8 ];
                snprintf( buffer, sizeof( buffer ), "\\u%04x", c );
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void WriteNumber( std::string &out, double value ) {
    if ( value == static_cast<double>( static_cast<long long>( value ) ) && fabs( value ) < 1e15 ) {
        char buffer[ 32 ];
        snprintf( buffer, sizeof( buffer ), "%lld", static_cast<long long>( value ) );
        out += buffer;
        return;
    }
    char buffer[ 32 ];
    snprintf( buffer, sizeof( buffer ), "%.6g", value );
    out += buffer;
}

void WriteIndent( std::string &out, int32_t depth ) {
    for ( int32_t i = 0; i < depth; i++ ) {
        out += "  ";
    }
}

void WriteValue( std::string &out, const JsonValue &value, int32_t depth ) {
    switch ( value.type ) {
    case JsonValue::Type::Null: out += "null"; break;
    case JsonValue::Type::Bool: out += value.boolean ? "true" : "false"; break;
    case JsonValue::Type::Number: WriteNumber( out, value.number ); break;
    case JsonValue::Type::String: WriteEscaped( out, value.string ); break;
    case JsonValue::Type::Array: {
        if ( value.array.empty() ) {
            out += "[]";
            break;
        }
        out += "[\n";
        for ( size_t i = 0; i < value.array.size(); i++ ) {
            WriteIndent( out, depth + 1 );
            WriteValue( out, value.array[ i ], depth + 1 );
            if ( i + 1 < value.array.size() ) {
                out += ',';
            }
            out += '\n';
        }
        WriteIndent( out, depth );
        out += ']';
        break;
    }
    case JsonValue::Type::Object: {
        if ( value.object.empty() ) {
            out += "{}";
            break;
        }
        out += "{\n";
        for ( size_t i = 0; i < value.object.size(); i++ ) {
            WriteIndent( out, depth + 1 );
            WriteEscaped( out, value.object[ i ].first );
            out += ": ";
            WriteValue( out, value.object[ i ].second, depth + 1 );
            if ( i + 1 < value.object.size() ) {
                out += ',';
            }
            out += '\n';
        }
        WriteIndent( out, depth );
        out += '}';
        break;
    }
    }
}

void WriteValueCompact( std::string &out, const JsonValue &value ) {
    switch ( value.type ) {
    case JsonValue::Type::Null: out += "null"; break;
    case JsonValue::Type::Bool: out += value.boolean ? "true" : "false"; break;
    case JsonValue::Type::Number: WriteNumber( out, value.number ); break;
    case JsonValue::Type::String: WriteEscaped( out, value.string ); break;
    case JsonValue::Type::Array: {
        out += '[';
        for ( size_t i = 0; i < value.array.size(); i++ ) {
            if ( i > 0 ) {
                out += ',';
            }
            WriteValueCompact( out, value.array[ i ] );
        }
        out += ']';
        break;
    }
    case JsonValue::Type::Object: {
        out += '{';
        for ( size_t i = 0; i < value.object.size(); i++ ) {
            if ( i > 0 ) {
                out += ',';
            }
            WriteEscaped( out, value.object[ i ].first );
            out += ':';
            WriteValueCompact( out, value.object[ i ].second );
        }
        out += '}';
        break;
    }
    }
}

} // namespace

const JsonValue *JsonValue::find( const char *pKey ) const {
    if ( type != Type::Object ) {
        return nullptr;
    }
    for ( const auto &member : object ) {
        if ( member.first == pKey ) {
            return &member.second;
        }
    }
    return nullptr;
}

double JsonValue::getNumber( const char *pKey, double fallback ) const {
    const JsonValue *pValue = find( pKey );
    return pValue != nullptr ? pValue->asNumber( fallback ) : fallback;
}

std::string JsonValue::getString( const char *pKey, const std::string &fallback ) const {
    const JsonValue *pValue = find( pKey );
    return pValue != nullptr ? pValue->asString( fallback ) : fallback;
}

bool JsonValue::getBool( const char *pKey, bool fallback ) const {
    const JsonValue *pValue = find( pKey );
    return pValue != nullptr ? pValue->asBool( fallback ) : fallback;
}

JsonValue JsonValue::MakeNumber( double value ) {
    JsonValue v;
    v.type = Type::Number;
    v.number = value;
    return v;
}

JsonValue JsonValue::MakeString( const std::string &value ) {
    JsonValue v;
    v.type = Type::String;
    v.string = value;
    return v;
}

JsonValue JsonValue::MakeBool( bool value ) {
    JsonValue v;
    v.type = Type::Bool;
    v.boolean = value;
    return v;
}

JsonValue JsonValue::MakeObject() {
    JsonValue v;
    v.type = Type::Object;
    return v;
}

JsonValue JsonValue::MakeArray() {
    JsonValue v;
    v.type = Type::Array;
    return v;
}

bool JsonParse( const char *pText, size_t textSize, JsonValue &outValue, std::string &outError ) {
    outValue = JsonValue();
    outError.clear();
    Parser parser{ pText, textSize };
    if ( !parser.readValue( outValue ) ) {
        outError = parser.error;
        return false;
    }
    parser.skipSpace();
    if ( parser.pos != parser.size ) {
        outError = "trailing content after the value";
        return false;
    }
    return true;
}

std::string JsonWrite( const JsonValue &value ) {
    std::string out;
    WriteValue( out, value, 0 );
    out += '\n';
    return out;
}

std::string JsonWriteCompact( const JsonValue &value ) {
    std::string out;
    WriteValueCompact( out, value );
    return out;
}
