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
    const char *kinds[ 5 ] = { "library", "interior", "spot", "actor", "prop" };
    for ( int32_t i = 0; i < 5; i++ ) {
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
    if ( pObject->Kind == "interior" ) {
        ImGui::SameLine();
        if ( ImGui::Button( "walk inside (F6)" ) ) {
            editor.enterWalk( pObject->Id );
            bChanged = false;
        }
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
    if ( !ImGui::CollapsingHeader( "World pick / detach / recolor" ) ) {
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
        // The node's materials (M6): a recolor row per material (the live
        // swatch shows the override's color; the edit records a color
        // override — the same kind the commands write).
        if ( !hover.NodeMaterials.empty() ) {
            ImGui::TextDisabled( "materials of this node:" );
            for ( size_t i = 0; i < hover.NodeMaterials.size(); i++ ) {
                const std::string &name = hover.NodeMaterials[ i ];
                ImGui::PushID( static_cast<int>( i ) );
                // The current override's color (or white when none).
                const SceneOverride *pExisting = nullptr;
                for ( const SceneOverride &o : editor.doc.Overrides ) {
                    if ( o.Kind == "color" && StringCmpI( o.Target.c_str(), name.c_str() ) == 0 ) {
                        pExisting = &o;
                        break;
                    }
                }
                float rgb[ 3 ] = { pExisting != nullptr ? pExisting->Color.x : 1.0f,
                                   pExisting != nullptr ? pExisting->Color.y : 1.0f,
                                   pExisting != nullptr ? pExisting->Color.z : 1.0f };
                ImGui::SetNextItemWidth( 130.0f );
                if ( ImGui::ColorEdit3( "##mat", rgb, ImGuiColorEditFlags_NoInputs ) ) {
                    editor.addColorOverride( name, Vector4f( rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], 1.0f ) );
                }
                ImGui::SameLine();
                ImGui::TextUnformatted( name.c_str() );
                ImGui::PopID();
            }
        }
    } else {
        ImGui::TextDisabled( "click the world to inspect a node" );
    }
    if ( !editor.doc.Overrides.empty() ) {
        ImGui::Text( "%zu override(s):", editor.doc.Overrides.size() );
        for ( size_t i = 0; i < editor.doc.Overrides.size(); i++ ) {
            const SceneOverride &override = editor.doc.Overrides[ i ];
            ImGui::PushID( static_cast<int>( i ) );
            if ( override.Kind == "color" ) {
                float rgb[ 3 ] = { override.Color.x, override.Color.y, override.Color.z };
                ImGui::SetNextItemWidth( 130.0f );
                if ( ImGui::ColorEdit3( "##ovr", rgb, ImGuiColorEditFlags_NoInputs ) ) {
                    editor.addColorOverride( override.Target, Vector4f( rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], 1.0f ) );
                }
                ImGui::SameLine();
                ImGui::Text( "color '%s'", override.Target.c_str() );
            } else {
                ImGui::Text( "- %s '%s'", override.Kind.c_str(), override.Target.c_str() );
            }
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
    if ( ImGui::Button( "drive test here (starts FREERIDE)" ) ) {
        editor.statusText = editor.driveTest( "", true, "" );
    }
    if ( ImGui::Button( "commit prop placements -> props.txt" ) ) {
        editor.commitPropsToGame();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "export spots -> mods overlay" ) ) {
        editor.exportSpotsToGame();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "export instances -> sectors" ) ) {
        editor.exportPmiToGame();
    }
    ImGui::SameLine();
    static char bankName[ 64 ] = "composed";
    ImGui::InputText( "##bankname", bankName, sizeof( bankName ) );
    ImGui::SameLine();
    if ( ImGui::Button( "compile bank -> exterior/" ) ) {
        editor.exportBankToGame( bankName );
    }
    ImGui::TextDisabled( "props.txt is the route the current game build draws;" );
    ImGui::TextDisabled( "export spots writes a native spots.bin, export instances patches the" );
    ImGui::TextDisabled( "sector .pmi lists (FREERIDE streams both with no engine change)" );
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
            // The widgets edit the document in place: snapshot it first so ONE undo entry (taken at the first
            // change of a drag) restores the state before the edit.
            static bool bEditUndoHeld = false;
            const SceneDocument before = editor.doc;
            if ( ImGui::SliderFloat( "width", &road.Width, 3.0f, 30.0f, "%.1f m" ) ) {
                bChanged = true;
            }
            {
                static const char *kSurfaces[] = { "default", "tarmac", "dirt", "grass" };
                int32_t current = road.Surface == "tarmac" ? 1 : road.Surface == "dirt" ? 2 : road.Surface == "grass" ? 3 : 0;
                if ( ImGui::Combo( "surface", &current, kSurfaces, 4 ) ) {
                    road.Surface = current == 0 ? std::string() : std::string( kSurfaces[ current ] );
                    bChanged = true;
                }
                float bank = road.Banks.empty() ? 0.0f : road.Banks[ 0 ];
                if ( ImGui::SliderFloat( "bank (all points)", &bank, -30.0f, 30.0f, "%.1f deg" ) ) {
                    road.Banks.assign( road.Points.size(), bank );
                    bChanged = true;
                }
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
                if ( !bEditUndoHeld ) {
                    editor.pushUndoSnapshot( before );
                    bEditUndoHeld = true;
                }
                editor.markSceneChanged();
            }
            if ( !ImGui::IsMouseDown( 0 ) ) {
                bEditUndoHeld = false;
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
    // Petrol stations: a forecourt slab + a drive to the nearest retail road + a canopy, one click.
    ImGui::Separator();
    ImGui::TextUnformatted( "petrol stations" );
    if ( ImGui::Button( "new station at the camera focus" ) ) {
        const Vector3f focus = editor.position + editor.getForward() * 25.0f;
        float ground = focus.y;
        if ( editor.bWorldReady ) {
            editor.view.getWorld().getSurfaceHeight( focus.x, focus.z, editor.position.y + 50.0f, ground );
        }
        editor.pushUndo();
        SceneStation station;
        station.Id = editor.doc.uniqueId( "station" );
        station.Name = station.Id;
        station.Position = Vector3f( focus.x, ground, focus.z );
        station.Yaw = FLT_MAX; // auto: the slab points at the nearest road
        editor.doc.Stations.push_back( station );
        editor.statusText = station.Id + ": " + editor.rebuildStation( station.Id );
    }
    for ( size_t i = 0; i < editor.doc.Stations.size(); i++ ) {
        const SceneStation &station = editor.doc.Stations[ i ];
        ImGui::PushID( static_cast<int>( 5000 + i ) );
        ImGui::Text( "%s  (%.0f, %.0f) yaw %.0f", station.Id.c_str(), station.Position.x, station.Position.z, station.Yaw );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "rebuild" ) ) {
            const std::string id = station.Id;
            editor.pushUndo();
            editor.statusText = editor.rebuildStation( id );
        }
        ImGui::SameLine();
        if ( ImGui::SmallButton( "remove" ) ) {
            const std::string id = station.Id;
            editor.pushUndo();
            editor.removeStationParts( id );
            editor.doc.Stations.erase( editor.doc.Stations.begin() + i );
            editor.markSceneChanged();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if ( !editor.doc.Stations.empty() && ImGui::Button( "export stations (roads + stations.txt + canopy)" ) ) {
        editor.exportStationsToGame();
    }
}

} // namespace

void GMSceneEditor::drawUI() {
    drawSceneUI();
}

void GMSceneEditor::drawSceneUI() {
    // The world overlays (the gizmo, the selection box) draw FIRST (the
    // ImGui background list renders behind the panel windows, over the 3D
    // scene). This runs inside the ImGui frame (drawUI), which draw() (the
    // 3D pass) is NOT — a background-list command added in draw() is cleared
    // by the next NewFrame and never renders (found by the gizmo's missing
    // draw, M6).
    if ( bWorldReady && view.isReady() ) {
        drawGizmo();
        drawSelectionBox();
    }
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
    if ( isWalkActive() ) {
        ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.3f, 1.0f ), "WALK: %s", getWalkNote().c_str() );
        ImGui::TextDisabled( "WASD move, mouse look, Esc / F6 back to the editor" );
    } else {
        ImGui::TextDisabled( "RMB look, WSAD move, wheel speed; LMB pick / drag selection" );
        ImGui::TextDisabled( "Ctrl+Z / Ctrl+Y undo / redo, Ctrl+D duplicate (on the ground), Del delete, F2 panel, F3 gizmo, F4 move / rotate, F6 walk" );
    }
    ImGui::End();
}
