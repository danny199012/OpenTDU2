#include "shared.h"
#include "gm_ui_viewer.h"

#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "flash/flash_definitions.h"
#include "flash/flash_movie.h"
#include "flash/flash_player.h"
#include "flash/gs_flash.h"
#include "game/ui_labels.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace {
    // The default movie: the pause menu and the options pages are the root
    // timeline of main.vmf, and `PauseMenu` (game/pause_menu.cpp) already drives
    // it by hand, so the tool starts on ground truth the port has verified.
    constexpr const char *kDefaultMovie = "main";
    // Twips per stage pixel: FlashPlayer::draw passes a 1/20 stage->canvas
    // matrix (flash_player.cpp draw), so a stage point in twips is 20x a stage
    // pixel. The hit test converts through this.
    constexpr float kTwipsPerStagePixel = 20.0f;
    // A display list is rebuilt every frame (the timeline moves objects); the
    // walk stops at this many nodes so a pathological movie cannot stall the
    // frame.
    constexpr size_t kMaxElements = 6000;
    // Sprites nesting deeper than this are not descended into (a display list
    // cannot really be this deep; the cap only guards the recursion).
    constexpr int32_t kMaxLevel = 24;

    const char *TypeName( eFlashCharacterType type ) {
        switch ( type ) {
        case eFlashCharacterType::CT_Shape: return "shape";
        case eFlashCharacterType::CT_Sprite: return "sprite";
        case eFlashCharacterType::CT_EditText: return "text";
        case eFlashCharacterType::CT_Button: return "button";
        case eFlashCharacterType::CT_Bitmap: return "bitmap";
        case eFlashCharacterType::CT_Font: return "font";
        default: return "?";
        }
    }

    // A rectangle in twips, unioned as the display list is walked.
    struct Bounds {
        float XMin = 0.0f, XMax = 0.0f, YMin = 0.0f, YMax = 0.0f;
        bool bAny = false;

        void add( float x0, float y0, float x1, float y1 ) {
            if ( !bAny ) {
                XMin = x0;
                XMax = x1;
                YMin = y0;
                YMax = y1;
                bAny = true;
                return;
            }
            XMin = Min( XMin, x0 );
            XMax = Max( XMax, x1 );
            YMin = Min( YMin, y0 );
            YMax = Max( YMax, y1 );
        }

        void addTransformed( const Bounds &other, const FlashMatrix &matrix ) {
            if ( !other.bAny ) {
                return;
            }
            float x = 0.0f, y = 0.0f;
            matrix.transform( other.XMin, other.YMin, x, y );
            add( x, y, x, y );
            matrix.transform( other.XMax, other.YMin, x, y );
            add( x, y, x, y );
            matrix.transform( other.XMin, other.YMax, x, y );
            add( x, y, x, y );
            matrix.transform( other.XMax, other.YMax, x, y );
            add( x, y, x, y );
        }
    };

    bool CharacterBounds( const FlashDefinitions &definitions,
                          uint16_t characterId,
                          const FlashPlayer::Clip *pClip,
                          Bounds &out,
                          int32_t level );

    // Bounds of everything a clip holds, in that clip's own space.
    bool ClipBounds( const FlashDefinitions &definitions,
                     const FlashPlayer::Clip &clip,
                     Bounds &out,
                     int32_t level ) {
        if ( level > kMaxLevel ) {
            return false;
        }
        for ( const auto &entry : clip.DisplayList ) {
            const FlashPlayer::Instance &instance = *entry.second;
            Bounds local;
            if ( !CharacterBounds( definitions, instance.CharacterId,
                                   instance.pClip.get(), local, level + 1 ) ) {
                continue;
            }
            out.addTransformed( local, instance.Matrix );
        }
        return out.bAny;
    }

    bool CharacterBounds( const FlashDefinitions &definitions,
                          uint16_t characterId,
                          const FlashPlayer::Clip *pClip,
                          Bounds &out,
                          int32_t level ) {
        switch ( definitions.getType( characterId ) ) {
        case eFlashCharacterType::CT_Shape: {
            const FlashShape *pShape = definitions.getShape( characterId );
            if ( pShape == nullptr ) {
                return false;
            }
            out.add( pShape->XMin, pShape->YMin, pShape->XMax, pShape->YMax );
            return out.bAny;
        }
        case eFlashCharacterType::CT_EditText: {
            const FlashEditText *pText = definitions.getEditText( characterId );
            if ( pText == nullptr ) {
                return false;
            }
            out.add( pText->XMin, pText->YMin, pText->XMax, pText->YMax );
            return out.bAny;
        }
        case eFlashCharacterType::CT_Sprite: {
            if ( pClip == nullptr ) {
                return false;
            }
            return ClipBounds( definitions, *pClip, out, level );
        }
        case eFlashCharacterType::CT_Button: {
            // Eden buttons are hit areas only: the hit record shapes carry the
            // bounds (flash_player.cpp sendMouseToClip does the same).
            const FlashButton *pButton = definitions.getButton( characterId );
            if ( pButton == nullptr ) {
                return false;
            }
            for ( const FlashButtonRecord &record : pButton->Records ) {
                if ( ( record.States & 0x08 ) == 0 ) {
                    continue;
                }
                const FlashShape *pShape = definitions.getShape( record.CharacterId );
                if ( pShape == nullptr ) {
                    continue;
                }
                Bounds shapeBounds;
                shapeBounds.add( pShape->XMin, pShape->YMin, pShape->XMax,
                                 pShape->YMax );
                out.addTransformed( shapeBounds, record.Matrix );
            }
            return out.bAny;
        }
        default:
            // Bitmaps and fonts are shape fills in this runtime, not display
            // list characters.
            return false;
        }
    }
} // namespace

GMUiViewer::GMUiViewer() : GameMode( eGameMode::GM_UiViewer ) {}

GMUiViewer::~GMUiViewer() { exit(); }

bool GMUiViewer::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.04f, 0.05f, 0.07f, 1.0f ) );

    bShowPanel = !CmdLine::HasArg( "-nopanel" );
    // Paused by default: a scripted run screenshots a still timeline.
    bPaused = !CmdLine::HasArg( "-play" );
    bDumpPending = CmdLine::HasArg( "-dumpui" );

    // -lintui: the label file against the movies, then out (a test hook: the
    // verdict is in the log, like -labellint of the asset viewer).
    if ( CmdLine::HasArg( "-lintui" ) ) {
        lintLabels();
        pInstance->requestExit();
        return true;
    }

    scanMovies();

    std::string start = kDefaultMovie;
    if ( const char *pMovie = CmdLine::GetArgValue( "-movie" ) ) {
        start = pMovie;
    }
    if ( !openMovie( start ) ) {
        for ( const std::string &candidate : movieNames ) {
            if ( openMovie( candidate ) ) {
                break;
            }
        }
    }

    // -clip <path>: select an element before the first frame (so a screenshot
    // shows its panel).
    if ( const char *pClip = CmdLine::GetArgValue( "-clip" ) ) {
        selectedPath = NormalizeUiClip( pClip );
        bRebuild = true;
    }
    // -goto "<clip>:<label>[;<clip>:<label>...]": jump clips to timeline
    // labels before the screenshot (a menu state only a scripted run reaches).
    // Deferred to the first settled frame and applied in order, because the
    // nested clips do not exist before then:
    //   -goto "/:pause_stop;/pause_menu/menu_sq:stop"
    if ( const char *pGoto = CmdLine::GetArgValue( "-goto" ) ) {
        std::string list = pGoto;
        size_t at = 0;
        while ( at <= list.size() ) {
            const size_t semicolon = list.find( ';', at );
            const std::string spec =
                semicolon == std::string::npos
                    ? list.substr( at )
                    : list.substr( at, semicolon - at );
            const size_t colon = spec.find_last_of( ':' );
            if ( !spec.empty() && colon != std::string::npos ) {
                pendingGotos.emplace_back( spec.substr( 0, colon ),
                                           spec.substr( colon + 1 ) );
            }
            if ( semicolon == std::string::npos ) {
                break;
            }
            at = semicolon + 1;
        }
        if ( pendingGotos.empty() ) {
            setStatus( "-goto wants <clip>:<label>[;<clip>:<label>]" );
        }
    }
    // -label <text> / -note <text>: fill the editor (with -clip, a scripted
    // label write).
    if ( const char *pLabel = CmdLine::GetArgValue( "-label" ) ) {
        snprintf( labelText, sizeof( labelText ), "%s", pLabel );
    }
    if ( const char *pNote = CmdLine::GetArgValue( "-note" ) ) {
        snprintf( noteText, sizeof( noteText ), "%s", pNote );
    }
    return true;
}

void GMUiViewer::exit() {
    unloadMovie();
    elements.clear();
    selected = -1;
    selectedPath.clear();
}

void GMUiViewer::scanMovies() {
    movieNames.clear();
    if ( gpFile == nullptr ) {
        return;
    }
    const auto hires = gpFile->resolvePath( "Euro/Bnk/FrontEnd/HiRes" );
    if ( !hires.has_value() ) {
        OTDU_LOG_WARN( "UiViewer: no Euro/Bnk/FrontEnd/HiRes under the game "
                       "root\n" );
        return;
    }
    std::error_code ec;
    for ( std::filesystem::directory_iterator it( *hires, ec ), end;
          it != end; it.increment( ec ) ) {
        if ( ec ) {
            break;
        }
        std::error_code fileEc;
        if ( !it->is_regular_file( fileEc ) || fileEc ) {
            continue;
        }
        if ( ToLower( it->path().extension().string() ) != ".bnk" ) {
            continue;
        }
        movieNames.push_back( it->path().stem().string() );
    }
    std::sort( movieNames.begin(), movieNames.end() );
    OTDU_LOG_INFO( "UiViewer: %zu frontend movie(s) under '%s'\n",
                   movieNames.size(), hires->string().c_str() );
}

bool GMUiViewer::openMovie( const std::string &name ) {
    unloadMovie();
    if ( gpFile == nullptr ) {
        return false;
    }
    // The name may be a bank stem ("main"), a movie ("main.vmf") or a bank
    // path; the game data keys on the stem of FrontEnd/HiRes/<stem>.bnk.
    std::string stem = NormalizeUiMovie( name.c_str() );
    if ( stem.size() > 4 ) {
        stem = stem.substr( 0, stem.size() - 4 );
    }
    if ( stem.empty() ) {
        return false;
    }

    pMovie = std::make_unique<FlashMovie>();
    if ( !pMovie->load( stem.c_str(), "HiRes" ) ) {
        pMovie.reset();
        setStatus( "cannot load FrontEnd/HiRes/" + stem + ".bnk" );
        return false;
    }
    pPlayer = std::make_unique<FlashPlayer>();
    // -dumpbuttons: the movie's buttons + action strings (its FSCommands).
    if ( CmdLine::HasArg( "-dumpbuttons" ) ) {
        DumpMovieButtons( pMovie.get() );
    }
    if ( !pPlayer->load( pMovie.get() ) ) {
        unloadMovie();
        setStatus( "cannot start the player of '" + stem + "'" );
        return false;
    }
    movieKey = NormalizeUiMovie( stem.c_str() );
    const VMFFile &vmf = pMovie->getVMF();
    stageWidth = vmf.getStageWidth();
    stageHeight = vmf.getStageHeight();
    char info[320] = {};
    snprintf( info, sizeof( info ),
              "%s: %.0fx%.0f stage (px, 1 px = 20 twips), %u frames @ %.0f fps, "
              "%zu tag(s), %zu bitmap(s)",
              movieKey.c_str(), stageWidth, stageHeight, vmf.getFrameCount(),
              vmf.getFrameRate(), vmf.getTags().size(),
              vmf.getBitmaps().size() );
    movieInfo = info;
    OTDU_LOG_INFO( "UiViewer: %s\n", movieInfo.c_str() );

    for ( int32_t i = 0; i < static_cast<int32_t>( movieNames.size() ); i++ ) {
        if ( StringCmpI( movieNames[i].c_str(), stem.c_str() ) == 0 ) {
            selectedMovie = i;
            break;
        }
    }
    selected = -1;
    selectedPath.clear();
    bRebuild = true;
    framesSinceOpen = 0;
    return true;
}

void GMUiViewer::unloadMovie() {
    // The player first (it holds clips of the movie, the movie holds the
    // textures), the same order as the asset viewer's movie preview.
    pPlayer.reset();
    pMovie.reset();
    movieKey.clear();
    movieInfo.clear();
    elements.clear();
    stageWidth = 0.0f;
    stageHeight = 0.0f;
}

void GMUiViewer::setStatus( const std::string &text ) {
    status = text;
    OTDU_LOG_INFO( "UiViewer: %s\n", text.c_str() );
}

void GMUiViewer::rebuildElements() {
    elements.clear();
    bRebuild = false;
    if ( pPlayer == nullptr || pPlayer->getRoot() == nullptr ) {
        return;
    }
    const FlashDefinitions &definitions = pPlayer->getDefinitions();

    // The labelled clip paths of this movie, once: the walk marks each node
    // that already has a name in docs/re/ui_elements.txt.
    std::set<std::string> labelled;
    for ( const UiElementLabel &label : GetUiElementLabels() ) {
        if ( label.Movie == movieKey ) {
            labelled.insert( label.Clip );
        }
    }

    // The selection survives a rebuild by path: the display list is rebuilt
    // every frame as the timeline places and removes objects.
    const std::string wanted = selectedPath;

    // The root timeline is an element too: its own labels are the movie's
    // states (main.vmf's pause_in / pause_stop / pause_back / pause_out), which
    // is the first thing a native rebuild has to model.
    {
        const FlashPlayer::Clip *pRoot = pPlayer->getRoot();
        Element rootElement;
        rootElement.Path = "/";
        rootElement.Name = "/";
        rootElement.Level = 0;
        rootElement.pType = "root";
        rootElement.bVisible = true;
        rootElement.bHasClip = true;
        rootElement.Frame = pRoot->CurrentFrame;
        rootElement.FrameCount = pRoot->pTimeline != nullptr
                                     ? pRoot->pTimeline->FrameCount
                                     : 0;
        rootElement.bLabelled = labelled.count( "/" ) > 0;
        elements.push_back( rootElement );
    }

    std::function<void( const FlashPlayer::Clip &, const FlashMatrix &,
                        const std::string &, int32_t )>
        walk;
    walk = [&]( const FlashPlayer::Clip &clip, const FlashMatrix &parentMatrix,
                const std::string &parentPath, int32_t level ) {
        if ( level > kMaxLevel ) {
            return;
        }
        for ( const auto &entry : clip.DisplayList ) {
            if ( elements.size() >= kMaxElements ) {
                return;
            }
            const FlashPlayer::Instance &instance = *entry.second;
            // FlashPlayer's own composition rule (drawClip:
            // matrix * instance.Matrix).
            const FlashMatrix world = parentMatrix * instance.Matrix;

            std::string name = instance.Name;
            if ( name.empty() ) {
                char unnamed[24] = {};
                snprintf( unnamed, sizeof( unnamed ), "#%u",
                          static_cast<uint32_t>( instance.Depth ) );
                name = unnamed;
            }
            const std::string path =
                parentPath == "/" ? "/" + name : parentPath + "/" + name;

            Element element;
            element.Path = path;
            element.Name = name;
            element.Level = level;
            element.Depth = instance.Depth;
            element.CharacterId = instance.CharacterId;
            element.pType = TypeName( definitions.getType( instance.CharacterId ) );
            element.bVisible = instance.bVisible;
            element.bHasClip = instance.pClip != nullptr;
            if ( instance.pClip != nullptr ) {
                element.Frame = instance.pClip->CurrentFrame;
                element.FrameCount = instance.pClip->pTimeline != nullptr
                                         ? instance.pClip->pTimeline->FrameCount
                                         : 0;
            }
            Bounds bounds;
            if ( CharacterBounds( definitions, instance.CharacterId,
                                  instance.pClip.get(), bounds, level + 1 ) ) {
                element.bHasBounds = true;
                element.XMin = bounds.XMin;
                element.XMax = bounds.XMax;
                element.YMin = bounds.YMin;
                element.YMax = bounds.YMax;
            }
            element.bLabelled = labelled.count( path ) > 0;
            element.WorldA = world.A;
            element.WorldB = world.B;
            element.WorldC = world.C;
            element.WorldD = world.D;
            element.WorldTX = world.TX;
            element.WorldTY = world.TY;
            elements.push_back( element );

            if ( instance.pClip != nullptr ) {
                walk( *instance.pClip, world, path, level + 1 );
            }
        }
    };
    walk( *pPlayer->getRoot(), FlashMatrix(), "/", 0 );

    selected = -1;
    if ( !wanted.empty() ) {
        for ( int32_t i = 0; i < static_cast<int32_t>( elements.size() ); i++ ) {
            if ( StringCmpI( elements[i].Path.c_str(), wanted.c_str() ) == 0 ) {
                selected = i;
                break;
            }
        }
    }
}

void GMUiViewer::selectElement( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( elements.size() ) ) {
        selected = -1;
        selectedPath.clear();
        editorPath.clear();
        labelText[0] = '\0';
        noteText[0] = '\0';
        return;
    }
    selected = index;
    selectedPath = elements[index].Path;
    editorPath = selectedPath;
    const UiElementLabel *pLabel =
        FindUiElementLabel( movieKey.c_str(), selectedPath.c_str() );
    snprintf( labelText, sizeof( labelText ), "%s",
              pLabel != nullptr ? pLabel->Label.c_str() : "" );
    snprintf( noteText, sizeof( noteText ), "%s",
              pLabel != nullptr ? pLabel->Note.c_str() : "" );
    OTDU_LOG_INFO( "UiViewer: selected '%s' (%s, character %u, depth %u, "
                   "frame %u/%u, %s, bounds %s)\n",
                   selectedPath.c_str(), elements[index].pType,
                   static_cast<uint32_t>( elements[index].CharacterId ),
                   static_cast<uint32_t>( elements[index].Depth ),
                   elements[index].Frame, elements[index].FrameCount,
                   elements[index].bVisible ? "visible" : "hidden",
                   elements[index].bHasBounds ? "known" : "none" );
}

int32_t GMUiViewer::hitTest( float u, float v ) {
    if ( pPlayer == nullptr || pPlayer->getRoot() == nullptr ||
         stageWidth <= 0.0f || stageHeight <= 0.0f ) {
        return -1;
    }
    // The clicked point in the root clip's space. The stage maps onto the
    // panel 1:1 in normalized terms, and one stage pixel is 20 twips.
    const float rootX = u * stageWidth * kTwipsPerStagePixel;
    const float rootY = v * stageHeight * kTwipsPerStagePixel;
    const FlashDefinitions &definitions = pPlayer->getDefinitions();

    // Deeper wins (the more specific element); at one level the first tested
    // wins, and the lists are walked front to back.
    std::string bestPath;
    int32_t bestLevel = -1;

    std::function<void( const FlashPlayer::Clip &, const FlashMatrix &,
                        const std::string &, int32_t )>
        walk;
    walk = [&]( const FlashPlayer::Clip &clip, const FlashMatrix &parentMatrix,
                const std::string &parentPath, int32_t level ) {
        if ( level > kMaxLevel ) {
            return;
        }
        for ( auto it = clip.DisplayList.rbegin(); it != clip.DisplayList.rend();
              ++it ) {
            const FlashPlayer::Instance &instance = *it->second;
            if ( !instance.bVisible ) {
                continue;
            }
            const FlashMatrix world = parentMatrix * instance.Matrix;
            std::string name = instance.Name;
            if ( name.empty() ) {
                char unnamed[24] = {};
                snprintf( unnamed, sizeof( unnamed ), "#%u",
                          static_cast<uint32_t>( instance.Depth ) );
                name = unnamed;
            }
            const std::string path =
                parentPath == "/" ? "/" + name : parentPath + "/" + name;
            Bounds bounds;
            if ( CharacterBounds( definitions, instance.CharacterId,
                                  instance.pClip.get(), bounds, level + 1 ) ) {
                float x = 0.0f, y = 0.0f;
                world.inverse().transform( rootX, rootY, x, y );
                if ( x >= bounds.XMin && x <= bounds.XMax && y >= bounds.YMin &&
                     y <= bounds.YMax && level > bestLevel ) {
                    bestLevel = level;
                    bestPath = path;
                }
            }
            if ( instance.pClip != nullptr ) {
                walk( *instance.pClip, world, path, level + 1 );
            }
        }
    };
    walk( *pPlayer->getRoot(), FlashMatrix(), "/", 0 );

    if ( bestPath.empty() ) {
        return -1;
    }
    for ( int32_t i = 0; i < static_cast<int32_t>( elements.size() ); i++ ) {
        if ( elements[i].Path == bestPath ) {
            return i;
        }
    }
    return -1;
}

bool GMUiViewer::goToLabel( const char *pClipPath, const char *pLabel ) {
    if ( pPlayer == nullptr || pClipPath == nullptr || pLabel == nullptr ) {
        return false;
    }
    // The clip path keeps its authored case (only the separators are fixed):
    // it is what findClip / gotoLabel resolve against the movie.
    std::string path = pClipPath;
    for ( char &c : path ) {
        if ( c == '\\' ) {
            c = '/';
        }
    }
    if ( path.empty() || path[0] != '/' ) {
        path.insert( path.begin(), '/' );
    }
    if ( !pPlayer->gotoLabel( path, pLabel, false ) ) {
        // Quiet: the -goto retry loop calls this every frame until the clip
        // exists, and the give-up is reported there.
        OTDU_LOG_DEBUG( "UiViewer: no label '%s' on '%s' (yet)\n", pLabel,
                        path.c_str() );
        return false;
    }
    bRebuild = true;
    setStatus( "jumped '" + path + "' to '" + pLabel + "'" );
    return true;
}

void GMUiViewer::saveLabel() {
    if ( selected < 0 || selected >= static_cast<int32_t>( elements.size() ) ) {
        setStatus( "no element selected" );
        return;
    }
    const std::string label = labelText;
    const std::string note = noteText;
    if ( !SetUiElementLabel( movieKey.c_str(), elements[selected].Path.c_str(),
                             label, note ) ) {
        setStatus( "label unchanged" );
        return;
    }
    if ( !SaveUiElementLabels() ) {
        setStatus( "cannot write " + GetUiElementLabelPath() );
        return;
    }
    bRebuild = true;
    setStatus( label.empty() && note.empty() ? "label removed" : "label saved" );
}

void GMUiViewer::dumpElements() {
    if ( elements.empty() ) {
        setStatus( "nothing to dump" );
        return;
    }
    OTDU_LOG_INFO( "UiViewer: --- %s: %zu element(s) ---\n", movieKey.c_str(),
                   elements.size() );
    OTDU_LOG_INFO( "UI\tmovie\tpath\ttype\tchar\tframe\tstate\tbounds\n" );
    for ( const Element &element : elements ) {
        char bounds[64] = { '-' };
        if ( element.bHasBounds ) {
            snprintf( bounds, sizeof( bounds ), "%.0f,%.0f,%.0f,%.0f",
                      element.XMin, element.YMin, element.XMax, element.YMax );
        }
        char frame[32] = {};
        snprintf( frame, sizeof( frame ), "%u/%u", element.Frame,
                  element.FrameCount );
        OTDU_LOG_INFO( "UI\t%s\t%s\t%s\t%u\t%s\t%s\t%s\n", movieKey.c_str(),
                       element.Path.c_str(), element.pType,
                       static_cast<uint32_t>( element.CharacterId ), frame,
                       element.bVisible ? "vis" : "hid", bounds );
    }
    // The timeline labels of every clip that has some: the states an element
    // can be forced to (-goto), i.e. what a native rebuild has to model.
    for ( const Element &element : elements ) {
        if ( !element.bHasClip || pPlayer == nullptr ) {
            continue;
        }
        const FlashPlayer::Clip *pClip = pPlayer->findClip( element.Path );
        if ( pClip == nullptr || pClip->pTimeline == nullptr ||
             pClip->pTimeline->Labels.empty() ) {
            continue;
        }
        std::string list;
        for ( const auto &label : pClip->pTimeline->Labels ) {
            if ( !list.empty() ) {
                list += ", ";
            }
            list += label.first;
        }
        OTDU_LOG_INFO( "UILabel\t%s\t%s\t%s\n", movieKey.c_str(),
                       element.Path.c_str(), list.c_str() );
    }
    setStatus( "dumped " + std::to_string( elements.size() ) + " element(s)" );
}

int32_t GMUiViewer::lintLabels() {
    // Every key must be a display-list path of the movie it names. The list is
    // walked with the timeline paused at the frame it opens on, which covers
    // the static frontend screens the labels describe; an element that only
    // exists on a later frame is reported, and the fix is to reach that state
    // once with -goto / -play before labelling it.
    int32_t problems = 0;
    std::set<std::string> movies;
    for ( const UiElementLabel &label : GetUiElementLabels() ) {
        movies.insert( label.Movie );
    }
    size_t checked = 0;
    for ( const std::string &movie : movies ) {
        if ( !openMovie( movie ) ) {
            OTDU_LOG_WARN( "UI lint: cannot load movie '%s'\n", movie.c_str() );
            problems++;
            continue;
        }
        // Settle the movie exactly like tick() does: a frontend display list is
        // built by the actions of the first frames, so walking it straight
        // after load finds nothing at all.
        const float frameTime =
            1.0f / Max( 1.0f, pMovie->getVMF().getFrameRate() );
        for ( int32_t i = 0; i < 30; i++ ) {
            pPlayer->tick( frameTime );
        }
        rebuildElements();
        std::set<std::string> paths;
        const auto collect = [&paths, this]() {
            for ( const Element &element : elements ) {
                paths.insert( ToLower( element.Path ) );
            }
        };
        collect();
        // The pause menu and the options pages only exist in the movie's own
        // states, so every label of the root timeline is walked as well: a key
        // is then checked against every reachable screen, not only the one the
        // movie opens on.
        const FlashPlayer::Clip *pRootClip = pPlayer->getRoot();
        if ( pRootClip != nullptr && pRootClip->pTimeline != nullptr ) {
            std::vector<std::string> rootLabels;
            for ( const auto &label : pRootClip->pTimeline->Labels ) {
                rootLabels.push_back( label.first );
            }
            for ( const std::string &label : rootLabels ) {
                if ( !pPlayer->gotoLabel( "/", label, false ) ) {
                    continue;
                }
                for ( int32_t i = 0; i < 10; i++ ) {
                    pPlayer->tick( frameTime );
                }
                rebuildElements();
                collect();
            }
        }
        for ( const UiElementLabel &label : GetUiElementLabels() ) {
            if ( label.Movie != movie ) {
                continue;
            }
            checked++;
            if ( paths.count( label.Clip ) == 0 ) {
                OTDU_LOG_WARN( "UI lint: '%s' is not in '%s' at frame 0 ('%s')\n",
                               label.Clip.c_str(), movie.c_str(),
                               label.Label.c_str() );
                problems++;
            }
        }
    }
    OTDU_LOG_INFO( "UI lint: %zu label(s) over %zu movie(s), %d problem(s)%s\n",
                   checked, movies.size(), problems,
                   problems == 0 ? " - every key is a display list path" : "" );
    unloadMovie();
    return problems;
}

void GMUiViewer::tick( float deltaTime ) {
    if ( pPlayer == nullptr ) {
        return;
    }
    // A movie builds its UI from the actions of its first frames, so those
    // always run: a "paused" viewer shows a settled screen, not frame 0.
    constexpr int32_t kSettleFrames = 30;
    const float frameTime =
        pMovie != nullptr
            ? 1.0f / Max( 1.0f, pMovie->getVMF().getFrameRate() )
            : 1.0f / 30.0f;
    if ( framesSinceOpen < kSettleFrames ) {
        framesSinceOpen++;
        pPlayer->tick( frameTime );
    } else if ( !bPaused ) {
        pPlayer->tick( deltaTime * playSpeed );
    } else {
        // Zero time still drains the actions a -goto / label click queued,
        // while the timeline itself stays where it is.
        pPlayer->tick( 0.0f );
    }

    // The -goto list: applied once the movie has settled, retried for a few
    // frames per entry (a child clip appears a frame after its parent's label).
    if ( framesSinceOpen >= kSettleFrames && !pendingGotos.empty() ) {
        const std::pair<std::string, std::string> &step = pendingGotos.front();
        if ( goToLabel( step.first.c_str(), step.second.c_str() ) ) {
            pendingGotos.erase( pendingGotos.begin() );
            gotoTries = 0;
        } else if ( ++gotoTries > 90 ) {
            setStatus( "cannot reach -goto '" + step.first + ":" +
                       step.second + "'" );
            pendingGotos.erase( pendingGotos.begin() );
            gotoTries = 0;
        }
    }

    rebuildElements();

    // A selection can arrive without a click (-clip, or the selection kept
    // across a rebuild): load the label editor for it once it exists.
    if ( selected >= 0 &&
         selected < static_cast<int32_t>( elements.size() ) &&
         elements[selected].Path != editorPath ) {
        selectElement( selected );
    }

    if ( bDumpPending && framesSinceOpen >= kSettleFrames ) {
        bDumpPending = false;
        dumpElements();
        if ( pGameInstance != nullptr ) {
            pGameInstance->requestExit();
        }
    }
}

void GMUiViewer::draw() {
    if ( pPlayer == nullptr ) {
        return;
    }
    if ( bShowPanel ) {
        // The panels report their layout in drawUI; the 2D layer draws before
        // them, so it uses the rect of the last frame.
        if ( !bMovieRectValid ) {
            return;
        }
        pPlayer->setStageRect( movieRect );
    } else {
        // -nopanel: the movie alone, fitted to the screen with its own aspect.
        const float backbufferWidth = static_cast<float>(
            gpRender->getDevice().getBackbufferWidth() );
        const float backbufferHeight = static_cast<float>(
            gpRender->getDevice().getBackbufferHeight() );
        const float stageAspect = stageWidth / Max( 1.0f, stageHeight );
        const float screenAspect =
            backbufferWidth / Max( 1.0f, backbufferHeight );
        float rect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        if ( screenAspect > stageAspect ) {
            rect[2] = stageAspect / Max( 0.001f, screenAspect );
            rect[0] = ( 1.0f - rect[2] ) * 0.5f;
        } else {
            rect[3] = screenAspect / Max( 0.001f, stageAspect );
            rect[1] = ( 1.0f - rect[3] ) * 0.5f;
        }
        pPlayer->setStageRect( rect );
    }
    pPlayer->draw( gpRender->get2DRenderer() );
}

void GMUiViewer::drawUI() {
    if ( !bShowPanel ) {
        return;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = DpiScale();

    // ---- Movies (left, top) ----
    ImGui::SetNextWindowPos( DpiScaled( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 320.0f, 300.0f ),
                              ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "UI Viewer - movies" ) ) {
        ImGui::TextWrapped( "labels: %s", GetUiElementLabelPath().c_str() );
        ImGui::Text( "%zu movie(s), %zu label(s)", movieNames.size(),
                     GetUiElementLabels().size() );
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##moviefilter", "filter movies", movieFilter,
                                  sizeof( movieFilter ) );
        ImGui::BeginChild( "##movielist" );
        const std::string movieFilterLower = ToLower( movieFilter );
        for ( int32_t i = 0;
              i < static_cast<int32_t>( movieNames.size() ); i++ ) {
            if ( !movieFilterLower.empty() &&
                 ToLower( movieNames[i] ).find( movieFilterLower ) ==
                     std::string::npos ) {
                continue;
            }
            if ( ImGui::Selectable( movieNames[i].c_str(), i == selectedMovie ) ) {
                openMovie( movieNames[i] );
            }
        }
        ImGui::EndChild();
        if ( !movieInfo.empty() ) {
            ImGui::Separator();
            ImGui::TextWrapped( "%s", movieInfo.c_str() );
        }
        if ( !status.empty() ) {
            ImGui::TextDisabled( "%s", status.c_str() );
        }
    }
    ImGui::End();

    // ---- The display list (left, bottom) ----
    // Clamped to the display: on a HiDPI setup (DpiScale ~2) a fixed 320 pt
    // column would push this window off a 720 px tall backbuffer.
    const float listY = Min( 320.0f * scale, display.y * 0.45f );
    ImGui::SetNextWindowPos( ImVec2( 10.0f * scale, listY ),
                             ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize(
        ImVec2( 320.0f * scale,
                Max( 120.0f * scale, display.y - listY - 10.0f * scale ) ),
        ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "UI Viewer - display list" ) ) {
        bool bPlay = !bPaused;
        if ( ImGui::Checkbox( "play", &bPlay ) ) {
            bPaused = !bPlay;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "step" ) && pPlayer != nullptr ) {
            pPlayer->tick( 1.0f / 30.0f );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "dump" ) ) {
            dumpElements();
        }
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##clipfilter", "filter elements", clipFilter,
                                  sizeof( clipFilter ) );
        ImGui::Text( "%zu element(s)%s", elements.size(),
                     elements.size() >= kMaxElements ? " (capped)" : "" );

        const std::string clipFilterLower = ToLower( clipFilter );
        numDrawnRows = 0;
        ImGui::BeginChild( "##elementlist" );
        for ( int32_t i = 0; i < static_cast<int32_t>( elements.size() ); i++ ) {
            const Element &element = elements[i];
            if ( !clipFilterLower.empty() ) {
                const std::string hay =
                    ToLower( element.Path + " " + element.pType );
                if ( hay.find( clipFilterLower ) == std::string::npos ) {
                    continue;
                }
            }
            char text[600] = {};
            snprintf( text, sizeof( text ), "%*s%s%s",
                      Min( element.Level * 2, 40 ), "", element.Name.c_str(),
                      element.bLabelled ? "  *" : "" );
            if ( ImGui::Selectable( text, i == selected ) ) {
                selectElement( i );
            }
            if ( ImGui::IsItemHovered() ) {
                ImGui::SetTooltip(
                    "%s\n%s, character %u, depth %u, frame %u/%u\n%s",
                    element.Path.c_str(), element.pType,
                    static_cast<uint32_t>( element.CharacterId ),
                    static_cast<uint32_t>( element.Depth ), element.Frame,
                    element.FrameCount,
                    element.bVisible ? "visible" : "hidden" );
            }
            numDrawnRows++;
        }
        ImGui::EndChild();
        ImGui::TextDisabled( "%u row(s) shown", numDrawnRows );
    }
    ImGui::End();

    // ---- The element + the movie (right) ----
    ImGui::SetNextWindowPos( DpiScaled( 340.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize(
        ImVec2( Max( 240.0f * scale, display.x - 350.0f * scale ),
                Max( 200.0f * scale, display.y - 20.0f * scale ) ),
        ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "UI Viewer - movie" ) ) {
        if ( selected >= 0 &&
             selected < static_cast<int32_t>( elements.size() ) ) {
            const Element &element = elements[selected];
            ImGui::TextUnformatted( element.Path.c_str() );
            ImGui::TextDisabled(
                "%s, character %u, depth %u, frame %u/%u, %s", element.pType,
                static_cast<uint32_t>( element.CharacterId ),
                static_cast<uint32_t>( element.Depth ), element.Frame,
                element.FrameCount,
                element.bVisible ? "visible" : "hidden" );
            // The timeline labels of the element's own clip: the states -goto
            // and these buttons jump to.
            if ( element.bHasClip && pPlayer != nullptr ) {
                const FlashPlayer::Clip *pClip = pPlayer->findClip( element.Path );
                if ( pClip != nullptr && pClip->pTimeline != nullptr &&
                     !pClip->pTimeline->Labels.empty() ) {
                    ImGui::TextUnformatted( "labels:" );
                    for ( const auto &label : pClip->pTimeline->Labels ) {
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( label.first.c_str() ) ) {
                            goToLabel( element.Path.c_str(), label.first.c_str() );
                        }
                    }
                }
            }
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputTextWithHint( "##label", "label: what this element is",
                                      labelText, sizeof( labelText ) );
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputTextWithHint( "##note", "note", noteText,
                                      sizeof( noteText ) );
            if ( ImGui::Button( "Save label" ) ) {
                saveLabel();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Clear" ) ) {
                labelText[0] = '\0';
                noteText[0] = '\0';
                saveLabel();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Copy path" ) ) {
                ImGui::SetClipboardText( element.Path.c_str() );
            }
            ImGui::Separator();
        } else if ( pPlayer != nullptr ) {
            ImGui::TextDisabled(
                "click an element of the list or of the movie" );
        }

        // The stage: the movie itself (the 2D layer), fitted to the rest of
        // the window. draw() needs its rect normalized to the backbuffer, so
        // the layout is reported a frame ahead - the same one frame latency as
        // the asset viewer's movie preview.
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float stageAspect = stageWidth / Max( 1.0f, stageHeight );
        float width = avail.x;
        float height = stageAspect > 0.0001f ? width / stageAspect : avail.y;
        if ( height > avail.y ) {
            height = avail.y;
            width = height * stageAspect;
        }
        if ( width > 16.0f && height > 16.0f && pPlayer != nullptr ) {
            const ImVec2 framebufferScale =
                ImGui::GetIO().DisplayFramebufferScale;
            const float backbufferWidth = static_cast<float>(
                gpRender->getDevice().getBackbufferWidth() );
            const float backbufferHeight = static_cast<float>(
                gpRender->getDevice().getBackbufferHeight() );
            movieRect[0] = imageMin.x * framebufferScale.x /
                           Max( 1.0f, backbufferWidth );
            movieRect[1] = imageMin.y * framebufferScale.y /
                           Max( 1.0f, backbufferHeight );
            movieRect[2] =
                width * framebufferScale.x / Max( 1.0f, backbufferWidth );
            movieRect[3] =
                height * framebufferScale.y / Max( 1.0f, backbufferHeight );
            bMovieRectValid = true;

            ImGui::Dummy( ImVec2( avail.x, avail.y ) );
            if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) ) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float u = ( mouse.x - imageMin.x ) / Max( 1.0f, width );
                const float v = ( mouse.y - imageMin.y ) / Max( 1.0f, height );
                if ( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f ) {
                    const int32_t hit = hitTest( u, v );
                    if ( hit >= 0 ) {
                        selectElement( hit );
                    } else {
                        setStatus( "click: no element there" );
                    }
                }
            }
            // The selected element's bounds, projected onto the stage.
            if ( selected >= 0 &&
                 selected < static_cast<int32_t>( elements.size() ) &&
                 elements[selected].bHasBounds && stageWidth > 0.0f &&
                 stageHeight > 0.0f ) {
                const Element &element = elements[selected];
                float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
                bool bFirst = true;
                const float corners[4][2] = { { element.XMin, element.YMin },
                                              { element.XMax, element.YMin },
                                              { element.XMin, element.YMax },
                                              { element.XMax, element.YMax } };
                for ( const auto &corner : corners ) {
                    const float worldX = element.WorldA * corner[0] +
                                         element.WorldC * corner[1] +
                                         element.WorldTX;
                    const float worldY = element.WorldB * corner[0] +
                                         element.WorldD * corner[1] +
                                         element.WorldTY;
                    const float u =
                        worldX / ( stageWidth * kTwipsPerStagePixel );
                    const float v =
                        worldY / ( stageHeight * kTwipsPerStagePixel );
                    if ( bFirst ) {
                        minX = maxX = u;
                        minY = maxY = v;
                        bFirst = false;
                    } else {
                        minX = Min( minX, u );
                        maxX = Max( maxX, u );
                        minY = Min( minY, v );
                        maxY = Max( maxY, v );
                    }
                }
                ImGui::GetWindowDrawList()->AddRect(
                    ImVec2( imageMin.x + minX * width,
                            imageMin.y + minY * height ),
                    ImVec2( imageMin.x + maxX * width,
                            imageMin.y + maxY * height ),
                    IM_COL32( 255, 214, 64, 255 ), 0.0f, 0, 2.0f );
            }
        } else {
            bMovieRectValid = false;
            ImGui::TextDisabled( "no movie" );
        }
    }
    ImGui::End();
}

