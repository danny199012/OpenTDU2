#include "shared.h"
#include "scene_editor_ui.h"

#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "game/scene_editor/gm_scene_editor.h"
#include "game/scene_editor/scene_doc.h"
#include "game/spots/mod_props.h"

#include "imgui.h"

#include <cmath>
#include <cstring>

// The scene editor panels (drawUI of GMSceneEditor, scene_editor_ui.cpp per
// the plan). One window, collapsible sections: scene, assets, outliner,
// properties, world-pick overrides, export. The WORLD_VIEWER panel is the
// style reference (DpiScaled, radio islands, list boxes).

namespace {
constexpr float kPi = 3.14159265f;

const char *IslandLabel( const char *pIsland ) {
    if ( strcmp( pIsland, "ibiza" ) == 0 ) {
        return "Ibiza";
    }
    if ( strcmp( pIsland, "hawai" ) == 0 ) {
        return "Hawai";
    }
    if ( strcmp( pIsland, "eden" ) == 0 ) {
        return "Eden";
    }
    if ( strcmp( pIsland, "hongkong" ) == 0 ) {
        return "Hong Kong";
    }
    return "Casino";
}

void DrawSceneSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Scene", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        return;
    }
    ImGui::TextUnformatted( "Island" );
    for ( const char *pIsland : GMSceneEditor::kIslands ) {
        ImGui::SameLine();
        if ( ImGui::RadioButton( IslandLabel( pIsland ), editor.island == pIsland ) && editor.island != pIsland ) {
            editor.pendingIsland = pIsland;
        }
    }
    static char sceneName[ 96 ] = {};
    if ( sceneName[ 0 ] == 0 && !editor.doc.Name.empty() ) {
        snprintf( sceneName, sizeof( sceneName ), "%s", editor.doc.Name.c_str() );
    }
    if ( ImGui::InputText( "name", sceneName, sizeof( sceneName ) ) ) {
        editor.doc.Name = sceneName;
    }
    if ( ImGui::Button( "new" ) ) {
        editor.pushUndo();
        editor.doc = SceneDocument();
        editor.doc.Island = editor.island;
        editor.doc.Name = sceneName;
        editor.selectedId.clear();
        editor.markSceneChanged();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "open..." ) ) {
        ImGui::OpenPopup( "open_scene" );
    }
    ImGui::SameLine();
    if ( ImGui::Button( "save" ) ) {
        const std::string file =
            ( editor.doc.Name.empty() ? std::string( "untitled" ) : editor.doc.Name ) + ".scene.json";
        std::string error;
        if ( editor.doc.saveToMods( file, error ) ) {
            editor.statusText = "saved " + file;
        } else {
            editor.statusText = error;
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "undo" ) ) {
        editor.undo();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "redo" ) ) {
        editor.redo();
    }
    if ( ImGui::BeginPopup( "open_scene" ) ) {
        for ( const std::string &file : ListSceneFiles() ) {
            if ( ImGui::Selectable( file.c_str() ) ) {
                std::string error;
                if ( editor.doc.loadFromVfs( file, error ) ) {
                    editor.selectedId.clear();
                    editor.applyOverrides();
                    editor.markSceneChanged();
                    editor.statusText = "opened " + file;
                    snprintf( sceneName, sizeof( sceneName ), "%s", editor.doc.Name.c_str() );
                } else {
                    editor.statusText = error;
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void DrawAssetsSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Assets", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        return;
    }
    if ( editor.bAssetsStale ) {
        editor.refreshAssets();
    }
    static char filter[ 64 ] = {};
    ImGui::SetNextItemWidth( 200.0f );
    ImGui::InputText( "search", filter, sizeof( filter ) );
    static int32_t kindTab = 0;
    const char *kinds[ 4 ] = { "library", "spot", "actor", "prop" };
    for ( int32_t i = 0; i < 4; i++ ) {
        if ( i > 0 ) {
            ImGui::SameLine();
        }
        if ( ImGui::RadioButton( kinds[ i ], kindTab == i ) ) {
            kindTab = i;
        }
    }
    // Counted lines: the list of the tab's assets matching the filter.
    std::vector<const GMSceneEditor::AssetEntry *> list;
    for ( const GMSceneEditor::AssetEntry &entry : editor.assets ) {
        if ( entry.Kind != kinds[ kindTab ] ) {
            continue;
        }
        if ( filter[ 0 ] != 0 && ToLower( entry.Name ).find( ToLower( std::string( filter ) ) ) == std::string::npos ) {
            continue;
        }
        list.push_back( &entry );
    }
    ImGui::Text( "%zu assets", list.size() );
    if ( ImGui::BeginListBox( "##assets", ImVec2( -1.0f, 180.0f ) ) ) {
        for ( const GMSceneEditor::AssetEntry *pEntry : list ) {
            if ( ImGui::Selectable( pEntry->Name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick ) &&
                 ImGui::IsMouseDoubleClicked( 0 ) ) {
                // Double click: place at the focus point (8 m ahead of the
                // camera, snapped to the surface).
                editor.placeAsset( pEntry->Kind, pEntry->Name, Vector3f(), true );
            }
        }
        ImGui::EndListBox();
    }
    ImGui::TextDisabled( "double-click: place 8 m ahead (snapped to ground)" );
}

void DrawOutlinerSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Outliner", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        return;
    }
    if ( editor.doc.Objects.empty() ) {
        ImGui::TextDisabled( "empty - add assets above" );
        return;
    }
    if ( ImGui::BeginListBox( "##outliner", ImVec2( -1.0f, 200.0f ) ) ) {
        for ( const SceneObject &object : editor.doc.Objects ) {
            char label[ 160 ];
            snprintf( label, sizeof( label ), "%s%s [%s] %s%s", object.Parent.empty() ? "" : "  + ",
                      object.Id.c_str(), object.Kind.c_str(), object.Ref.c_str(),
                      object.bVisible ? "" : "  (hidden)" );
            const bool bSelected = editor.selectedId == object.Id;
            if ( ImGui::Selectable( label, bSelected ) ) {
                editor.selectedId = object.Id;
            }
        }
        ImGui::EndListBox();
    }
}

void DrawPropertiesSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Properties", ImGuiTreeNodeFlags_DefaultOpen ) ) {
        return;
    }
    SceneObject *pObject = editor.doc.findObject( editor.selectedId );
    if ( pObject == nullptr ) {
        ImGui::TextDisabled( "nothing selected - click an object in the viewport" );
        return;
    }
    ImGui::Text( "%s [%s] '%s'", pObject->Id.c_str(), pObject->Kind.c_str(), pObject->Ref.c_str() );
    bool bChanged = false;
    if ( ImGui::InputFloat3( "position", &pObject->Position.x, "%.2f" ) ) {
        bChanged = true;
    }
    if ( ImGui::SliderFloat( "heading", &pObject->Yaw, -180.0f, 180.0f, "%.1f deg" ) ) {
        bChanged = true;
    }
    float scale = pObject->Scale.x;
    if ( ImGui::SliderFloat( "scale", &scale, 0.05f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic ) ) {
        pObject->Scale = Vector3f( scale, scale, scale );
        bChanged = true;
    }
    ImGui::Checkbox( "visible", &pObject->bVisible );
    bChanged |= ImGui::IsItemEdited();
    // Attach: the parent combo (groups are the parents).
    const char *pCurrentParent = pObject->Parent.empty() ? "(scene root)" : pObject->Parent.c_str();
    if ( ImGui::BeginCombo( "attach to", pCurrentParent ) ) {
        if ( ImGui::Selectable( "(scene root)", pObject->Parent.empty() ) ) {
            pObject->Parent.clear();
            bChanged = true;
        }
        for ( const SceneObject &candidate : editor.doc.Objects ) {
            if ( candidate.Kind != "group" || candidate.Id == pObject->Id ) {
                continue;
            }
            if ( ImGui::Selectable( candidate.Id.c_str(), candidate.Id == pObject->Parent ) ) {
                pObject->Parent = candidate.Id;
                bChanged = true;
            }
        }
        ImGui::EndCombo();
    }
    if ( ImGui::Button( "at camera" ) ) {
        pObject->Position = editor.position + editor.getForward() * 8.0f;
        bChanged = true;
    }
    ImGui::SameLine();
    if ( ImGui::Button( "snap ground" ) ) {
        editor.snapSelectedToGround();
        bChanged = false; // snap pushes its own undo
    }
    ImGui::SameLine();
    if ( ImGui::Button( "duplicate" ) ) {
        editor.duplicateSelected();
        bChanged = false;
    }
    ImGui::SameLine();
    ImGui::PushID( "delete" );
    if ( ImGui::Button( "delete" ) ) {
        editor.deleteSelected();
        ImGui::PopID();
        return;
    }
    ImGui::PopID();
    if ( bChanged ) {
        editor.markSceneChanged();
    }
}

void DrawWorldPickSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "World pick / detach" ) ) {
        return;
    }
    const GMSceneEditor::PickInfo &hover = editor.hover;
    if ( hover.bSceneObject ) {
        ImGui::Text( "pick: scene object '%s' (%.1f m)", hover.ObjectId.c_str(), hover.Distance );
    } else if ( hover.bWorld ) {
        ImGui::Text( "pick: world node '%s' (%.1f m)", hover.NodeName.c_str(), hover.Distance );
        ImGui::Text( "at %.1f %.1f %.1f", hover.Position.x, hover.Position.y, hover.Position.z );
        if ( !hover.NodeName.empty() && ImGui::Button( "hide this node (detach)" ) ) {
            editor.addOverride( "node", hover.NodeName );
        }
    } else {
        ImGui::TextDisabled( "click the world to inspect a node" );
    }
    if ( !editor.doc.Overrides.empty() ) {
        ImGui::Text( "%zu hidden:", editor.doc.Overrides.size() );
        for ( size_t i = 0; i < editor.doc.Overrides.size(); i++ ) {
            const SceneOverride &override = editor.doc.Overrides[ i ];
            ImGui::PushID( static_cast<int>( i ) );
            ImGui::Text( "- %s '%s'", override.Kind.c_str(), override.Target.c_str() );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "remove" ) ) {
                editor.pushUndo();
                editor.doc.Overrides.erase( editor.doc.Overrides.begin() + static_cast<ptrdiff_t>( i ) );
                editor.applyOverrides();
                ImGui::PopID();
                break; // the list changed
            }
            ImGui::PopID();
        }
    }
}

void DrawExportSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Export" ) ) {
        return;
    }
    if ( ImGui::Button( "commit prop placements -> props.txt" ) ) {
        editor.commitPropsToGame();
    }
    ImGui::TextDisabled( "props / library / spots draw in-game from the scene JSON (M3+);" );
    ImGui::TextDisabled( "props.txt is the route the current game build draws" );
}

void DrawRoadsSection( GMSceneEditor &editor ) {
    if ( !ImGui::CollapsingHeader( "Roads" ) ) {
        return;
    }
    if ( ImGui::Button( "new road" ) ) {
        std::string id = editor.doc.uniqueId( "road" );
        editor.pushUndo();
        SceneRoad road;
        road.Id = id;
        editor.doc.Roads.push_back( road );
        editor.activeRoadId = id;
        editor.markSceneChanged();
        editor.statusText = "road " + id + ": click the terrain to add points (see the commands)";
    }
    ImGui::TextDisabled( "the road tool is driven from the AI commands (add_road_point);" );
    ImGui::TextDisabled( "a point lands at the camera focus, snapped to the surface" );
    for ( size_t i = 0; i < editor.doc.Roads.size(); i++ ) {
        SceneRoad &road = editor.doc.Roads[ i ];
        ImGui::PushID( static_cast<int>( i ) );
        const bool bActive = editor.activeRoadId == road.Id;
        char label[ 96 ];
        snprintf( label, sizeof( label ), "%s%s (%zu pts)", bActive ? "> " : "", road.Id.c_str(), road.Points.size() );
        if ( ImGui::Selectable( label, bActive ) ) {
            editor.activeRoadId = road.Id;
        }
        if ( ImGui::BeginPopupContextItem( "road_ctx" ) ) {
            if ( ImGui::MenuItem( "delete road" ) ) {
                editor.pushUndo();
                editor.doc.Roads.erase( editor.doc.Roads.begin() + static_cast<ptrdiff_t>( i ) );
                if ( editor.activeRoadId == road.Id ) {
                    editor.activeRoadId.clear();
                }
                editor.markSceneChanged();
                ImGui::PopID();
                ImGui::EndPopup();
                break; // the list changed
            }
            ImGui::EndPopup();
        }
        if ( bActive ) {
            bool bChanged = false;
            if ( ImGui::SliderFloat( "width", &road.Width, 3.0f, 30.0f, "%.1f m" ) ) {
                bChanged = true;
            }
            if ( ImGui::SliderInt( "lanes", &road.Lanes, 1, 6 ) ) {
                bChanged = true;
            }
            if ( ImGui::Checkbox( "closed loop", &road.bClosed ) ) {
                bChanged = true;
            }
            ImGui::SameLine();
            if ( ImGui::Checkbox( "collide", &road.bCollide ) ) {
                bChanged = true;
            }
            if ( ImGui::Button( "add point at focus" ) ) {
                editor.pushUndo();
                float ground = 0.0f;
                Vector3f focus = editor.position + editor.getForward() * 8.0f;
                if ( editor.view.getWorld().getSurfaceHeight( focus.x, focus.z, editor.position.y + 50.0f, ground ) ) {
                    focus.y = ground;
                }
                road.Points.push_back( focus );
                editor.markSceneChanged();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "remove last point" ) && !road.Points.empty() ) {
                editor.pushUndo();
                road.Points.pop_back();
                editor.markSceneChanged();
            }
            if ( bChanged ) {
                editor.markSceneChanged();
            }
        }
        ImGui::PopID();
    }
    if ( !editor.doc.Roads.empty() ) {
        if ( ImGui::Button( "export roads file" ) ) {
            editor.exportRoadsFile();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "bake active road -> props" ) ) {
            editor.bakeRoadToProps( editor.activeRoadId );
        }
    }
}

} // namespace

void GMSceneEditor::drawUI() {
    drawSceneUI();
}

void GMSceneEditor::drawSceneUI() {
    if ( !bShowPanel ) {
        return;
    }
    ImGui::SetNextWindowPos( DpiScaled( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 430.0f, 0.0f ), ImGuiCond_FirstUseEver );
    if ( !ImGui::Begin( "Scene Editor" ) ) {
        ImGui::End();
        return;
    }
    DrawSceneSection( *this );
    ImGui::Separator();
    DrawAssetsSection( *this );
    ImGui::Separator();
    DrawOutlinerSection( *this );
    ImGui::Separator();
    DrawPropertiesSection( *this );
    ImGui::Separator();
    DrawWorldPickSection( *this );
    ImGui::Separator();
    DrawRoadsSection( *this );
    ImGui::Separator();
    DrawExportSection( *this );
    ImGui::Separator();

    // Camera + world status (the WORLD_VIEWER lines)
    ImGui::Text( "pos %.1f %.1f %.1f", position.x, position.y, position.z );
    ImGui::Text( "yaw %.1f pitch %.1f deg, speed %.0f m/s", yaw * 180.0f / kPi, pitch * 180.0f / kPi, currentSpeed );
    if ( bWorldReady ) {
        const EdenWorld &world = view.getWorld();
        const EdenWorld::SectorId id = EdenWorld::getSectorAt( position );
        ImGui::Text( "sector-%d-%d-%d-%d, %zu sectors loaded", id.AreaX, id.AreaY, id.X, id.Y, world.getNumSectors() );
    }
    ImGui::Checkbox( "snap ground", &bSnapGround );
    ImGui::SameLine();
    ImGui::SetNextItemWidth( 80.0f );
    ImGui::InputFloat( "grid", &gridSnap, 0.5f, 5.0f, "%.1f m" );
    ImGui::Checkbox( "spot exteriors", &bExteriors );
    if ( !statusText.empty() ) {
        ImGui::TextColored( ImVec4( 0.6f, 1.0f, 0.6f, 1.0f ), "%s", statusText.c_str() );
    }
    if ( !pendingIsland.empty() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "loading %s...", pendingIsland.c_str() );
    }
    if ( !statusText.empty() && statusText.size() > 0 ) {
        ImGui::SameLine();
    }
    ImGui::Separator();
    ImGui::TextDisabled( "RMB look, WSAD move, wheel speed; LMB pick / drag selection" );
    ImGui::TextDisabled( "Ctrl+Z / Ctrl+Y undo / redo, Ctrl+D duplicate, Del delete, F2 panel" );
    ImGui::End();
}
