#pragma once

// Minimal JSON for the scene editor (no third-party dependency in this repo):
// parse + write the subset a scene document / command protocol needs
// (objects, arrays, strings with the common escapes, numbers, bools, null).
// Object members keep their document order (a scene file must round trip
// human-editable, not re-hash into a map).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    inline bool isNull() const { return type == Type::Null; }
    inline bool isObject() const { return type == Type::Object; }
    inline bool isArray() const { return type == Type::Array; }
    inline bool isNumber() const { return type == Type::Number; }

    double asNumber( double fallback = 0.0 ) const { return type == Type::Number ? number : fallback; }
    bool asBool( bool fallback = false ) const { return type == Type::Bool ? boolean : fallback; }
    std::string asString( const std::string &fallback = std::string() ) const {
        return type == Type::String ? string : fallback;
    }
    // Object member lookup; nullptr when this is not an object or the key is
    // absent.
    const JsonValue *find( const char *pKey ) const;
    // find() + asNumber / asString sugar.
    double getNumber( const char *pKey, double fallback = 0.0 ) const;
    std::string getString( const char *pKey, const std::string &fallback = std::string() ) const;
    bool getBool( const char *pKey, bool fallback = false ) const;

    // Builders (keep the call sites readable).
    static JsonValue MakeNumber( double value );
    static JsonValue MakeString( const std::string &value );
    static JsonValue MakeBool( bool value );
    static JsonValue MakeObject();
    static JsonValue MakeArray();
    void add( const char *pKey, JsonValue value ) {
        type = Type::Object;
        object.emplace_back( pKey, std::move( value ) );
    }
    void push( JsonValue value ) {
        type = Type::Array;
        array.push_back( std::move( value ) );
    }
};

// Parses the text into outValue; false + outError (with a byte offset) when
// the document is malformed. Trailing content after the value is an error.
bool JsonParse( const char *pText, size_t textSize, JsonValue &outValue, std::string &outError );
inline bool JsonParse( const std::string &text, JsonValue &outValue, std::string &outError ) {
    return JsonParse( text.data(), text.size(), outValue, outError );
}
// Serializes pretty (2-space indent, one member / array element per line) so
// scene files stay hand-editable and diffable.
std::string JsonWrite( const JsonValue &value );
// Serializes on ONE line (the command protocol: a response is one JSON-lines
// record, and the batch log wants one line per result).
std::string JsonWriteCompact( const JsonValue &value );
