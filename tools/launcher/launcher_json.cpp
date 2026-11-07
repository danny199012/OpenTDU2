#include "launcher_json.h"

#include <cstdlib>
#include <cstring>

namespace Launcher {

const JsonValue *JsonValue::Find( const std::string &key ) const {
    for ( const auto &member : mObject ) {
        if ( member.first == key ) {
            return &member.second;
        }
    }
    return nullptr;
}

std::string JsonValue::GetString( const std::string &key,
                                  const std::string &fallback ) const {
    const JsonValue *value = Find( key );
    return value != nullptr && value->IsString() ? value->AsString() : fallback;
}

namespace {

void AppendUtf8( std::string &out, unsigned int codePoint ) {
    if ( codePoint <= 0x7Fu ) {
        out += static_cast<char>( codePoint );
    } else if ( codePoint <= 0x7FFu ) {
        out += static_cast<char>( 0xC0u | ( codePoint >> 6 ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else if ( codePoint <= 0xFFFFu ) {
        out += static_cast<char>( 0xE0u | ( codePoint >> 12 ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else {
        out += static_cast<char>( 0xF0u | ( codePoint >> 18 ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 12 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( ( codePoint >> 6 ) & 0x3Fu ) );
        out += static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    }
}

} // namespace

class JsonParser {
public:
    explicit JsonParser( const std::string &text ) : mText( text ) {}

    bool ParseValue( JsonValue &outValue, std::string &outError, int depth = 0 ) {
        if ( depth > 64 ) {
            outError = "JSON nested too deeply";
            return false;
        }
        SkipWhitespace();
        if ( mAt >= mText.size() ) {
            outError = "JSON ended unexpectedly";
            return false;
        }
        const char c = mText[mAt];
        switch ( c ) {
        case '{': return ParseObject( outValue, outError, depth );
        case '[': return ParseArray( outValue, outError, depth );
        case '"': {
            std::string text;
            if ( !ParseString( text, outError ) ) {
                return false;
            }
            outValue = JsonValue::MakeString( text );
            return true;
        }
        case 't':
            if ( mText.compare( mAt, 4, "true" ) == 0 ) {
                mAt += 4;
                outValue = JsonValue::MakeBool( true );
                return true;
            }
            break;
        case 'f':
            if ( mText.compare( mAt, 5, "false" ) == 0 ) {
                mAt += 5;
                outValue = JsonValue::MakeBool( false );
                return true;
            }
            break;
        case 'n':
            if ( mText.compare( mAt, 4, "null" ) == 0 ) {
                mAt += 4;
                outValue = JsonValue::MakeNull();
                return true;
            }
            break;
        default:
            break;
        }
        if ( c == '-' || ( c >= '0' && c <= '9' ) ) {
            const char *begin = mText.c_str() + mAt;
            char *end = nullptr;
            const double number = std::strtod( begin, &end );
            if ( end == begin ) {
                outError = "Invalid JSON number";
                return false;
            }
            mAt += static_cast<size_t>( end - begin );
            outValue = JsonValue::MakeNumber( number );
            return true;
        }
        outError = std::string( "Unexpected JSON character '" ) + c + "'";
        return false;
    }

private:
    void SkipWhitespace() {
        while ( mAt < mText.size() ) {
            const char c = mText[mAt];
            if ( c == ' ' || c == '\t' || c == '\n' || c == '\r' ) {
                mAt++;
            } else {
                break;
            }
        }
    }

    bool ParseString( std::string &outText, std::string &outError ) {
        if ( mAt >= mText.size() || mText[mAt] != '"' ) {
            outError = "Expected a JSON string";
            return false;
        }
        mAt++;
        outText.clear();
        while ( mAt < mText.size() ) {
            const char c = mText[mAt++];
            if ( c == '"' ) {
                return true;
            }
            if ( c != '\\' ) {
                outText += c;
                continue;
            }
            if ( mAt >= mText.size() ) {
                break;
            }
            const char escape = mText[mAt++];
            switch ( escape ) {
            case '"': outText += '"'; break;
            case '\\': outText += '\\'; break;
            case '/': outText += '/'; break;
            case 'b': outText += '\b'; break;
            case 'f': outText += '\f'; break;
            case 'n': outText += '\n'; break;
            case 'r': outText += '\r'; break;
            case 't': outText += '\t'; break;
            case 'u': {
                if ( mAt + 4 > mText.size() ) {
                    outError = "Bad \\u escape";
                    return false;
                }
                const std::string hex = mText.substr( mAt, 4 );
                mAt += 4;
                const unsigned int code =
                    static_cast<unsigned int>( std::strtoul( hex.c_str(), nullptr, 16 ) );
                unsigned int codePoint = code;
                if ( code >= 0xD800u && code <= 0xDBFFu && mAt + 6 <= mText.size() &&
                     mText[mAt] == '\\' && mText[mAt + 1] == 'u' ) {
                    const std::string lowHex = mText.substr( mAt + 2, 4 );
                    const unsigned int low = static_cast<unsigned int>(
                        std::strtoul( lowHex.c_str(), nullptr, 16 ) );
                    if ( low >= 0xDC00u && low <= 0xDFFFu ) {
                        mAt += 6;
                        codePoint = 0x10000u + ( ( code - 0xD800u ) << 10 ) + ( low - 0xDC00u );
                    }
                }
                AppendUtf8( outText, codePoint );
                break;
            }
            default: outError = "Bad JSON escape"; return false;
            }
        }
        outError = "Unterminated JSON string";
        return false;
    }

    bool ParseObject( JsonValue &outValue, std::string &outError, int depth ) {
        mAt++; // '{'
        JsonValue object = JsonValue::MakeObject();
        SkipWhitespace();
        if ( mAt < mText.size() && mText[mAt] == '}' ) {
            mAt++;
            outValue = std::move( object );
            return true;
        }
        for ( ;; ) {
            SkipWhitespace();
            std::string key;
            if ( !ParseString( key, outError ) ) {
                return false;
            }
            SkipWhitespace();
            if ( mAt >= mText.size() || mText[mAt] != ':' ) {
                outError = "Expected ':' in a JSON object";
                return false;
            }
            mAt++;
            JsonValue value;
            if ( !ParseValue( value, outError, depth + 1 ) ) {
                return false;
            }
            object.AddMember( key, std::move( value ) );
            SkipWhitespace();
            if ( mAt >= mText.size() ) {
                outError = "Unterminated JSON object";
                return false;
            }
            if ( mText[mAt] == ',' ) {
                mAt++;
                continue;
            }
            if ( mText[mAt] == '}' ) {
                mAt++;
                outValue = std::move( object );
                return true;
            }
            outError = "Expected ',' or '}' in a JSON object";
            return false;
        }
    }

    bool ParseArray( JsonValue &outValue, std::string &outError, int depth ) {
        mAt++; // '['
        JsonValue array = JsonValue::MakeArray();
        SkipWhitespace();
        if ( mAt < mText.size() && mText[mAt] == ']' ) {
            mAt++;
            outValue = std::move( array );
            return true;
        }
        for ( ;; ) {
            JsonValue value;
            if ( !ParseValue( value, outError, depth + 1 ) ) {
                return false;
            }
            array.AddElement( std::move( value ) );
            SkipWhitespace();
            if ( mAt >= mText.size() ) {
                outError = "Unterminated JSON array";
                return false;
            }
            if ( mText[mAt] == ',' ) {
                mAt++;
                continue;
            }
            if ( mText[mAt] == ']' ) {
                mAt++;
                outValue = std::move( array );
                return true;
            }
            outError = "Expected ',' or ']' in a JSON array";
            return false;
        }
    }

    const std::string &mText;
    size_t mAt = 0;
};

bool JsonValue::Parse( const std::string &text, JsonValue &outValue, std::string &outError ) {
    outError.clear();
    JsonParser parser( text );
    if ( !parser.ParseValue( outValue, outError ) ) {
        return false;
    }
    return true;
}

} // namespace Launcher

