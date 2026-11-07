#include "shared.h"
#include "speech_synth.h"

#include <cmath>
#include <vector>

const char *const SpeechAnimation::kTargets[10] = { "open", "W", "ShCh", "PBM", "FV",
                                                    "wide", "tBack", "tRoof", "tTeeth", "Blink" };

namespace {
    // One driven mouth shape of a phoneme (a FaceFX target node + the value).
    struct Target {
        const char *Name;
        float Value;
    };
    // One spoken unit: a grapheme (or digraph) and how long it lasts.
    struct Unit {
        float Duration = 0.0f;
        std::vector<Target> Targets;
        // Envelope edges inside the unit: a short attack and release keep the
        // mouth from snapping between shapes (linear interpolation of the
        // curve does the rest, see FaceFxCurve::evaluate).
        inline float Attack() const { return Min( 0.030f, Duration * 0.35f ); }
        inline float Release() const { return Min( 0.030f, Duration * 0.35f ); }
    };

    // Durations tuned to a normal speaking rate (~4.5 syllables / s).
    constexpr float kVowel = 0.110f;
    constexpr float kConsonant = 0.060f;
    constexpr float kDigraph = 0.085f;
    constexpr float kWordGap = 0.060f;
    constexpr float kCommaGap = 0.150f;
    constexpr float kStopGap = 0.250f;
    constexpr float kMaxSpeech = 20.0f; // a longer text is cut (the anim loops)

    inline char Lower( char c ) {
        return static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
    }

    void Add( std::vector<Unit> &units, float duration, std::initializer_list<Target> targets ) {
        Unit unit;
        unit.Duration = duration;
        unit.Targets.assign( targets.begin(), targets.end() );
        units.push_back( std::move( unit ) );
    }

    // The mouth shape of one letter. English spelling, not phonetics: this is
    // the lookup a text-to-speech front end would do with a real dictionary
    // [INF]; the shapes are the ones the retail animations use.
    void AddLetter( std::vector<Unit> &units, char c ) {
        switch ( c ) {
        case 'a':
            Add( units, kVowel, { { "open", 0.75f } } );
            break;
        case 'e':
            Add( units, kVowel, { { "open", 0.55f }, { "wide", 0.45f } } );
            break;
        case 'i':
            Add( units, kVowel, { { "open", 0.30f }, { "wide", 0.70f } } );
            break;
        case 'o':
            Add( units, kVowel, { { "open", 0.55f }, { "W", 0.65f } } );
            break;
        case 'u':
            Add( units, kVowel, { { "W", 0.80f } } );
            break;
        case 'y':
            Add( units, kVowel, { { "wide", 0.60f } } );
            break;
        // Bilabials: the lips close (the one shape that is unmistakable).
        case 'p':
        case 'b':
        case 'm':
            Add( units, kConsonant, { { "PBM", 0.90f } } );
            break;
        // Labiodentals: the lower lip against the teeth.
        case 'f':
        case 'v':
            Add( units, kConsonant, { { "FV", 0.80f } } );
            break;
        // Alveolars: tongue tip up behind the teeth.
        case 't':
        case 'd':
        case 'n':
        case 'l':
            Add( units, kConsonant, { { "tRoof", 0.65f } } );
            break;
        // Velars: tongue back.
        case 'k':
        case 'g':
        case 'q':
            Add( units, kConsonant, { { "tBack", 0.70f } } );
            break;
        // Sibilants / affricates.
        case 's':
        case 'z':
        case 'x':
            Add( units, kConsonant, { { "ShCh", 0.55f }, { "wide", 0.25f } } );
            break;
        case 'j':
            Add( units, kConsonant, { { "ShCh", 0.70f } } );
            break;
        case 'h':
            Add( units, kConsonant, { { "open", 0.40f } } );
            break;
        case 'w':
            Add( units, kConsonant, { { "W", 0.85f } } );
            break;
        case 'r':
            Add( units, kConsonant, { { "W", 0.45f }, { "wide", 0.20f } } );
            break;
        default:
            // An unlisted letter ('c' on its own, ...): a neutral opening,
            // never silence in the middle of a word.
            Add( units, kConsonant, { { "tBack", 0.35f } } );
            break;
        }
    }

    // The text -> units pass. Spaces, commas and stops become pauses (a gap
    // the envelopes fall back to zero in), so words are separated.
    void BuildUnits( const std::string &text, std::vector<Unit> &units ) {
        float total = 0.0f;
        for ( size_t i = 0; i < text.size() && total < kMaxSpeech; i++ ) {
            const size_t before = units.size();
            const char c = Lower( text[i] );
            const char next = i + 1 < text.size() ? Lower( text[i + 1] ) : '\0';
            // Digraphs first: they are one mouth shape, not two.
            if ( c == 'c' && next == 'h' ) {
                Add( units, kDigraph, { { "ShCh", 0.90f } } );
                i++;
            } else if ( c == 's' && next == 'h' ) {
                Add( units, kDigraph, { { "ShCh", 0.90f } } );
                i++;
            } else if ( c == 't' && next == 'h' ) {
                Add( units, kDigraph, { { "tTeeth", 0.85f } } );
                i++;
            } else if ( c == 'p' && next == 'h' ) {
                Add( units, kDigraph, { { "FV", 0.80f } } );
                i++;
            } else if ( c == 'n' && next == 'g' ) {
                Add( units, kDigraph, { { "tBack", 0.60f } } );
                i++;
            } else if ( c == ' ' || c == '\t' || c == '\n' ) {
                Add( units, kWordGap, {} );
            } else if ( c == ',' || c == ';' || c == ':' ) {
                Add( units, kCommaGap, {} );
            } else if ( c == '.' || c == '!' || c == '?' ) {
                Add( units, kStopGap, {} );
            } else if ( c == '\'' || c == '-' || c == '"' ) {
                // Punctuation that is not a pause: skip it.
            } else if ( c >= 'a' && c <= 'z' ) {
                AddLetter( units, c );
            }
            if ( units.size() != before ) {
                total += units.back().Duration;
            }
        }
    }

    // One curve under construction; keys are appended in time order.
    struct Track {
        std::string Name;
        std::vector<FaceFxCurve::Key> Keys;
        float Last = -1.0f;

        void Push( float time, float value ) {
            if ( time <= Last ) {
                time = Last + 0.001f;
            }
            Keys.push_back( { time, value, 0.0f, 0.0f } );
            Last = time;
        }
    };

    // Value of one target at time t: the trapezoid of the unit that covers t
    // (a run of the same target in consecutive units therefore stays open).
    float Envelope( const std::vector<Unit> &units, const std::vector<float> &starts,
                    const char *pTarget, float t ) {
        for ( size_t u = 0; u < units.size(); u++ ) {
            const Unit &unit = units[u];
            const float begin = starts[u];
            const float end = begin + unit.Duration;
            if ( t < begin || t >= end ) {
                continue;
            }
            for ( const Target &target : unit.Targets ) {
                if ( target.Name != pTarget ) {
                    continue;
                }
                float value = target.Value;
                if ( unit.Attack() > 1e-4f ) {
                    value = Min( value, target.Value * ( t - begin ) / unit.Attack() );
                }
                if ( unit.Release() > 1e-4f ) {
                    value = Min( value, target.Value * ( end - t ) / unit.Release() );
                }
                return Max( value, 0.0f );
            }
            return 0.0f;
        }
        return 0.0f;
    }
} // namespace

bool SpeechAnimation::Build( const char *pText, FaceFxAnim &outAnim ) {
    outAnim = FaceFxAnim();
    if ( pText == nullptr ) {
        return false;
    }
    std::vector<Unit> units;
    BuildUnits( pText, units );
    if ( units.empty() ) {
        return false;
    }
    std::vector<float> starts( units.size() );
    float total = 0.0f;
    for ( size_t u = 0; u < units.size(); u++ ) {
        starts[u] = total;
        total += units[u].Duration;
    }
    constexpr float kTail = 0.15f; // the mouth closes and holds closed
    total += kTail;

    // 5 ms sampling of every target's envelope, a key only where the value
    // actually moves: the curves stay small (a few hundred keys) and the
    // linear interpolation between them is the mouth's motion.
    constexpr float kStep = 0.005f;
    constexpr float kEpsilon = 0.015f;
    std::vector<Track> tracks;
    for ( int32_t i = 0; i < 9; i++ ) { // kTargets[9] = Blink, added below
        Track track;
        track.Name = kTargets[i];
        float previous = -1.0f;
        for ( float t = 0.0f; t <= total; t += kStep ) {
            const float value = Envelope( units, starts, kTargets[i], t );
            if ( previous < 0.0f || std::fabs( value - previous ) > kEpsilon || t + kStep > total ) {
                track.Push( t, value );
                previous = value;
            }
        }
        track.Push( total, 0.0f );
        if ( track.Keys.size() >= 2 ) {
            tracks.push_back( std::move( track ) );
        }
    }
    // Blinks: one every ~3.4 s, 0.1 s long (the retail curves animate "Blink"
    // the same way, docs/re/facefx_spec.md 1.1).
    Track blink;
    blink.Name = kTargets[9];
    blink.Push( 0.0f, 0.0f );
    for ( float t = 1.5f; t + 0.12f < total; t += 3.4f ) {
        blink.Push( t, 0.0f );
        blink.Push( t + 0.05f, 1.0f );
        blink.Push( t + 0.10f, 0.0f );
    }
    tracks.push_back( std::move( blink ) );

    outAnim.Name = "OTDU_Say";
    outAnim.StartTime = 0.0f;
    outAnim.EndTime = total;
    for ( Track &track : tracks ) {
        FaceFxCurve curve;
        curve.Name = track.Name;
        curve.Interpolation = 1; // linear (FxAnimCurve +0x1c)
        curve.Keys = std::move( track.Keys );
        outAnim.Curves.push_back( std::move( curve ) );
    }
    return true;
}
