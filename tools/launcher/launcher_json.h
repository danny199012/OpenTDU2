#pragma once

// The smallest JSON reader the launcher's updater needs: the GitHub REST
// answers (releases / commits). Objects, arrays, strings, numbers, booleans and
// null; no serialisation. UTF-8 in, UTF-8 out.

#include <string>
#include <utility>
#include <vector>

namespace Launcher {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type GetType() const { return mType; }
    bool IsNull() const { return mType == Type::Null; }
    bool IsArray() const { return mType == Type::Array; }
    bool IsObject() const { return mType == Type::Object; }
    bool IsString() const { return mType == Type::String; }

    bool AsBool() const { return mBool; }
    double AsNumber() const { return mNumber; }
    const std::string &AsString() const { return mString; }
    const std::vector<JsonValue> &AsArray() const { return mArray; }
    const std::vector<std::pair<std::string, JsonValue>> &AsObject() const { return mObject; }
    size_t Size() const { return mArray.size(); }

    // Object member by key; nullptr when absent or not an object.
    const JsonValue *Find( const std::string &key ) const;
    // String member, or 'fallback' when absent / not a string.
    std::string GetString( const std::string &key, const std::string &fallback = std::string() ) const;

    static bool Parse( const std::string &text, JsonValue &outValue, std::string &outError );

private:
    friend class JsonParser;
    static JsonValue MakeNull() { return JsonValue(); }
    static JsonValue MakeBool( bool value ) {
        JsonValue v;
        v.mType = Type::Bool;
        v.mBool = value;
        return v;
    }
    static JsonValue MakeNumber( double value ) {
        JsonValue v;
        v.mType = Type::Number;
        v.mNumber = value;
        return v;
    }
    static JsonValue MakeString( std::string value ) {
        JsonValue v;
        v.mType = Type::String;
        v.mString = std::move( value );
        return v;
    }
    static JsonValue MakeArray() {
        JsonValue v;
        v.mType = Type::Array;
        return v;
    }
    static JsonValue MakeObject() {
        JsonValue v;
        v.mType = Type::Object;
        return v;
    }
    void AddElement( JsonValue value ) { mArray.push_back( std::move( value ) ); }
    void AddMember( std::string key, JsonValue value ) {
        mObject.emplace_back( std::move( key ), std::move( value ) );
    }

    Type mType = Type::Null;
    bool mBool = false;
    double mNumber = 0.0;
    std::string mString;
    std::vector<JsonValue> mArray;
    std::vector<std::pair<std::string, JsonValue>> mObject;
};

} // namespace Launcher
