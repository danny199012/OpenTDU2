#include "shared.h"
#include "scene_cmd.h"

#include "filesystem/gs_file.h"
#include "game/scene_editor/gm_scene_editor.h"
#include "game/scene_editor/scene_json.h"
#include "game/spots/mod_props.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment( lib, "ws2_32.lib" )
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

// --- the command interpreter -------------------------------------------------

namespace {

Vector3f ReadVec3( const JsonValue *pValue, const Vector3f &fallback ) {
    if ( pValue == nullptr || !pValue->isArray() || pValue->array.size() < 3 ) {
        return fallback;
    }
    return Vector3f( static_cast<float>( pValue->array[ 0 ].asNumber() ),
                     static_cast<float>( pValue->array[ 1 ].asNumber() ),
                     static_cast<float>( pValue->array[ 2 ].asNumber() ) );
}

JsonValue ResultOk( const char *pNote = nullptr ) {
    JsonValue result = JsonValue::MakeObject();
    result.add( "ok", JsonValue::MakeBool( true ) );
    if ( pNote != nullptr ) {
        result.add( "note", JsonValue::MakeString( pNote ) );
    }
    return result;
}

JsonValue ResultOk( const std::string &note ) {
    JsonValue result = JsonValue::MakeObject();
    result.add( "ok", JsonValue::MakeBool( true ) );
    result.add( "note", JsonValue::MakeString( note ) );
    return result;
}

JsonValue ResultError( const std::string &error ) {
    JsonValue result = JsonValue::MakeObject();
    result.add( "ok", JsonValue::MakeBool( false ) );
    result.add( "error", JsonValue::MakeString( error ) );
    return result;
}

JsonValue ObjectToJson( const SceneObject &object ) {
    JsonValue out = JsonValue::MakeObject();
    out.add( "id", JsonValue::MakeString( object.Id ) );
    out.add( "kind", JsonValue::MakeString( object.Kind ) );
    out.add( "ref", JsonValue::MakeString( object.Ref ) );
    out.add( "parent", JsonValue::MakeString( object.Parent ) );
    JsonValue pos = JsonValue::MakeArray();
    pos.push( JsonValue::MakeNumber( object.Position.x ) );
    pos.push( JsonValue::MakeNumber( object.Position.y ) );
    pos.push( JsonValue::MakeNumber( object.Position.z ) );
    out.add( "pos", std::move( pos ) );
    out.add( "yaw", JsonValue::MakeNumber( object.Yaw ) );
    out.add( "pitch", JsonValue::MakeNumber( object.Pitch ) );
    out.add( "roll", JsonValue::MakeNumber( object.Roll ) );
    JsonValue scale = JsonValue::MakeArray();
    scale.push( JsonValue::MakeNumber( object.Scale.x ) );
    scale.push( JsonValue::MakeNumber( object.Scale.y ) );
    scale.push( JsonValue::MakeNumber( object.Scale.z ) );
    out.add( "scale", std::move( scale ) );
    out.add( "visible", JsonValue::MakeBool( object.bVisible ) );
    return out;
}

std::string Serialize( const JsonValue &value ) {
    return JsonWriteCompact( value ); // one line: the JSON-lines protocol + the batch log
}

// The world position the camera looks at (8 m ahead + snapped), the "here"
// of add/place commands without an explicit pos.
Vector3f EditorFocusPoint( GMSceneEditor &editor ) {
    const Vector3f focus = editor.position + editor.getForward() * 8.0f;
    float ground = 0.0f;
    if ( editor.bWorldReady &&
         editor.view.getWorld().getSurfaceHeight( focus.x, focus.z, editor.position.y + 50.0f, ground ) ) {
        return Vector3f( focus.x, ground, focus.z );
    }
    return focus;
}

} // namespace

std::string SceneEditorExecuteCommand( GMSceneEditor &editor, const char *pJsonText, std::string &outError ) {
    JsonValue command;
    if ( !JsonParse( pJsonText, strlen( pJsonText ), command, outError ) ) {
        return Serialize( ResultError( outError ) );
    }
    if ( !command.isObject() ) {
        outError = "not a command object";
        return Serialize( ResultError( outError ) );
    }
    const std::string cmd = ToLower( command.getString( "cmd", command.getString( "command", "" ) ) );
    if ( cmd.empty() ) {
        return Serialize( ResultError( "no 'cmd' member" ) );
    }
    outError.clear();

    JsonValue result;
    if ( cmd == "ping" ) {
        result = ResultOk( "pong" );
    } else if ( cmd == "status" ) {
        result = ResultOk();
        result.add( "island", JsonValue::MakeString( editor.island ) );
        result.add( "world_ready", JsonValue::MakeBool( editor.bWorldReady ) );
        result.add( "scene_name", JsonValue::MakeString( editor.doc.Name ) );
        result.add( "num_objects", JsonValue::MakeNumber( static_cast<double>( editor.doc.Objects.size() ) ) );
        result.add( "num_overrides", JsonValue::MakeNumber( static_cast<double>( editor.doc.Overrides.size() ) ) );
        result.add( "num_roads", JsonValue::MakeNumber( static_cast<double>( editor.doc.Roads.size() ) ) );
        result.add( "selected", JsonValue::MakeString( editor.selectedId ) );
        result.add( "walk_active", JsonValue::MakeBool( editor.isWalkActive() ) );
        result.add( "walk_interior", JsonValue::MakeString( editor.getWalkInteriorId() ) );
        JsonValue pos = JsonValue::MakeArray();
        pos.push( JsonValue::MakeNumber( editor.position.x ) );
        pos.push( JsonValue::MakeNumber( editor.position.y ) );
        pos.push( JsonValue::MakeNumber( editor.position.z ) );
        result.add( "camera", std::move( pos ) );
    } else if ( cmd == "new_scene" ) {
        editor.pushUndo();
        editor.doc = SceneDocument();
        editor.doc.Island = editor.island;
        editor.doc.Name = command.getString( "name", "" );
        editor.selectedId.clear();
        editor.markSceneChanged();
        result = ResultOk( "new scene" );
    } else if ( cmd == "load_scene" ) {
        const std::string file = command.getString( "file", "" );
        std::string error;
        if ( file.empty() || !editor.doc.loadFromVfs( file, error ) ) {
            result = ResultError( error.empty() ? "no 'file'" : error );
        } else {
            editor.selectedId.clear();
            editor.applyOverrides();
            editor.markSceneChanged();
            result = ResultOk( editor.doc.Name );
            result.add( "num_objects", JsonValue::MakeNumber( static_cast<double>( editor.doc.Objects.size() ) ) );
        }
    } else if ( cmd == "save_scene" ) {
        std::string file = command.getString( "file", "" );
        if ( file.empty() ) {
            file = ( editor.doc.Name.empty() ? std::string( "untitled" ) : editor.doc.Name ) + ".scene.json";
        }
        // The scene's name follows the file (without the extension).
        const size_t cut = file.find( ".scene.json" );
        editor.doc.Name = cut != std::string::npos ? file.substr( 0, cut ) : file;
        std::string error;
        if ( editor.doc.saveToMods( file, error ) ) {
            result = ResultOk( file );
        } else {
            result = ResultError( error );
        }
    } else if ( cmd == "list_scenes" ) {
        result = ResultOk();
        JsonValue files = JsonValue::MakeArray();
        for ( const std::string &name : ListSceneFiles() ) {
            files.push( JsonValue::MakeString( name ) );
        }
        result.add( "files", std::move( files ) );
    } else if ( cmd == "set_island" ) {
        const std::string name = ToLower( command.getString( "name", "" ) );
        bool bKnown = false;
        for ( const char *pIsland : GMSceneEditor::kIslands ) {
            bKnown |= name == pIsland;
        }
        if ( !bKnown ) {
            result = ResultError( "unknown island: " + name );
        } else {
            editor.pendingIsland = name;
            result = ResultOk( name );
        }
    } else if ( cmd == "list_assets" ) {
        editor.refreshAssets();
        const std::string filter = ToLower( command.getString( "filter", "" ) );
        const std::string kind = ToLower( command.getString( "kind", "" ) );
        result = ResultOk();
        JsonValue list = JsonValue::MakeArray();
        for ( const GMSceneEditor::AssetEntry &entry : editor.assets ) {
            if ( !kind.empty() && entry.Kind != kind ) {
                continue;
            }
            if ( !filter.empty() && ToLower( entry.Name ).find( filter ) == std::string::npos ) {
                continue;
            }
            JsonValue item = JsonValue::MakeObject();
            item.add( "name", JsonValue::MakeString( entry.Name ) );
            item.add( "kind", JsonValue::MakeString( entry.Kind ) );
            list.push( std::move( item ) );
        }
        result.add( "assets", std::move( list ) );
    } else if ( cmd == "add" || cmd == "place" ) {
        const std::string kind = ToLower( command.getString( "kind", "" ) );
        const std::string ref = command.getString( "ref", command.getString( "name", "" ) );
        if ( kind.empty() ) {
            result = ResultError( "no 'kind' (group|library|actor|spot|interior|prop)" );
        } else {
            const JsonValue *pPos = command.find( "pos" );
            const Vector3f at = pPos != nullptr ? ReadVec3( pPos, Vector3f() ) : EditorFocusPoint( editor );
            const std::string id = editor.placeAsset( kind, ref, at, pPos == nullptr );
            SceneObject *pObject = editor.doc.findObject( id );
            if ( pObject != nullptr ) {
                pObject->Yaw = static_cast<float>( command.getNumber( "yaw", pObject->Yaw ) );
                pObject->Pitch = static_cast<float>( command.getNumber( "pitch", pObject->Pitch ) );
                pObject->Roll = static_cast<float>( command.getNumber( "roll", pObject->Roll ) );
                editor.markSceneChanged();
            }
            editor.selectedId = id;
            result = ResultOk( id );
            result.add( "id", JsonValue::MakeString( id ) );
        }
    } else if ( cmd == "remove" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        if ( editor.doc.findObject( id ) == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            editor.selectedId = id;
            editor.deleteSelected();
            result = ResultOk( id );
        }
    } else if ( cmd == "duplicate" ) {
        editor.selectedId = command.getString( "id", editor.selectedId );
        editor.duplicateSelected();
        result = ResultOk( editor.selectedId );
        result.add( "id", JsonValue::MakeString( editor.selectedId ) );
    } else if ( cmd == "select" ) {
        const std::string id = command.getString( "id", "" );
        if ( !id.empty() && editor.doc.findObject( id ) == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            editor.selectedId = id;
            result = ResultOk( id );
        }
    } else if ( cmd == "get_object" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        const SceneObject *pObject = editor.doc.findObject( id );
        if ( pObject == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            result = ResultOk();
            result.add( "object", ObjectToJson( *pObject ) );
        }
    } else if ( cmd == "list_objects" ) {
        result = ResultOk();
        JsonValue list = JsonValue::MakeArray();
        for ( const SceneObject &object : editor.doc.Objects ) {
            list.push( ObjectToJson( object ) );
        }
        result.add( "objects", std::move( list ) );
    } else if ( cmd == "set_transform" || cmd == "move" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        SceneObject *pObject = editor.doc.findObject( id );
        if ( pObject == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            editor.pushUndo();
            const JsonValue *pPos = command.find( "pos" );
            if ( pPos != nullptr ) {
                pObject->Position = ReadVec3( pPos, pObject->Position );
            }
            if ( command.find( "yaw" ) != nullptr ) {
                pObject->Yaw = static_cast<float>( command.getNumber( "yaw", pObject->Yaw ) );
            }
            if ( command.find( "pitch" ) != nullptr ) {
                pObject->Pitch = static_cast<float>( command.getNumber( "pitch", pObject->Pitch ) );
            }
            if ( command.find( "roll" ) != nullptr ) {
                pObject->Roll = static_cast<float>( command.getNumber( "roll", pObject->Roll ) );
            }
            const JsonValue *pScale = command.find( "scale" );
            if ( pScale != nullptr ) {
                if ( pScale->isNumber() ) {
                    pObject->Scale = Vector3f( static_cast<float>( pScale->asNumber() ), static_cast<float>( pScale->asNumber() ),
                                               static_cast<float>( pScale->asNumber() ) );
                } else {
                    pObject->Scale = ReadVec3( pScale, pObject->Scale );
                }
            }
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "set_visible" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        SceneObject *pObject = editor.doc.findObject( id );
        if ( pObject == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            editor.pushUndo();
            pObject->bVisible = command.getBool( "visible", !pObject->bVisible );
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "attach" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        SceneObject *pObject = editor.doc.findObject( id );
        const std::string parent = command.getString( "parent", "" );
        if ( pObject == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else if ( parent == id ) {
            result = ResultError( "an object cannot attach to itself" );
        } else {
            editor.pushUndo();
            pObject->Parent = parent;
            editor.markSceneChanged();
            result = ResultOk( parent );
        }
    } else if ( cmd == "detach" ) {
        const std::string id = command.getString( "id", editor.selectedId );
        SceneObject *pObject = editor.doc.findObject( id );
        if ( pObject == nullptr ) {
            result = ResultError( "no such object: " + id );
        } else {
            editor.pushUndo();
            pObject->Parent.clear();
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "snap_ground" ) {
        editor.selectedId = command.getString( "id", editor.selectedId );
        editor.snapSelectedToGround();
        result = ResultOk( editor.statusText );
    } else if ( cmd == "teleport" || cmd == "camera" ) {
        if ( editor.isWalkActive() ) {
            editor.exitWalk(); // the walk eye parks the camera first
        }
        const JsonValue *pPos = command.find( "pos" );
        if ( pPos != nullptr ) {
            editor.position = ReadVec3( pPos, editor.position );
        }
        if ( command.find( "yaw" ) != nullptr ) {
            editor.yaw = static_cast<float>( command.getNumber( "yaw", editor.yaw * 180.0f / 3.14159265f ) ) *
                         3.14159265f / 180.0f;
        }
        if ( command.find( "pitch" ) != nullptr ) {
            editor.pitch = static_cast<float>( command.getNumber( "pitch", editor.pitch * 180.0f / 3.14159265f ) ) *
                           3.14159265f / 180.0f;
        }
        result = ResultOk();
    } else if ( cmd == "look_at" ) {
        const Vector3f target = ReadVec3( command.find( "pos" ), Vector3f() );
        const Vector3f delta = target - editor.position;
        if ( delta.length() > 1e-4f ) {
            editor.yaw = std::atan2( delta.x, delta.z );
            editor.pitch = std::asin( Clamp( delta.y / delta.length(), -1.0f, 1.0f ) );
        }
        result = ResultOk();
    } else if ( cmd == "hide_node" || cmd == "hide_material" ) {
        const std::string target = command.getString( "target", "" );
        if ( target.empty() ) {
            result = ResultError( "no 'target' (a node / material name, or hc:<hex>)" );
        } else {
            editor.addOverride( cmd == "hide_material" ? "material" : "node", target );
            result = ResultOk( target );
        }
    } else if ( cmd == "set_material_color" ) {
        // M6: the material color override (the Diffuse.rgb replaced; the
        // same targets the hides use). color = [r, g, b] in 0..1.
        const std::string target = command.getString( "target", "" );
        const JsonValue *pColor = command.find( "color" );
        if ( target.empty() || pColor == nullptr || !pColor->isArray() || pColor->array.size() < 3 ) {
            result = ResultError( "no 'target' or 'color' ([r, g, b] in 0..1)" );
        } else {
            const Vector4f color( static_cast<float>( pColor->array[ 0 ].asNumber() ),
                                  static_cast<float>( pColor->array[ 1 ].asNumber() ),
                                  static_cast<float>( pColor->array[ 2 ].asNumber() ), 1.0f );
            editor.addColorOverride( target, color );
            result = ResultOk( target );
        }
    } else if ( cmd == "move_axis" ) {
        // M6: the gizmo's axis drag, AI-drivable (the same screen-delta math
        // the mouse drag uses, synthesized from the screen centre).
        const std::string axis = ToLower( command.getString( "axis", "x" ) );
        GMSceneEditor::eGizmoAxis axisId = GMSceneEditor::eGizmoAxis::None;
        if ( axis == "x" ) {
            axisId = GMSceneEditor::eGizmoAxis::X;
        } else if ( axis == "y" ) {
            axisId = GMSceneEditor::eGizmoAxis::Y;
        } else if ( axis == "z" ) {
            axisId = GMSceneEditor::eGizmoAxis::Z;
        }
        if ( axisId == GMSceneEditor::eGizmoAxis::None ) {
            result = ResultError( "no 'axis' (x / y / z)" );
        } else if ( editor.gizmoAxisDrag( axisId, static_cast<float>( command.getNumber( "dx", 0.0 ) ),
                                          static_cast<float>( command.getNumber( "dy", 0.0 ) ) ) ) {
            result = ResultOk( editor.statusText );
            const SceneObject *pObject = editor.doc.findObject( editor.selectedId );
            if ( pObject != nullptr ) {
                result.add( "pos", [ & ]() {
                    JsonValue p = JsonValue::MakeArray();
                    p.push( JsonValue::MakeNumber( pObject->Position.x ) );
                    p.push( JsonValue::MakeNumber( pObject->Position.y ) );
                    p.push( JsonValue::MakeNumber( pObject->Position.z ) );
                    return p;
                }() );
            }
        } else {
            result = ResultError( editor.statusText.empty() ? "no selection" : editor.statusText );
        }
    } else if ( cmd == "clear_overrides" ) {
        editor.pushUndo();
        editor.doc.Overrides.clear();
        editor.applyOverrides();
        editor.markSceneChanged();
        result = ResultOk();
    } else if ( cmd == "list_overrides" ) {
        result = ResultOk();
        JsonValue list = JsonValue::MakeArray();
        for ( const SceneOverride &override : editor.doc.Overrides ) {
            JsonValue entry = JsonValue::MakeObject();
            entry.add( "kind", JsonValue::MakeString( override.Kind ) );
            entry.add( "target", JsonValue::MakeString( override.Target ) );
            list.push( std::move( entry ) );
        }
        result.add( "overrides", std::move( list ) );
    } else if ( cmd == "screenshot" ) {
        std::string path = command.getString( "path", "" );
        if ( path.empty() ) {
            result = ResultError( "no 'path'" );
        } else if ( gpRender == nullptr ) {
            result = ResultError( "no renderer" );
        } else {
            gpRender->requestScreenshot( path.c_str() );
            result = ResultOk( "requested (saved on the next present)" );
        }
    } else if ( cmd == "commit_props" ) {
        editor.commitPropsToGame();
        result = ResultOk( editor.statusText );
    } else if ( cmd == "export_spots" ) {
        // M5: the placements append as real spots.bin records through the
        // mods overlay (the game streams the scene with no engine change).
        editor.exportSpotsToGame();
        result = ResultOk( editor.statusText );
    } else if ( cmd == "export_pmi" ) {
        // M5: the library / actor placements append as .pmi records to the
        // landing sectors' banks (copied into the overlay).
        editor.exportPmiToGame();
        result = ResultOk( editor.statusText );
    } else if ( cmd == "export_bank" ) {
        // M5 last: the scene's props compiled into one new exterior bank
        // (<mods>/.../exterior/<name>.bnk) + the anchor's spot record.
        std::string name = command.getString( "name", "" );
        if ( name.empty() ) {
            name = editor.doc.Name.empty() ? "composed" : editor.doc.Name;
        }
        editor.exportBankToGame( name );
        result = ResultOk( editor.statusText );
    } else if ( cmd == "add_road" ) {
        std::string id = command.getString( "id", "" );
        if ( id.empty() ) {
            id = editor.doc.uniqueId( "road" );
        }
        if ( editor.doc.findRoad( id ) != nullptr ) {
            result = ResultError( "road exists: " + id );
        } else {
            editor.pushUndo();
            SceneRoad road;
            road.Id = id;
            road.Width = static_cast<float>( command.getNumber( "width", 7.0 ) );
            road.Lanes = static_cast<int32_t>( command.getNumber( "lanes", 2.0 ) );
            road.bClosed = command.getBool( "closed", false );
            road.bCollide = command.getBool( "collide", true );
            editor.doc.Roads.push_back( road );
            editor.activeRoadId = id;
            editor.markSceneChanged();
            result = ResultOk( id );
            result.add( "id", JsonValue::MakeString( id ) );
        }
    } else if ( cmd == "add_road_point" ) {
        const std::string id = command.getString( "road", editor.activeRoadId );
        SceneRoad *pRoad = editor.doc.findRoad( id );
        if ( pRoad == nullptr ) {
            result = ResultError( "no such road: " + id );
        } else {
            editor.pushUndo();
            const JsonValue *pPos = command.find( "pos" );
            Vector3f point = ReadVec3( pPos, EditorFocusPoint( editor ) );
            if ( pPos == nullptr || command.getBool( "snap", true ) ) {
                float ground = 0.0f;
                if ( editor.bWorldReady &&
                     editor.view.getWorld().getSurfaceHeight( point.x, point.z, point.y + 50.0f, ground ) ) {
                    point.y = ground;
                }
            }
            pRoad->Points.push_back( point );
            editor.markSceneChanged();
            result = ResultOk( id );
            result.add( "num_points", JsonValue::MakeNumber( static_cast<double>( pRoad->Points.size() ) ) );
        }
    } else if ( cmd == "set_road" ) {
        const std::string id = command.getString( "road", editor.activeRoadId );
        SceneRoad *pRoad = editor.doc.findRoad( id );
        if ( pRoad == nullptr ) {
            result = ResultError( "no such road: " + id );
        } else {
            editor.pushUndo();
            if ( command.find( "width" ) != nullptr ) {
                pRoad->Width = static_cast<float>( command.getNumber( "width", pRoad->Width ) );
            }
            if ( command.find( "lanes" ) != nullptr ) {
                pRoad->Lanes = static_cast<int32_t>( command.getNumber( "lanes", static_cast<double>( pRoad->Lanes ) ) );
            }
            if ( command.find( "closed" ) != nullptr ) {
                pRoad->bClosed = command.getBool( "closed", pRoad->bClosed );
            }
            if ( command.find( "collide" ) != nullptr ) {
                pRoad->bCollide = command.getBool( "collide", pRoad->bCollide );
            }
            if ( command.find( "texture" ) != nullptr ) {
                pRoad->Texture = command.getString( "texture", "" );
            }
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "remove_road_point" ) {
        const std::string id = command.getString( "road", editor.activeRoadId );
        SceneRoad *pRoad = editor.doc.findRoad( id );
        if ( pRoad == nullptr || pRoad->Points.empty() ) {
            result = ResultError( "no such road or no points: " + id );
        } else {
            editor.pushUndo();
            pRoad->Points.pop_back();
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "remove_road" ) {
        const std::string id = command.getString( "road", editor.activeRoadId );
        SceneRoad *pRoad = editor.doc.findRoad( id );
        if ( pRoad == nullptr ) {
            result = ResultError( "no such road: " + id );
        } else {
            editor.pushUndo();
            editor.doc.Roads.erase( editor.doc.Roads.begin() + ( pRoad - editor.doc.Roads.data() ) );
            if ( editor.activeRoadId == id ) {
                editor.activeRoadId.clear();
            }
            editor.markSceneChanged();
            result = ResultOk( id );
        }
    } else if ( cmd == "enter_walk" ) {
        // M4: first person. Fields: id? (an `interior` object; default the
        // selection, else the nearest placement; none -> the scene grounds),
        // pos? (bank-local start eye [x, y, z]), yaw? (bank-local degrees).
        const JsonValue *pPos = command.find( "pos" );
        Vector3f atBankLocal( FLT_MAX, FLT_MAX, FLT_MAX );
        if ( pPos != nullptr ) {
            atBankLocal = ReadVec3( pPos, atBankLocal );
        }
        float yawLocal = FLT_MAX;
        if ( command.find( "yaw" ) != nullptr ) {
            yawLocal = static_cast<float>( command.getNumber( "yaw", 0.0 ) );
        }
        // No id: the selection when it is an interior, else the nearest
        // placement (the enterWalk defaults; a non-interior selection does
        // not hijack the walk).
        std::string walkId = command.getString( "id", "" );
        if ( walkId.empty() ) {
            const SceneObject *pSelected = editor.doc.findObject( editor.selectedId );
            if ( pSelected != nullptr && pSelected->Kind == "interior" ) {
                walkId = editor.selectedId;
            }
        }
        if ( editor.enterWalk( walkId, atBankLocal, yawLocal ) ) {
            result = ResultOk( editor.getWalkNote() );
            result.add( "walk_interior", JsonValue::MakeString( editor.getWalkInteriorId() ) );
            JsonValue eye = JsonValue::MakeArray();
            eye.push( JsonValue::MakeNumber( editor.walkEyeWorld.x ) );
            eye.push( JsonValue::MakeNumber( editor.walkEyeWorld.y ) );
            eye.push( JsonValue::MakeNumber( editor.walkEyeWorld.z ) );
            result.add( "eye", std::move( eye ) );
        } else {
            result = ResultError( editor.getWalkNote() );
        }
    } else if ( cmd == "exit_walk" ) {
        if ( !editor.isWalkActive() ) {
            result = ResultError( "not walking" );
        } else {
            editor.exitWalk();
            result = ResultOk( "walk ended (the camera parked at the eye)" );
        }
    } else if ( cmd == "export_roads" ) {
        editor.exportRoadsFile();
        result = ResultOk( editor.statusText );
    } else if ( cmd == "load_roads" ) {
        // The overlay's <mods>/roads/<island>.json into the document (so an
        // existing roads file is editable; a fresh doc imports its island's
        // file at load anyway).
        editor.pushUndo();
        const int32_t imported = editor.importRoadsFile();
        editor.markSceneChanged();
        result = imported >= 0 ? ResultOk( std::to_string( imported ) + " roads imported" )
                               : ResultError( "no roads file for " + editor.island );
    } else if ( cmd == "bake_road" ) {
        editor.bakeRoadToProps( command.getString( "road", editor.activeRoadId ) );
        result = ResultOk( editor.statusText );
    } else {
        result = ResultError( "unknown command: " + cmd );
    }

    return Serialize( result );
}

void SceneEditorRunScript( GMSceneEditor &editor, const char *pPath ) {
    // The script may be an overlay path ("scenes/...") or a plain file path
    // (the AI writes it next to itself, not into the game data).
    std::vector<uint8_t> bytes;
    if ( !gpFile->loadFile( pPath, bytes ) ) {
        FILE *pFile = fopen( pPath, "rb" );
        if ( pFile == nullptr ) {
            OTDU_LOG_ERROR( "SceneEditor: script '%s' not found\n", pPath );
            return;
        }
        char buffer[ 256 * 1024 ];
        const size_t read = fread( buffer, 1, sizeof( buffer ) - 1, pFile );
        fclose( pFile );
        buffer[ read ] = 0;
        bytes.assign( buffer, buffer + read );
    }
    const std::string text( bytes.begin(), bytes.end() );
    std::string error;
    JsonValue root;
    if ( !JsonParse( text, root, error ) ) {
        OTDU_LOG_ERROR( "SceneEditor: script '%s': %s\n", pPath, error.c_str() );
        return;
    }
    std::vector<JsonValue> commands;
    if ( root.isArray() ) {
        commands = root.array;
    } else if ( root.isObject() ) {
        commands.push_back( root );
    } else {
        OTDU_LOG_ERROR( "SceneEditor: script '%s' is not a command array\n", pPath );
        return;
    }
    OTDU_LOG_INFO( "SceneEditor: running %zu scripted commands ('%s')\n", commands.size(), pPath );
    for ( const JsonValue &command : commands ) {
        const std::string text = JsonWrite( command );
        std::string cmdError;
        const std::string result = SceneEditorExecuteCommand( editor, text.c_str(), cmdError );
        OTDU_LOG_INFO( "SceneEditor: script cmd -> %s\n", result.c_str() );
    }
}

// --- the live server ----------------------------------------------------------

bool SceneCommandServer::start( int32_t port, GMSceneEditor *pOwner ) {
#ifdef _WIN32
    stop();
    pEditor = pOwner;
    serverPort = port;
    WSADATA data;
    if ( WSAStartup( MAKEWORD( 2, 2 ), &data ) != 0 ) {
        OTDU_LOG_ERROR( "SceneEditor: WSAStartup failed\n" );
        return false;
    }
    const SOCKET sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( sock == INVALID_SOCKET ) {
        OTDU_LOG_ERROR( "SceneEditor: server socket failed\n" );
        WSACleanup();
        return false;
    }
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl( INADDR_LOOPBACK ); // 127.0.0.1 only
    address.sin_port = htons( static_cast<uint16_t>( port ) );
    if ( bind( sock, reinterpret_cast<const sockaddr *>( &address ), sizeof( address ) ) != 0 ||
         listen( sock, 1 ) != 0 ) {
        OTDU_LOG_ERROR( "SceneEditor: server bind/listen on 127.0.0.1:%d failed\n", port );
        closesocket( sock );
        WSACleanup();
        return false;
    }
    listenSocket = reinterpret_cast<uintptr_t>( sock );
    bRun = true;
    thread = std::thread( [ this ]() { socketThread(); } );
    OTDU_LOG_INFO( "SceneEditor: command server on 127.0.0.1:%d (JSON lines; docs/studio_ai_api.md)\n", port );
    return true;
#else
    (void)port;
    (void)pOwner;
    return false;
#endif
}

void SceneCommandServer::socketThread() {
#ifdef _WIN32
    const SOCKET listenSock = static_cast<SOCKET>( listenSocket );
    while ( bRun ) {
        // One client at a time (the editor's driver).
        const SOCKET client = accept( listenSock, nullptr, nullptr );
        if ( client == INVALID_SOCKET || !bRun ) {
            break;
        }
        {
            std::lock_guard<std::mutex> lock( mutex );
            clientSocket = reinterpret_cast<uintptr_t>( client );
        }
        OTDU_LOG_INFO( "SceneEditor: command client connected\n" );
        std::string pending;
        while ( bRun ) {
            // Send everything queued while nothing is incoming.
            {
                std::lock_guard<std::mutex> lock( mutex );
                while ( !outbox.empty() ) {
                    const std::string &response = outbox.front();
                    int32_t sent = 0;
                    int32_t total = static_cast<int32_t>( response.size() );
                    const char *pData = response.data();
                    while ( sent < total ) {
                        const int32_t n = send( client, pData + sent, total - sent, 0 );
                        if ( n <= 0 ) {
                            break;
                        }
                        sent += n;
                    }
                    outbox.pop_front();
                }
            }
            // Receive (100 ms poll so the outbox keeps flowing).
            char buffer[ 4096 ];
            fd_set readSet;
            FD_ZERO( &readSet );
            FD_SET( client, &readSet );
            timeval timeout { 0, 100000 };
            const int32_t ready = select( 1, &readSet, nullptr, nullptr, &timeout );
            if ( ready <= 0 ) {
                continue;
            }
            const int32_t received = recv( client, buffer, sizeof( buffer ), 0 );
            if ( received <= 0 ) {
                break; // disconnected
            }
            pending.append( buffer, static_cast<size_t>( received ) );
            size_t newline = pending.find( '\n' );
            while ( newline != std::string::npos ) {
                std::string line = pending.substr( 0, newline );
                pending.erase( 0, newline + 1 );
                if ( !line.empty() && line.back() == '\r' ) {
                    line.pop_back();
                }
                if ( !line.empty() ) {
                    std::lock_guard<std::mutex> lock( mutex );
                    inbox.push_back( std::move( line ) );
                }
                newline = pending.find( '\n' );
            }
        }
        closesocket( client );
        std::lock_guard<std::mutex> lock( mutex );
        clientSocket = ~0ull;
        OTDU_LOG_INFO( "SceneEditor: command client disconnected\n" );
    }
#endif
}

void SceneCommandServer::tick() {
#ifdef _WIN32
    std::deque<std::string> commands;
    {
        std::lock_guard<std::mutex> lock( mutex );
        commands.swap( inbox );
    }
    if ( pEditor == nullptr ) {
        return;
    }
    for ( const std::string &text : commands ) {
        std::string error;
        const std::string result = SceneEditorExecuteCommand( *pEditor, text.c_str(), error );
        std::lock_guard<std::mutex> lock( mutex );
        outbox.push_back( result + "\n" ); // JSON lines: one response record
    }
#endif
}

void SceneCommandServer::stop() {
#ifdef _WIN32
    if ( !bRun ) {
        return;
    }
    bRun = false;
    if ( listenSocket != ~0ull ) {
        closesocket( static_cast<SOCKET>( listenSocket ) );
        listenSocket = ~0ull;
    }
    {
        std::lock_guard<std::mutex> lock( mutex );
        if ( clientSocket != ~0ull ) {
            closesocket( static_cast<SOCKET>( clientSocket ) );
            clientSocket = ~0ull;
        }
    }
    if ( thread.joinable() ) {
        thread.join();
    }
    WSACleanup();
#endif
}
