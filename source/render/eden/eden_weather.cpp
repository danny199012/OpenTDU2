#include "shared.h"
#include "system/cmd_line.h"
#include "eden_weather.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"

Vector4f WeatherPreset::get( const char *pName, const Vector4f &defaultValue ) const {
    if ( ActiveIntempery >= 0 && ActiveIntempery < static_cast<int32_t>( Intemperies.size() ) ) {
        auto itOverlay = Intemperies[ActiveIntempery].find( pName );
        if ( itOverlay != Intemperies[ActiveIntempery].end() ) {
            return itOverlay->second;
        }
    }
    auto it = Values.find( pName );
    return it != Values.end() ? it->second : defaultValue;
}

float WeatherPreset::getFloat( const char *pName, float defaultValue ) const {
    return get( pName, Vector4f( defaultValue, 0.0f, 0.0f, 0.0f ) ).x;
}

Vector3f WeatherPreset::getVector3( const char *pName, const Vector3f &defaultValue ) const {
    return get( pName, Vector4f( defaultValue.x, defaultValue.y, defaultValue.z, 0.0f ) ).xyz();
}

Vector4f WeatherPreset::getVector4( const char *pName, const Vector4f &defaultValue ) const {
    return get( pName, defaultValue );
}

bool WeatherLibrary::load( const char *pPath ) {
    presets.clear();
    variants.clear();
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        OTDU_LOG_WARN( "Weather library '%s' not found\n", pPath );
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        return false;
    }
    const int32_t typeFloat[5] = { -1, document.findType( "float1" ), document.findType( "float2" ),
                                   document.findType( "float3" ), document.findType( "float4" ) };
    const int32_t typeString = document.findType( "STRING_PARAM" );

    // One EXPORT_INTEMPERY of a weather, laid over `preset` (the base values of
    // the weather stay for every parameter the intempery does not carry).
    auto readIntempery = [&]( uint32_t intemperiesOffset, uint32_t index, WeatherPreset &preset ) {
        const uint32_t numParams = document.readU32( intemperiesOffset + index * 8 );
        const uint32_t paramsOffset = document.readU32( intemperiesOffset + index * 8 + 4 );
        for ( uint32_t k = 0; k < numParams; k++ ) {
            const uint32_t param = paramsOffset + k * 12;
            const std::string name = document.readString( param );
            const int32_t typeIndex = static_cast<int32_t>( document.readU32( param + 4 ) / 2 );
            const uint32_t valueOffset = document.readU32( param + 8 );
            bool bFloat = false;
            Vector4f value;
            for ( int32_t c = 1; c <= 4; c++ ) {
                if ( typeIndex == typeFloat[c] ) {
                    float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    for ( int32_t n = 0; n < c; n++ ) {
                        const uint32_t bits = document.readU32( valueOffset + n * 4 );
                        memcpy( &components[n], &bits, sizeof( float ) );
                    }
                    value = Vector4f( components[0], components[1], components[2], components[3] );
                    bFloat = true;
                }
            }
            if ( bFloat ) {
                preset.Values[name] = value;
            } else if ( typeIndex == typeString ) {
                preset.Strings[name] = document.readString( valueOffset );
            }
        }
    };

    const uint32_t numWeathers = document.readU32( 0 );
    const uint32_t weathersOffset = document.readU32( 4 );
    for ( uint32_t i = 0; i < numWeathers; i++ ) {
        const uint32_t weather = weathersOffset + i * 24;
        WeatherPreset preset;
        preset.Name = document.readString( weather );
        preset.Hour = static_cast<int32_t>( document.readU32( weather + 4 ) );
        preset.Minutes = static_cast<int32_t>( document.readU32( weather + 8 ) );
        const uint32_t numIntemperies = document.readU32( weather + 16 );
        const uint32_t intemperiesOffset = document.readU32( weather + 20 );
        // Intempery 0: base (clear) values, the following ones override them.
        // The base block is short (16 parameters: tunnel / headlight values), so
        // the file's first full set (intempery 1) is what the presets carry. The
        // clear (0) and cloudy (1) blocks merge into Values (the map every reader
        // sees by default); the further blocks (rain, storm, drying...) each land
        // in their own overlay so -intempery <n> can resolve them over the base.
        preset.Intemperies.resize( numIntemperies );
        preset.IntemperyStrings.resize( numIntemperies );
        for ( uint32_t j = 0; j < numIntemperies; j++ ) {
            const uint32_t numParams = document.readU32( intemperiesOffset + j * 8 );
            const uint32_t paramsOffset = document.readU32( intemperiesOffset + j * 8 + 4 );
            std::unordered_map<std::string, Vector4f> &values =
                j < 2 ? preset.Values : preset.Intemperies[j];
            std::unordered_map<std::string, std::string> &strings =
                j < 2 ? preset.Strings : preset.IntemperyStrings[j];
            for ( uint32_t k = 0; k < numParams; k++ ) {
                const uint32_t param = paramsOffset + k * 12;
                const std::string name = document.readString( param );
                const int32_t typeIndex = static_cast<int32_t>( document.readU32( param + 4 ) / 2 );
                const uint32_t valueOffset = document.readU32( param + 8 );
                bool bFloat = false;
                Vector4f value;
                for ( int32_t c = 1; c <= 4; c++ ) {
                    if ( typeIndex == typeFloat[c] ) {
                        float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        for ( int32_t n = 0; n < c; n++ ) {
                            const uint32_t bits = document.readU32( valueOffset + n * 4 );
                            memcpy( &components[n], &bits, sizeof( float ) );
                        }
                        value = Vector4f( components[0], components[1], components[2], components[3] );
                        bFloat = true;
                    }
                }
                if ( bFloat ) {
                    values[name] = value;
                } else if ( typeIndex == typeString ) {
                    strings[name] = document.readString( valueOffset );
                }
            }
        }
        presets.push_back( preset );
        // Every full set: the base with that intempery's values laid over it
        // (they carry the whole parameter set, so this is what the retail game
        // applies when it picks one - clouds, fog, rain, wet ground...). The
        // in-game clock's weather cycle walks these (getVariants / findVariant).
        // readIntempery re-reads the block over the base copy - the same
        // resolve WeatherPreset::get does for -intempery.
        for ( uint32_t j = 1; j < numIntemperies; j++ ) {
            WeatherPreset variant = presets.back();
            variant.Intemperies.clear();
            variant.Intempery = static_cast<int32_t>( j );
            readIntempery( intemperiesOffset, j, variant );
            variants.push_back( std::move( variant ) );
        }
    }
    OTDU_LOG_DEBUG( "Weather library '%s': %zu presets, %zu intemperies\n", pPath, presets.size(), variants.size() );
    return !presets.empty();
}

const WeatherPreset *WeatherLibrary::findVariant( const char *pName, int32_t intempery ) const {
    for ( const WeatherPreset &preset : variants ) {
        if ( preset.Intempery == intempery && StringCmpI( preset.Name.c_str(), pName ) == 0 ) {
            return &preset;
        }
    }
    return nullptr;
}

int32_t WeatherLibrary::getIntemperyCount( const char *pName ) const {
    int32_t count = 0;
    for ( const WeatherPreset &preset : variants ) {
        if ( StringCmpI( preset.Name.c_str(), pName ) == 0 && preset.Intempery + 1 > count ) {
            count = preset.Intempery + 1;
        }
    }
    return count;
}

const WeatherPreset *WeatherLibrary::find( const char *pName ) const {
    for ( const WeatherPreset &preset : presets ) {
        if ( StringCmpI( preset.Name.c_str(), pName ) == 0 ) {
            return &preset;
        }
    }
    return nullptr;
}

bool WeatherLibrary::loadModifiers( const char *pPath ) {
    modifiers.clear();
    modifiedPresets.clear();
    std::vector<uint8_t> buffer;
    if ( !gpFile->loadFile( pPath, buffer ) ) {
        OTDU_LOG_WARN( "Weather modifiers '%s' not found\n", pPath );
        return false;
    }
    XmbDocument document;
    if ( !document.load( buffer ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "modifier" ); i++ ) {
        XmbValue entry = root.getArrayElement( "modifier", i );
        Modifier modifier;
        modifier.Name = entry["name"].asString();
        modifier.WeatherName = entry["weather_name"].asString();
        modifier.IntemperyIndex = static_cast<int32_t>( entry["intempery_index"].asInt( -1 ) );
        // EXPORT_PARAM { String name; Object param }: an Object is stored as
        // (type index * 2, data offset) - the same raw layout the weather
        // library reads. (The schema accessor `param["param"]` has no type for
        // an Object field, which dropped every modifier value: the cinematic
        // shots' Intro_02..15 overrides never applied.)
        const int32_t typeFloat[5] = { -1, document.findType( "float1" ), document.findType( "float2" ),
                                       document.findType( "float3" ), document.findType( "float4" ) };
        const int32_t typeString = document.findType( "STRING_PARAM" );
        for ( uint32_t j = 0; j < entry.getArraySize( "param" ); j++ ) {
            XmbValue param = entry.getArrayElement( "param", j );
            const uint32_t at = param.getOffset();
            const std::string name = document.readString( at );
            const int32_t typeIndex = static_cast<int32_t>( document.readU32( at + 4 ) / 2 );
            const uint32_t valueOffset = document.readU32( at + 8 );
            if ( typeIndex == typeString ) {
                modifier.Strings[name] = document.readString( valueOffset );
                continue;
            }
            for ( int32_t n = 1; n <= 4; n++ ) {
                if ( typeIndex == typeFloat[n] ) {
                    float components[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    for ( int32_t k = 0; k < n; k++ ) {
                        const uint32_t bits = document.readU32( valueOffset + k * 4 );
                        memcpy( &components[k], &bits, sizeof( float ) );
                    }
                    modifier.Values[name] = Vector4f( components[0], components[1], components[2], components[3] );
                    break;
                }
            }
        }
        if ( CmdLine::HasArg( "-logmodifiers" ) ) {
            auto it = modifier.Values.find( "sun_shadow_opacity" );
            OTDU_LOG_INFO( "MODIFIER '%s' weather '%s' intempery %d: %zu values%s%.2f\n", modifier.Name.c_str(),
                           modifier.WeatherName.c_str(), modifier.IntemperyIndex, modifier.Values.size(),
                           it != modifier.Values.end() ? " sun_shadow_opacity " : " (no shadow opacity) ",
                           it != modifier.Values.end() ? it->second.x : 0.0f );
        }
        modifiers.push_back( std::move( modifier ) );
    }
    OTDU_LOG_DEBUG( "Weather modifiers '%s': %zu\n", pPath, modifiers.size() );
    return !modifiers.empty();
}

const WeatherPreset *WeatherLibrary::findModified( const char *pWeather, const char *pModifier ) {
    const WeatherPreset *pBase = find( pWeather );
    if ( pBase == nullptr || pModifier == nullptr || pModifier[0] == '\0' ) {
        return pBase;
    }
    const std::string key = std::string( pWeather ) + "|" + pModifier;
    auto it = modifiedPresets.find( key );
    if ( it != modifiedPresets.end() ) {
        return it->second.get();
    }
    auto pPreset = std::make_unique<WeatherPreset>( *pBase );
    bool bFound = false;
    // Global parameters (intempery -1), then the clear intempery (0).
    for ( int32_t intempery : { -1, 0 } ) {
        for ( const Modifier &modifier : modifiers ) {
            if ( modifier.IntemperyIndex != intempery || StringCmpI( modifier.Name.c_str(), pModifier ) != 0 ||
                 StringCmpI( modifier.WeatherName.c_str(), pWeather ) != 0 ) {
                continue;
            }
            for ( const auto &value : modifier.Values ) {
                pPreset->Values[value.first] = value.second;
            }
            for ( const auto &value : modifier.Strings ) {
                pPreset->Strings[value.first] = value.second;
            }
            bFound = true;
        }
    }
    if ( !bFound ) {
        OTDU_LOG_WARN( "Weather modifier '%s' not found for '%s'\n", pModifier, pWeather );
    }
    WeatherPreset *pResult = pPreset.get();
    modifiedPresets.emplace( key, std::move( pPreset ) );
    return pResult;
}
