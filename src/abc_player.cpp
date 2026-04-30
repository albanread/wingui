#ifndef WINGUI_BUILD_DLL
#define WINGUI_BUILD_DLL
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "wingui/wingui.h"

#include "wingui_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

namespace {

struct Fraction {
    int32_t num = 0;
    int32_t denom = 1;

    static Fraction init(int32_t num_value, int32_t denom_value) {
        Fraction value;
        value.num = num_value;
        value.denom = denom_value == 0 ? 1 : denom_value;
        return value;
    }

    double toDouble() const {
        return static_cast<double>(num) / static_cast<double>(denom == 0 ? 1 : denom);
    }

    Fraction mul(Fraction other) const {
        return init(num * other.num, denom * other.denom);
    }

    Fraction add(Fraction other) const {
        return init(num * other.denom + other.num * denom, denom * other.denom);
    }
};

struct Tempo {
    int32_t bpm = 120;
};

struct TimeSig {
    uint8_t num = 4;
    uint8_t denom = 4;
};

struct KeySig {
    int8_t sharps = 0;
    bool is_major = true;
};

struct VoiceContext {
    int32_t id = 0;
    std::string name;
    KeySig key;
    TimeSig timesig;
    Fraction unit_len = Fraction::init(1, 8);
    int8_t transpose = 0;
    int8_t octave_shift = 0;
    uint8_t instrument = 0;
    int8_t channel = -1;
    uint8_t velocity = 80;
    bool percussion = false;
};

struct NoteFeature {
    uint8_t pitch = 0;
    int8_t accidental = 0;
    int8_t octave = 0;
    Fraction duration = Fraction::init(1, 8);
    uint8_t midi_note = 60;
    uint8_t velocity = 80;
    bool is_tied = false;
};

struct RestFeature {
    Fraction duration = Fraction::init(1, 8);
};

struct ChordFeature {
    std::vector<NoteFeature> notes;
    Fraction duration = Fraction::init(1, 8);
};

struct GuitarChordFeature {
    std::string symbol;
    uint8_t root_note = 60;
    std::string chord_type;
    Fraction duration = Fraction::init(1, 8);
};

enum class BarLineType {
    bar1,
    double_bar,
    rep_bar,
    bar_rep,
    double_rep,
};

struct BarLineFeature {
    BarLineType bar_type = BarLineType::bar1;
};

struct VoiceChangeFeature {
    int32_t voice_number = 0;
    std::string voice_name;
};

enum class FeatureType {
    note,
    rest,
    chord,
    gchord,
    bar,
    tempo,
    time,
    key,
    voice,
};

struct Feature {
    FeatureType type = FeatureType::note;
    int32_t voice_id = 0;
    double ts = 0.0;
    size_t line_number = 0;
    NoteFeature note{};
    RestFeature rest{};
    ChordFeature chord{};
    GuitarChordFeature gchord{};
    BarLineFeature bar{};
    Tempo tempo{};
    TimeSig time{};
    KeySig key{};
    VoiceChangeFeature voice{};
};

struct Tune {
    std::string title;
    std::string history;
    std::string composer;
    std::string origin;
    std::string rhythm;
    std::string notes;
    std::string words;
    std::string aligned_words;
    KeySig default_key{};
    TimeSig default_timesig{};
    Fraction default_unit = Fraction::init(1, 8);
    Tempo default_tempo{};
    uint8_t default_instrument = 0;
    int8_t default_channel = -1;
    bool default_percussion = false;
    std::map<int32_t, VoiceContext> voices;
    std::vector<Feature> features;
};

enum class ParseState {
    header,
    body,
    complete,
};

std::string trimCopy(std::string_view text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

bool isHeaderField(std::string_view line) {
    return line.size() >= 2 && std::isalpha(static_cast<unsigned char>(line[0])) && line[1] == ':';
}

bool isStringField(char field) {
    switch (field) {
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'G':
    case 'H':
    case 'N':
    case 'O':
    case 'R':
    case 'S':
    case 'T':
    case 'W':
    case 'Z':
    case 'w':
        return true;
    default:
        return false;
    }
}

KeySig parseKeySig(std::string_view value) {
    const std::string key_name = trimCopy(value);
    const bool is_minor = key_name.find('m') != std::string::npos && key_name != "M";

    int8_t sharps = 0;
    if (key_name.rfind("C#", 0) == 0) sharps = 7;
    else if (key_name.rfind("F#", 0) == 0) sharps = 6;
    else if (key_name.rfind("B", 0) == 0) sharps = 5;
    else if (key_name.rfind("E", 0) == 0) sharps = 4;
    else if (key_name.rfind("A", 0) == 0) sharps = 3;
    else if (key_name.rfind("D", 0) == 0) sharps = 2;
    else if (key_name.rfind("G", 0) == 0) sharps = 1;
    else if (key_name.rfind("Cb", 0) == 0) sharps = -7;
    else if (key_name.rfind("Gb", 0) == 0) sharps = -6;
    else if (key_name.rfind("Db", 0) == 0) sharps = -5;
    else if (key_name.rfind("Ab", 0) == 0) sharps = -4;
    else if (key_name.rfind("Eb", 0) == 0) sharps = -3;
    else if (key_name.rfind("Bb", 0) == 0) sharps = -2;
    else if (key_name.rfind("F", 0) == 0) sharps = -1;

    KeySig key;
    key.sharps = sharps;
    key.is_major = !is_minor;
    return key;
}

void appendStringField(std::string& field, std::string_view value) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return;
    }
    if (!field.empty()) {
        field.push_back(' ');
    }
    field += trimmed;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string expandLineRepeats(std::string_view line) {
    std::string result;
    size_t pos = 0;
    while (pos < line.size()) {
        const size_t start = line.find("|:", pos);
        if (start == std::string_view::npos) {
            result.append(line.substr(pos));
            break;
        }
        const size_t end = line.find(":|", start + 2);
        if (end == std::string_view::npos) {
            result.append(line.substr(pos));
            break;
        }
        result.append(line.substr(pos, start - pos));
        const std::string repeated = trimCopy(line.substr(start + 2, end - (start + 2)));
        result += repeated;
        result.push_back(' ');
        result += repeated;
        pos = end + 2;
    }
    return result;
}

std::string expandVoiceSectionRepeats(std::string_view section) {
    std::string output;
    std::vector<std::string> repeat_buffer;
    bool in_repeat = false;

    for (const std::string& raw_line : splitLines(section)) {
        const size_t repeat_start = raw_line.find("|:");
        if (repeat_start != std::string::npos) {
            in_repeat = true;
            const size_t repeat_end = raw_line.find(":|", repeat_start + 2);
            if (repeat_end != std::string::npos) {
                output += expandLineRepeats(raw_line);
                output.push_back('\n');
                in_repeat = false;
                continue;
            }

            std::string cleaned = raw_line.substr(0, repeat_start);
            cleaned.push_back('|');
            cleaned += raw_line.substr(repeat_start + 2);
            repeat_buffer.push_back(cleaned);
            continue;
        }

        if (in_repeat) {
            const size_t repeat_end = raw_line.find(":|");
            if (repeat_end != std::string::npos) {
                std::string cleaned = raw_line.substr(0, repeat_end);
                cleaned.push_back('|');
                cleaned += raw_line.substr(repeat_end + 2);
                repeat_buffer.push_back(cleaned);
                for (int pass = 0; pass < 2; ++pass) {
                    for (const std::string& repeat_line : repeat_buffer) {
                        output += repeat_line;
                        output.push_back('\n');
                    }
                }
                repeat_buffer.clear();
                in_repeat = false;
                continue;
            }

            repeat_buffer.push_back(raw_line);
            continue;
        }

        output += raw_line;
        output.push_back('\n');
    }

    for (const std::string& repeat_line : repeat_buffer) {
        output += repeat_line;
        output.push_back('\n');
    }

    return output;
}

std::string expandABCRepeats(std::string_view abc_content) {
    std::string output;
    std::string current_voice_section;
    bool in_header = true;
    bool in_voice_section = false;

    for (const std::string& raw_line : splitLines(abc_content)) {
        if (in_header) {
            output += raw_line;
            output.push_back('\n');
            if (raw_line.size() >= 2 && raw_line[0] == 'K' && raw_line[1] == ':') {
                in_header = false;
            }
            continue;
        }

        const bool starts_voice = raw_line.size() >= 2 && raw_line[0] == 'V' && raw_line[1] == ':';
        const bool inline_voice = raw_line.size() >= 4 && raw_line[0] == '[' && raw_line[1] == 'V' && raw_line[2] == ':';
        const bool directive = raw_line.size() >= 2 && raw_line[0] == '%' && raw_line[1] == '%';
        const bool empty = trimCopy(raw_line).empty();

        if (starts_voice || inline_voice || directive || empty) {
            if (in_voice_section && !current_voice_section.empty()) {
                output += expandVoiceSectionRepeats(current_voice_section);
                current_voice_section.clear();
            }
            in_voice_section = false;
            output += raw_line;
            output.push_back('\n');
            continue;
        }

        in_voice_section = true;
        current_voice_section += raw_line;
        current_voice_section.push_back('\n');
    }

    if (!current_voice_section.empty()) {
        output += expandVoiceSectionRepeats(current_voice_section);
    }

    return output;
}

struct VoiceManager {
    int32_t current_voice = 1;
    int32_t next_voice_id = 1;
    std::map<int32_t, double> voice_times;
    std::map<std::string, int32_t> voice_name_to_id;

    void reset() {
        current_voice = 1;
        next_voice_id = 1;
        voice_times.clear();
        voice_name_to_id.clear();
    }

    void saveCurrentTime(double time) {
        if (current_voice != 0) {
            voice_times[current_voice] = time;
        }
    }

    double restoreVoiceTime(int32_t voice_id) const {
        const auto it = voice_times.find(voice_id);
        return it != voice_times.end() ? it->second : 0.0;
    }

    void initializeVoiceFromDefaults(VoiceContext& voice, const Tune& tune) const {
        voice.key = tune.default_key;
        voice.timesig = tune.default_timesig;
        voice.unit_len = tune.default_unit;
        voice.transpose = 0;
        voice.octave_shift = 0;
        voice.instrument = tune.default_instrument;
        voice.channel = tune.default_channel;
        voice.velocity = 80;
        voice.percussion = tune.default_percussion;
    }

    int32_t findVoiceByIdentifier(std::string_view identifier, const Tune& tune) const {
        char* end_ptr = nullptr;
        const long parsed = std::strtol(std::string(identifier).c_str(), &end_ptr, 10);
        if (end_ptr && *end_ptr == '\0') {
            const auto voice_it = tune.voices.find(static_cast<int32_t>(parsed));
            if (voice_it != tune.voices.end()) {
                return static_cast<int32_t>(parsed);
            }
        }

        for (const auto& entry : tune.voices) {
            if (entry.second.name == identifier) {
                return entry.first;
            }
        }

        const auto named = voice_name_to_id.find(std::string(identifier));
        return named != voice_name_to_id.end() ? named->second : 0;
    }

    int32_t getOrCreateVoice(std::string_view identifier, Tune& tune) {
        const int32_t existing_id = findVoiceByIdentifier(identifier, tune);
        if (existing_id != 0) {
            return existing_id;
        }

        const int32_t voice_id = next_voice_id++;
        VoiceContext voice;
        voice.id = voice_id;
        voice.name = std::string(identifier);
        initializeVoiceFromDefaults(voice, tune);
        tune.voices.emplace(voice_id, voice);
        voice_name_to_id.emplace(std::string(identifier), voice_id);
        return voice_id;
    }

    int32_t switchToVoice(std::string_view identifier, Tune& tune) {
        int32_t voice_id = findVoiceByIdentifier(identifier, tune);
        if (voice_id == 0) {
            voice_id = getOrCreateVoice(identifier, tune);
        }
        current_voice = voice_id;
        return voice_id;
    }
};

bool isCompoundMeter(const TimeSig& timesig) {
    return timesig.denom == 8 && (timesig.num == 6 || timesig.num == 9 || timesig.num == 12);
}

int32_t inferTupletQ(int32_t p, const TimeSig& timesig) {
    switch (p) {
    case 2: return 3;
    case 3: return 2;
    case 4: return 3;
    case 6: return 2;
    case 8: return 3;
    case 5:
    case 7:
    case 9:
        return isCompoundMeter(timesig) ? 3 : 2;
    default:
        return isCompoundMeter(timesig) ? 3 : 2;
    }
}

std::array<std::optional<int8_t>, 7> emptyAccidentals() {
    return { std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt };
}

bool isNoteChar(char c) {
    return (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g');
}

bool isBarLineChar(char c) {
    return c == '|' || c == ':' || c == '[' || c == ']';
}

bool isAccidentalMarker(char c) {
    return c == '^' || c == '_' || c == '=';
}

size_t skipWhitespace(std::string_view text, size_t pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

size_t skipTieJoinDelimiters(std::string_view text, size_t pos) {
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '|' || c == ':') {
            ++pos;
            continue;
        }
        break;
    }
    return pos;
}

bool skipGraceGroup(std::string_view text, size_t& pos) {
    if (pos >= text.size() || text[pos] != '{') {
        return false;
    }
    ++pos;
    if (pos < text.size() && text[pos] == '/') {
        ++pos;
    }
    while (pos < text.size() && text[pos] != '}') {
        ++pos;
    }
    if (pos < text.size() && text[pos] == '}') {
        ++pos;
    } else {
        pos = text.size();
    }
    return true;
}

std::optional<size_t> pitchIndex(uint8_t pitch) {
    switch (static_cast<char>(std::toupper(pitch))) {
    case 'A': return 0;
    case 'B': return 1;
    case 'C': return 2;
    case 'D': return 3;
    case 'E': return 4;
    case 'F': return 5;
    case 'G': return 6;
    default: return std::nullopt;
    }
}

int8_t keyAccidentalForPitch(const KeySig& key, uint8_t pitch) {
    static constexpr const char* sharp_order = "FCGDAEB";
    static constexpr const char* flat_order = "BEADGCF";

    if (key.sharps > 0) {
        for (int i = 0; i < key.sharps && sharp_order[i] != '\0'; ++i) {
            if (sharp_order[i] == static_cast<char>(pitch)) {
                return 1;
            }
        }
        return 0;
    }

    if (key.sharps < 0) {
        const int count = -key.sharps;
        for (int i = 0; i < count && flat_order[i] != '\0'; ++i) {
            if (flat_order[i] == static_cast<char>(pitch)) {
                return -1;
            }
        }
    }

    return 0;
}

uint8_t calculateMidiNote(uint8_t pitch, int8_t accidental, int8_t octave, int8_t transpose) {
    const uint8_t normalized = static_cast<uint8_t>(std::toupper(pitch));
    if (normalized < 'A' || normalized > 'G') {
        return 60;
    }

    static constexpr uint8_t semitones[] = { 9, 11, 0, 2, 4, 5, 7 };
    int32_t semitone = semitones[normalized - 'A'];
    semitone += accidental;

    int32_t base_octave = std::islower(pitch) ? 5 : 4;
    base_octave += octave;

    int32_t midi = base_octave * 12 + semitone + transpose;
    midi = std::max(0, std::min(127, midi));
    return static_cast<uint8_t>(midi);
}

std::optional<int8_t> parseAccidental(std::string_view text, size_t& pos) {
    if (pos >= text.size()) {
        return std::nullopt;
    }
    if (text[pos] == '^') {
        if (pos + 1 < text.size() && text[pos + 1] == '^') {
            pos += 2;
            return 2;
        }
        ++pos;
        return 1;
    }
    if (text[pos] == '_') {
        if (pos + 1 < text.size() && text[pos + 1] == '_') {
            pos += 2;
            return -2;
        }
        ++pos;
        return -1;
    }
    if (text[pos] == '=') {
        ++pos;
        return 0;
    }
    return std::nullopt;
}

uint8_t parseNotePitch(std::string_view text, size_t& pos) {
    if (pos >= text.size() || !isNoteChar(text[pos])) {
        return 0;
    }
    return static_cast<uint8_t>(text[pos++]);
}

int8_t parseOctave(std::string_view text, size_t& pos) {
    int8_t octave = 0;
    while (pos < text.size() && text[pos] == '\'') {
        ++octave;
        ++pos;
    }
    while (pos < text.size() && text[pos] == ',') {
        --octave;
        ++pos;
    }
    return octave;
}

Fraction parseDuration(std::string_view text, size_t& pos, Fraction default_duration) {
    Fraction duration = default_duration;

    if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        int32_t numerator = 0;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            numerator = numerator * 10 + (text[pos] - '0');
            ++pos;
        }

        if (pos < text.size() && text[pos] == '/') {
            ++pos;
            int32_t denominator = 2;
            if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                denominator = 0;
                while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                    denominator = denominator * 10 + (text[pos] - '0');
                    ++pos;
                }
            }
            duration = Fraction::init(numerator, denominator).mul(default_duration);
        } else {
            duration = Fraction::init(numerator, 1).mul(default_duration);
        }
    } else if (pos < text.size() && text[pos] == '/') {
        ++pos;
        int32_t denominator = 2;
        if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            denominator = 0;
            while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                denominator = denominator * 10 + (text[pos] - '0');
                ++pos;
            }
        }
        duration = Fraction::init(default_duration.num, default_duration.denom * denominator);
    }

    while (pos < text.size() && text[pos] == '.') {
        duration = duration.mul(Fraction::init(3, 2));
        ++pos;
    }

    return duration;
}

uint8_t parseChordRoot(std::string_view symbol) {
    if (symbol.empty()) {
        return 60;
    }

    uint8_t midi_note = 60;
    switch (static_cast<char>(std::toupper(symbol[0]))) {
    case 'C': midi_note = 60; break;
    case 'D': midi_note = 62; break;
    case 'E': midi_note = 64; break;
    case 'F': midi_note = 65; break;
    case 'G': midi_note = 67; break;
    case 'A': midi_note = 69; break;
    case 'B': midi_note = 71; break;
    default: midi_note = 60; break;
    }
    if (symbol.size() > 1) {
        if (symbol[1] == '#') ++midi_note;
        if (symbol[1] == 'b') --midi_note;
    }
    return static_cast<uint8_t>(midi_note - 12);
}

std::string parseChordType(std::string_view symbol) {
    size_t type_start = 1;
    if (symbol.size() > 1 && (symbol[1] == '#' || symbol[1] == 'b')) {
        type_start = 2;
    }
    if (type_start >= symbol.size()) {
        return "major";
    }

    const std::string t(symbol.substr(type_start));
    if (t == "m" || t == "min") return "minor";
    if (t == "7") return "dom7";
    if (t == "maj7" || t == "M7") return "maj7";
    if (t == "m7") return "m7";
    if (t == "dim" || t == "o") return "dim";
    if (t == "aug" || t == "+") return "aug";
    return t.empty() ? "major" : t;
}

struct MusicParser {
    double current_time = 0.0;
    size_t current_line = 0;
    std::map<int32_t, std::array<std::optional<int8_t>, 7>> voice_bar_accidentals;
    int32_t tuplet_num = 1;
    int32_t tuplet_denom = 1;
    int32_t tuplet_notes_remaining = 0;

    void reset() {
        current_time = 0.0;
        current_line = 0;
        voice_bar_accidentals.clear();
        tuplet_num = 1;
        tuplet_denom = 1;
        tuplet_notes_remaining = 0;
    }

    std::array<std::optional<int8_t>, 7>& ensureVoiceBarAccidentalState(int32_t voice_id) {
        auto [it, inserted] = voice_bar_accidentals.emplace(voice_id, emptyAccidentals());
        if (inserted) {
            it->second = emptyAccidentals();
        }
        return it->second;
    }

    void setBarAccidental(int32_t voice_id, size_t note_index, int8_t accidental) {
        ensureVoiceBarAccidentalState(voice_id)[note_index] = accidental;
    }

    std::optional<int8_t> getBarAccidental(int32_t voice_id, size_t note_index) const {
        const auto it = voice_bar_accidentals.find(voice_id);
        if (it == voice_bar_accidentals.end()) {
            return std::nullopt;
        }
        return it->second[note_index];
    }

    void resetVoiceBarAccidentals(int32_t voice_id) {
        voice_bar_accidentals[voice_id] = emptyAccidentals();
    }

    void applyTupletToDuration(Fraction& duration) {
        if (tuplet_notes_remaining <= 0) {
            return;
        }

        duration = duration.mul(Fraction::init(tuplet_num, tuplet_denom));
        --tuplet_notes_remaining;
        if (tuplet_notes_remaining <= 0) {
            tuplet_num = 1;
            tuplet_denom = 1;
            tuplet_notes_remaining = 0;
        }
    }

    bool parseTupletSpecifier(std::string_view text, size_t& pos, Tune& tune, int32_t voice_id) {
        if (pos >= text.size() || text[pos] != '(' || pos + 1 >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
            return false;
        }

        ++pos;
        const int32_t p = text[pos] - '0';
        ++pos;

        std::optional<int32_t> q;
        std::optional<int32_t> r;
        if (pos < text.size() && text[pos] == ':') {
            ++pos;
            if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                q = text[pos] - '0';
                ++pos;
            }
            if (pos < text.size() && text[pos] == ':') {
                ++pos;
                if (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                    r = text[pos] - '0';
                    ++pos;
                }
            }
        }

        const auto voice_it = tune.voices.find(voice_id);
        if (voice_it == tune.voices.end()) {
            return false;
        }
        const int32_t final_q = q.has_value() ? *q : inferTupletQ(p, voice_it->second.timesig);
        const int32_t final_r = r.has_value() ? *r : p;
        if (p <= 0 || final_q <= 0 || final_r <= 0) {
            return false;
        }

        tuplet_num = final_q;
        tuplet_denom = p;
        tuplet_notes_remaining = final_r;
        return true;
    }

    void createFeature(Tune& tune, Feature feature) {
        tune.features.push_back(std::move(feature));
    }

    bool parseNote(std::string_view text, size_t& pos, NoteFeature& note, Tune& tune, int32_t voice_id) {
        if (pos >= text.size()) {
            return false;
        }

        size_t probe = pos;
        (void)parseAccidental(text, probe);
        if (probe >= text.size() || !isNoteChar(text[probe])) {
            return false;
        }

        const std::optional<int8_t> explicit_accidental = parseAccidental(text, pos);
        note.pitch = parseNotePitch(text, pos);
        if (note.pitch == 0) {
            return false;
        }
        note.octave = parseOctave(text, pos);

        const auto voice_it = tune.voices.find(voice_id);
        if (voice_it == tune.voices.end()) {
            return false;
        }
        const VoiceContext& voice = voice_it->second;
        note.duration = parseDuration(text, pos, voice.unit_len);

        const auto note_index = pitchIndex(note.pitch);
        if (!note_index.has_value()) {
            return false;
        }

        int8_t accidental = keyAccidentalForPitch(voice.key, static_cast<uint8_t>(std::toupper(note.pitch)));
        if (explicit_accidental.has_value()) {
            accidental = *explicit_accidental;
            setBarAccidental(voice_id, *note_index, accidental);
        } else {
            const auto bar_accidental = getBarAccidental(voice_id, *note_index);
            if (bar_accidental.has_value()) {
                accidental = *bar_accidental;
            }
        }

        note.accidental = accidental;
        note.midi_note = calculateMidiNote(note.pitch, accidental, note.octave, voice.transpose);
        note.velocity = voice.velocity;
        note.is_tied = false;
        return true;
    }

    bool parseRest(std::string_view text, size_t& pos, RestFeature& rest, Tune& tune, int32_t voice_id) {
        if (pos >= text.size() || (text[pos] != 'z' && text[pos] != 'Z')) {
            return false;
        }
        ++pos;
        const auto voice_it = tune.voices.find(voice_id);
        if (voice_it == tune.voices.end()) {
            return false;
        }
        rest.duration = parseDuration(text, pos, voice_it->second.unit_len);
        return true;
    }

    bool parseChord(std::string_view text, size_t& pos, ChordFeature& chord, Tune& tune, int32_t voice_id) {
        if (pos >= text.size() || text[pos] != '[') {
            return false;
        }
        const size_t start_pos = pos;
        ++pos;
        chord.notes.clear();

        while (pos < text.size() && text[pos] != ']') {
            pos = skipWhitespace(text, pos);
            if (pos >= text.size() || text[pos] == ']') {
                break;
            }

            NoteFeature note;
            if (parseNote(text, pos, note, tune, voice_id)) {
                chord.notes.push_back(note);
            } else if (pos < text.size() && text[pos] != ']') {
                ++pos;
            }
            pos = skipWhitespace(text, pos);
        }

        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            const auto voice_it = tune.voices.find(voice_id);
            if (voice_it == tune.voices.end()) {
                return false;
            }
            chord.duration = parseDuration(text, pos, voice_it->second.unit_len);
            return !chord.notes.empty();
        }

        pos = std::min(start_pos + 1, text.size());
        chord.notes.clear();
        return false;
    }

    bool parseGuitarChord(std::string_view text, size_t& pos, GuitarChordFeature& gchord) {
        if (pos >= text.size() || text[pos] != '"') {
            return false;
        }
        ++pos;
        const size_t start = pos;
        while (pos < text.size() && text[pos] != '"') {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != '"') {
            return false;
        }

        gchord.symbol = std::string(text.substr(start, pos - start));
        ++pos;
        gchord.root_note = parseChordRoot(gchord.symbol);
        gchord.chord_type = parseChordType(gchord.symbol);
        return true;
    }

    bool parseBarLine(std::string_view text, size_t& pos, BarLineFeature& barline) {
        if (pos >= text.size() || !isBarLineChar(text[pos])) {
            return false;
        }
        const size_t start = pos;
        while (pos < text.size() && isBarLineChar(text[pos])) {
            ++pos;
        }
        const std::string token(text.substr(start, pos - start));
        if (token == "|") barline.bar_type = BarLineType::bar1;
        else if (token == "||") barline.bar_type = BarLineType::double_bar;
        else if (token == "|:") barline.bar_type = BarLineType::rep_bar;
        else if (token == ":|") barline.bar_type = BarLineType::bar_rep;
        else if (token == ":|:") barline.bar_type = BarLineType::double_rep;
        else barline.bar_type = BarLineType::bar1;
        return true;
    }

    bool parseInlineVoiceSwitch(std::string_view text, size_t& pos, Tune& tune, VoiceManager& voice_manager) {
        if (pos + 2 >= text.size() || text[pos] != '[' || text[pos + 1] != 'V' || text[pos + 2] != ':') {
            return false;
        }
        pos += 3;
        const size_t start = pos;
        while (pos < text.size() && text[pos] != ']' && !std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        const std::string identifier(text.substr(start, pos - start));
        while (pos < text.size() && text[pos] != ']') {
            ++pos;
        }
        if (pos >= text.size() || text[pos] != ']') {
            return false;
        }
        ++pos;
        if (identifier.empty()) {
            return false;
        }

        voice_manager.saveCurrentTime(current_time);
        const int32_t voice_id = voice_manager.switchToVoice(identifier, tune);
        current_time = voice_manager.restoreVoiceTime(voice_id);

        Feature feature;
        feature.type = FeatureType::voice;
        feature.voice_id = voice_id;
        feature.ts = current_time;
        feature.line_number = current_line;
        feature.voice.voice_number = voice_id;
        feature.voice.voice_name = identifier;
        createFeature(tune, std::move(feature));
        return true;
    }

    void applyInlineFieldValue(char field, std::string_view value, Tune& tune, VoiceManager& voice_manager) {
        auto voice_it = tune.voices.find(voice_manager.current_voice);
        if (voice_it == tune.voices.end()) {
            return;
        }
        VoiceContext& voice = voice_it->second;

        switch (field) {
        case 'Q': {
            std::string tempo_value = std::string(value);
            const size_t eq = tempo_value.find('=');
            if (eq != std::string::npos) {
                tempo_value = trimCopy(std::string_view(tempo_value).substr(eq + 1));
            }
            if (!tempo_value.empty()) {
                const int bpm = std::atoi(tempo_value.c_str());
                tune.default_tempo.bpm = bpm;
                Feature feature;
                feature.type = FeatureType::tempo;
                feature.voice_id = voice_manager.current_voice;
                feature.ts = current_time;
                feature.line_number = current_line;
                feature.tempo.bpm = bpm;
                createFeature(tune, std::move(feature));
            }
            break;
        }
        case 'M': {
            TimeSig timesig = voice.timesig;
            const std::string meter = trimCopy(value);
            if (meter == "C" || meter == "c") {
                timesig = TimeSig{4, 4};
            } else if (meter == "C|" || meter == "c|") {
                timesig = TimeSig{2, 2};
            } else {
                const size_t slash = meter.find('/');
                if (slash != std::string::npos) {
                    timesig.num = static_cast<uint8_t>(std::atoi(meter.substr(0, slash).c_str()));
                    timesig.denom = static_cast<uint8_t>(std::atoi(meter.substr(slash + 1).c_str()));
                }
            }
            voice.timesig = timesig;
            Feature feature;
            feature.type = FeatureType::time;
            feature.voice_id = voice_manager.current_voice;
            feature.ts = current_time;
            feature.line_number = current_line;
            feature.time = timesig;
            createFeature(tune, std::move(feature));
            break;
        }
        case 'L': {
            const std::string unit = trimCopy(value);
            const size_t slash = unit.find('/');
            if (slash != std::string::npos) {
                voice.unit_len = Fraction::init(std::atoi(unit.substr(0, slash).c_str()), std::atoi(unit.substr(slash + 1).c_str()));
            }
            break;
        }
        case 'K': {
            voice.key = parseKeySig(value);
            Feature feature;
            feature.type = FeatureType::key;
            feature.voice_id = voice_manager.current_voice;
            feature.ts = current_time;
            feature.line_number = current_line;
            feature.key = voice.key;
            createFeature(tune, std::move(feature));
            break;
        }
        default:
            break;
        }
    }

    bool parseInlineBracketField(std::string_view text, size_t& pos, Tune& tune, VoiceManager& voice_manager) {
        if (pos + 3 >= text.size() || text[pos] != '[') {
            return false;
        }
        const char field = text[pos + 1];
        if (!std::isalpha(static_cast<unsigned char>(field)) || text[pos + 2] != ':') {
            return false;
        }
        if (std::toupper(static_cast<unsigned char>(field)) == 'V') {
            return parseInlineVoiceSwitch(text, pos, tune, voice_manager);
        }

        size_t p = pos + 3;
        const size_t value_start = p;
        while (p < text.size() && text[p] != ']') {
            ++p;
        }
        if (p >= text.size()) {
            return false;
        }
        applyInlineFieldValue(static_cast<char>(std::toupper(static_cast<unsigned char>(field))), text.substr(value_start, p - value_start), tune, voice_manager);
        pos = p + 1;
        return true;
    }

    void mergeTiedNotes(std::string_view text, size_t& pos, NoteFeature& note, Tune& tune, int32_t voice_id) {
        while (note.is_tied) {
            size_t scan_pos = skipTieJoinDelimiters(text, pos);
            if (scan_pos >= text.size() || !isNoteChar(text[scan_pos])) {
                break;
            }
            NoteFeature next_note;
            if (!parseNote(text, scan_pos, next_note, tune, voice_id)) {
                break;
            }
            if (next_note.midi_note != note.midi_note) {
                break;
            }
            note.duration = note.duration.add(next_note.duration);
            pos = scan_pos;
            if (pos < text.size() && text[pos] == '-') {
                note.is_tied = true;
                ++pos;
            } else {
                note.is_tied = false;
            }
        }
        note.is_tied = false;
    }

    void parseNoteSequence(std::string_view text, Tune& tune, VoiceManager& voice_manager) {
        size_t pos = 0;
        while (pos < text.size()) {
            pos = skipWhitespace(text, pos);
            if (pos >= text.size()) {
                break;
            }

            if (text[pos] == '{') {
                (void)skipGraceGroup(text, pos);
                continue;
            }
            if (text[pos] == '[' && parseInlineBracketField(text, pos, tune, voice_manager)) {
                continue;
            }
            if (text[pos] == '"') {
                GuitarChordFeature gchord;
                if (parseGuitarChord(text, pos, gchord)) {
                    size_t next_pos = skipWhitespace(text, pos);
                    if (next_pos < text.size() && isNoteChar(text[next_pos])) {
                        NoteFeature preview;
                        size_t preview_pos = next_pos;
                        if (parseNote(text, preview_pos, preview, tune, voice_manager.current_voice)) {
                            gchord.duration = preview.duration;
                        }
                    } else {
                        auto voice_it = tune.voices.find(voice_manager.current_voice);
                        if (voice_it != tune.voices.end()) {
                            gchord.duration = voice_it->second.unit_len;
                        }
                    }
                    Feature feature;
                    feature.type = FeatureType::gchord;
                    feature.voice_id = voice_manager.current_voice;
                    feature.ts = current_time;
                    feature.line_number = current_line;
                    feature.gchord = std::move(gchord);
                    createFeature(tune, std::move(feature));
                    continue;
                }
            }
            if (text[pos] == '[') {
                ChordFeature chord;
                if (parseChord(text, pos, chord, tune, voice_manager.current_voice)) {
                    Feature feature;
                    feature.type = FeatureType::chord;
                    feature.voice_id = voice_manager.current_voice;
                    feature.ts = current_time;
                    feature.line_number = current_line;
                    feature.chord = chord;
                    createFeature(tune, std::move(feature));
                    current_time += chord.duration.toDouble();
                    continue;
                }
            }
            if (isBarLineChar(text[pos])) {
                BarLineFeature bar;
                if (parseBarLine(text, pos, bar)) {
                    resetVoiceBarAccidentals(voice_manager.current_voice);
                    Feature feature;
                    feature.type = FeatureType::bar;
                    feature.voice_id = voice_manager.current_voice;
                    feature.ts = current_time;
                    feature.line_number = current_line;
                    feature.bar = bar;
                    createFeature(tune, std::move(feature));
                    continue;
                }
            }
            if (text[pos] == 'z' || text[pos] == 'Z') {
                RestFeature rest;
                if (parseRest(text, pos, rest, tune, voice_manager.current_voice)) {
                    Feature feature;
                    feature.type = FeatureType::rest;
                    feature.voice_id = voice_manager.current_voice;
                    feature.ts = current_time;
                    feature.line_number = current_line;
                    feature.rest = rest;
                    createFeature(tune, std::move(feature));
                    current_time += rest.duration.toDouble();
                    continue;
                }
            }
            if (text[pos] == '(' || text[pos] == ')') {
                if (text[pos] == '(' && pos + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[pos + 1]))) {
                    (void)parseTupletSpecifier(text, pos, tune, voice_manager.current_voice);
                    continue;
                }
                ++pos;
                continue;
            }
            if (isNoteChar(text[pos]) || isAccidentalMarker(text[pos])) {
                NoteFeature note;
                const int32_t current_voice = voice_manager.current_voice;
                if (parseNote(text, pos, note, tune, current_voice)) {
                    bool has_broken_rhythm = false;
                    bool is_first_longer = false;
                    if (pos < text.size()) {
                        if (text[pos] == '>') {
                            has_broken_rhythm = true;
                            is_first_longer = true;
                            ++pos;
                        } else if (text[pos] == '<') {
                            has_broken_rhythm = true;
                            is_first_longer = false;
                            ++pos;
                        } else {
                            size_t lookahead = pos;
                            while (lookahead < text.size() && skipGraceGroup(text, lookahead)) {
                                lookahead = skipWhitespace(text, lookahead);
                            }
                            if (lookahead < text.size() && (text[lookahead] == '>' || text[lookahead] == '<')) {
                                has_broken_rhythm = true;
                                is_first_longer = text[lookahead] == '>';
                                pos = lookahead + 1;
                            }
                        }
                    }

                    if (has_broken_rhythm) {
                        note.duration = note.duration.mul(is_first_longer ? Fraction::init(3, 2) : Fraction::init(1, 2));
                    }
                    applyTupletToDuration(note.duration);

                    if (pos < text.size() && text[pos] == '-') {
                        note.is_tied = true;
                        ++pos;
                    }
                    mergeTiedNotes(text, pos, note, tune, current_voice);

                    Feature feature;
                    feature.type = FeatureType::note;
                    feature.voice_id = current_voice;
                    feature.ts = current_time;
                    feature.line_number = current_line;
                    feature.note = note;
                    createFeature(tune, std::move(feature));
                    current_time += note.duration.toDouble();

                    if (has_broken_rhythm) {
                        pos = skipWhitespace(text, pos);
                        while (pos < text.size() && skipGraceGroup(text, pos)) {
                            pos = skipWhitespace(text, pos);
                        }
                        if (pos < text.size() && isNoteChar(text[pos])) {
                            NoteFeature next_note;
                            if (parseNote(text, pos, next_note, tune, current_voice)) {
                                next_note.duration = next_note.duration.mul(is_first_longer ? Fraction::init(1, 2) : Fraction::init(3, 2));
                                applyTupletToDuration(next_note.duration);
                                if (pos < text.size() && text[pos] == '-') {
                                    next_note.is_tied = true;
                                    ++pos;
                                }
                                mergeTiedNotes(text, pos, next_note, tune, current_voice);

                                Feature next_feature;
                                next_feature.type = FeatureType::note;
                                next_feature.voice_id = current_voice;
                                next_feature.ts = current_time;
                                next_feature.line_number = current_line;
                                next_feature.note = next_note;
                                createFeature(tune, std::move(next_feature));
                                current_time += next_note.duration.toDouble();
                            }
                        }
                    }
                    continue;
                }
            }

            ++pos;
        }
    }
};

struct ABCParser {
    ParseState state = ParseState::header;
    size_t current_line = 0;
    VoiceManager voice_manager;
    MusicParser music_parser;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void reset() {
        state = ParseState::header;
        current_line = 0;
        voice_manager.reset();
        music_parser.reset();
        errors.clear();
        warnings.clear();
    }

    void addWarning(std::string text) {
        warnings.push_back(std::move(text));
    }

    void parseHeaderLine(std::string_view line, Tune& tune) {
        if (line.size() < 2) {
            return;
        }
        const char field = line[0];
        const std::string value = trimCopy(line.substr(2));

        switch (field) {
        case 'T': appendStringField(tune.title, value); break;
        case 'H': appendStringField(tune.history, value); break;
        case 'C': appendStringField(tune.composer, value); break;
        case 'O': appendStringField(tune.origin, value); break;
        case 'R': appendStringField(tune.rhythm, value); break;
        case 'N': appendStringField(tune.notes, value); break;
        case 'W': appendStringField(tune.words, value); break;
        case 'w': appendStringField(tune.aligned_words, value); break;
        case 'M': {
            if (value == "C" || value == "c") {
                tune.default_timesig = TimeSig{4, 4};
            } else if (value == "C|" || value == "c|") {
                tune.default_timesig = TimeSig{2, 2};
            } else {
                const size_t slash = value.find('/');
                if (slash != std::string::npos) {
                    tune.default_timesig.num = static_cast<uint8_t>(std::atoi(value.substr(0, slash).c_str()));
                    tune.default_timesig.denom = static_cast<uint8_t>(std::atoi(value.substr(slash + 1).c_str()));
                }
            }
            break;
        }
        case 'L': {
            const size_t slash = value.find('/');
            if (slash != std::string::npos) {
                tune.default_unit = Fraction::init(
                    std::atoi(value.substr(0, slash).c_str()),
                    std::atoi(value.substr(slash + 1).c_str()));
            }
            break;
        }
        case 'Q': {
            size_t eq = value.find('=');
            const std::string tempo_value = trimCopy(eq == std::string::npos ? std::string_view(value) : std::string_view(value).substr(eq + 1));
            if (!tempo_value.empty()) {
                tune.default_tempo.bpm = std::atoi(tempo_value.c_str());
            }
            break;
        }
        case 'K':
            tune.default_key = parseKeySig(value);
            break;
        case 'V': {
            const size_t first_space = value.find(' ');
            const std::string identifier = first_space == std::string::npos ? value : value.substr(0, first_space);
            if (!identifier.empty()) {
                voice_manager.getOrCreateVoice(identifier, tune);
            }
            break;
        }
        default:
            break;
        }
    }

    void parseMidiDirective(std::string_view rest, Tune& tune) {
        const std::string args = trimCopy(rest);
        if (args.empty()) {
            return;
        }

        const size_t split = args.find_first_of(" \t");
        const std::string subcmd = split == std::string::npos ? args : args.substr(0, split);
        const std::string value = split == std::string::npos ? std::string() : trimCopy(std::string_view(args).substr(split + 1));

        VoiceContext* voice = nullptr;
        auto voice_it = tune.voices.find(voice_manager.current_voice);
        if (voice_it != tune.voices.end()) {
            voice = &voice_it->second;
        }

        auto ieq = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
            }
            return true;
        };

        if (ieq(subcmd, "program")) {
            const int program = std::atoi(value.c_str());
            if (program >= 0 && program <= 127) {
                if (voice) voice->instrument = static_cast<uint8_t>(program);
                else tune.default_instrument = static_cast<uint8_t>(program);
            }
        } else if (ieq(subcmd, "channel")) {
            const int channel = std::atoi(value.c_str());
            if (channel >= 1 && channel <= 16) {
                if (voice) voice->channel = static_cast<int8_t>(channel - 1);
                else tune.default_channel = static_cast<int8_t>(channel - 1);
            }
        } else if (ieq(subcmd, "transpose")) {
            if (voice) voice->transpose = static_cast<int8_t>(std::atoi(value.c_str()));
        } else if (ieq(subcmd, "velocity") || ieq(subcmd, "volume")) {
            const int velocity = std::atoi(value.c_str());
            if (voice && velocity >= 0 && velocity <= 127) {
                voice->velocity = static_cast<uint8_t>(velocity);
            }
        } else if (ieq(subcmd, "drum") || ieq(subcmd, "percussion")) {
            const bool on = value.empty() || ieq(value, "on") || ieq(value, "1");
            if (voice) {
                voice->percussion = on;
                if (on) voice->channel = 9;
            } else {
                tune.default_percussion = on;
                if (on) tune.default_channel = 9;
            }
        }
    }

    bool parse(std::string_view abc_content, Tune& tune) {
        reset();
        char last_field = 0;
        const std::string expanded = expandABCRepeats(abc_content);
        const std::vector<std::string> lines = splitLines(expanded);

        for (const std::string& raw_line : lines) {
            ++current_line;
            const std::string trimmed = trimCopy(raw_line);
            if (trimmed.empty()) {
                continue;
            }

            if (trimmed.rfind("%%MIDI", 0) == 0) {
                parseMidiDirective(std::string_view(trimmed).substr(6), tune);
                continue;
            }
            if (!trimmed.empty() && trimmed[0] == '%') {
                continue;
            }

            std::string clean_line = trimmed;
            const size_t comment = trimmed.find('%');
            if (comment != std::string::npos) {
                clean_line = trimCopy(std::string_view(trimmed).substr(0, comment));
            }
            if (clean_line.empty()) {
                continue;
            }

            if (clean_line.size() >= 2 && clean_line[0] == '+' && clean_line[1] == ':') {
                if (last_field != 0 && isStringField(last_field)) {
                    parseHeaderLine(std::string(1, last_field) + ":" + trimCopy(std::string_view(clean_line).substr(2)), tune);
                } else if (last_field != 0) {
                    addWarning("Ignoring +: continuation for non-string field");
                } else {
                    addWarning("Ignoring +: continuation with no previous field");
                }
                continue;
            }

            if (isHeaderField(clean_line)) {
                last_field = clean_line[0];
            }

            if (state == ParseState::header && isHeaderField(clean_line)) {
                parseHeaderLine(clean_line, tune);
                if (clean_line[0] == 'K') {
                    state = ParseState::body;
                }
                continue;
            }

            if (state == ParseState::header) {
                state = ParseState::body;
            }

            if (state == ParseState::body) {
                if (tune.voices.empty()) {
                    voice_manager.switchToVoice("1", tune);
                }
                music_parser.current_line = current_line;
                music_parser.parseNoteSequence(clean_line, tune, voice_manager);
            }
        }

        if (tune.voices.empty()) {
            VoiceContext default_voice;
            default_voice.id = 1;
            default_voice.name = "1";
            voice_manager.initializeVoiceFromDefaults(default_voice, tune);
            tune.voices.emplace(1, default_voice);
        }

        state = ParseState::complete;
        return errors.empty();
    }
};

bool playChordsEnabled() {
    return std::getenv("ED_PLAY_CHORDS") != nullptr;
}

std::vector<int8_t> chordIntervals(std::string_view chord_type) {
    if (chord_type == "minor") return { 0, 3, 7 };
    if (chord_type == "dom7") return { 0, 4, 7, 10 };
    if (chord_type == "maj7") return { 0, 4, 7, 11 };
    if (chord_type == "m7") return { 0, 3, 7, 10 };
    if (chord_type == "dim") return { 0, 3, 6 };
    if (chord_type == "aug") return { 0, 4, 8 };
    return { 0, 4, 7 };
}

double durationToBeats(Fraction duration, const VoiceContext& voice) {
    return duration.toDouble() * static_cast<double>(voice.timesig.denom);
}

double timestampToBeats(double ts, const VoiceContext& voice) {
    return ts * static_cast<double>(voice.timesig.denom);
}

enum class TrackType {
    notes,
    tempo,
};

enum class MIDIEventType {
    note_on,
    note_off,
    program_change,
    control_change,
    meta_tempo,
    meta_time_signature,
    meta_key_signature,
    meta_text,
    meta_end_of_track,
};

struct MIDIEvent {
    MIDIEventType type = MIDIEventType::note_on;
    double timestamp = 0.0;
    int8_t channel = -1;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    std::vector<uint8_t> meta_data;
};

struct MIDITrack {
    int32_t track_number = 0;
    TrackType type = TrackType::notes;
    int32_t voice_number = 0;
    std::string name;
    int8_t channel = -1;
    std::vector<MIDIEvent> events;
};

struct ActiveMIDINote {
    uint8_t midi_note = 0;
    int8_t channel = -1;
    uint8_t velocity = 0;
    double end_time = 0.0;
};

struct ChannelManager {
    std::array<bool, 16> channels_in_use{};
    std::map<int32_t, int8_t> voice_to_channel;
    int8_t next_available_channel = 0;

    void reset() {
        channels_in_use.fill(false);
        voice_to_channel.clear();
        next_available_channel = 0;
    }

    int8_t assignChannel(int32_t voice_id) {
        const auto it = voice_to_channel.find(voice_id);
        if (it != voice_to_channel.end()) {
            return it->second;
        }

        while (next_available_channel < 16) {
            if (next_available_channel != 9 && !channels_in_use[static_cast<size_t>(next_available_channel)]) {
                const int8_t channel = next_available_channel;
                channels_in_use[static_cast<size_t>(channel)] = true;
                voice_to_channel[voice_id] = channel;
                ++next_available_channel;
                return channel;
            }
            ++next_available_channel;
        }

        return 0;
    }

    void assignExplicitChannel(int32_t voice_id, int8_t channel) {
        channels_in_use[static_cast<size_t>(channel)] = true;
        voice_to_channel[voice_id] = channel;
    }
};

struct MIDIGenerator {
    int32_t ticks_per_quarter = 480;
    int32_t default_tempo = 120;
    uint8_t default_velocity = 80;
    double current_time = 0.0;
    int32_t current_tempo = 120;
    ChannelManager channel_manager;
    std::vector<ActiveMIDINote> active_notes;

    void addNoteOn(uint8_t midi_note, uint8_t velocity, int8_t channel, double timestamp, MIDITrack& track) {
        track.events.push_back(MIDIEvent{ MIDIEventType::note_on, timestamp, channel, midi_note, velocity, {} });
    }

    void addNoteOff(uint8_t midi_note, int8_t channel, double timestamp, MIDITrack& track) {
        track.events.push_back(MIDIEvent{ MIDIEventType::note_off, timestamp, channel, midi_note, 0, {} });
    }

    void addProgramChange(uint8_t program, int8_t channel, double timestamp, MIDITrack& track) {
        track.events.push_back(MIDIEvent{ MIDIEventType::program_change, timestamp, channel, program, 0, {} });
    }

    void addTempo(int32_t bpm, double timestamp, MIDITrack& track) {
        MIDIEvent event;
        event.type = MIDIEventType::meta_tempo;
        event.timestamp = timestamp;
        const uint32_t mpq = static_cast<uint32_t>(60000000 / std::max(1, bpm));
        event.meta_data.push_back(static_cast<uint8_t>((mpq >> 16) & 0xff));
        event.meta_data.push_back(static_cast<uint8_t>((mpq >> 8) & 0xff));
        event.meta_data.push_back(static_cast<uint8_t>(mpq & 0xff));
        track.events.push_back(std::move(event));
    }

    void addTimeSignature(uint8_t num, uint8_t denom, double timestamp, MIDITrack& track) {
        MIDIEvent event;
        event.type = MIDIEventType::meta_time_signature;
        event.timestamp = timestamp;
        uint8_t denom_power = 0;
        uint8_t temp = denom;
        while (temp > 1) {
            temp /= 2;
            ++denom_power;
        }
        event.meta_data = { num, denom_power, 24, 8 };
        track.events.push_back(std::move(event));
    }

    void addKeySignature(int8_t sharps, bool is_major, double timestamp, MIDITrack& track) {
        MIDIEvent event;
        event.type = MIDIEventType::meta_key_signature;
        event.timestamp = timestamp;
        event.meta_data.push_back(static_cast<uint8_t>(sharps));
        event.meta_data.push_back(is_major ? 0 : 1);
        track.events.push_back(std::move(event));
    }

    void addText(std::string_view text, double timestamp, MIDITrack& track) {
        MIDIEvent event;
        event.type = MIDIEventType::meta_text;
        event.timestamp = timestamp;
        event.meta_data.assign(text.begin(), text.end());
        track.events.push_back(std::move(event));
    }

    void addEndOfTrack(double timestamp, MIDITrack& track) {
        track.events.push_back(MIDIEvent{ MIDIEventType::meta_end_of_track, timestamp, 0, 0, 0, {} });
    }

    void processActiveNotes(double at_time, MIDITrack& track) {
        size_t index = 0;
        while (index < active_notes.size()) {
            const ActiveMIDINote note = active_notes[index];
            if (note.end_time <= at_time) {
                addNoteOff(note.midi_note, note.channel, note.end_time, track);
                active_notes.erase(active_notes.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                ++index;
            }
        }
    }

    void flushActiveNotes(MIDITrack& track) {
        for (const ActiveMIDINote& note : active_notes) {
            addNoteOff(note.midi_note, note.channel, note.end_time, track);
        }
        active_notes.clear();
    }

    void scheduleNoteOn(uint8_t midi_note, uint8_t velocity, int8_t channel, double timestamp, MIDITrack& track) {
        addNoteOn(midi_note, velocity, channel, timestamp, track);
    }

    void scheduleNoteOff(uint8_t midi_note, int8_t channel, double timestamp) {
        active_notes.push_back(ActiveMIDINote{ midi_note, channel, 0, timestamp });
    }

    void createTracks(const Tune& tune, std::vector<MIDITrack>& tracks) {
        MIDITrack tempo_track;
        tempo_track.track_number = 0;
        tempo_track.type = TrackType::tempo;
        tempo_track.name = "Tempo Track";
        tempo_track.channel = -1;
        tracks.push_back(std::move(tempo_track));

        int32_t track_number = 1;
        for (const auto& entry : tune.voices) {
            MIDITrack track;
            track.track_number = track_number++;
            track.type = TrackType::notes;
            track.voice_number = entry.second.id;
            track.name = entry.second.name.empty() ? "Voice" : entry.second.name;
            tracks.push_back(std::move(track));
        }
    }

    void assignChannels(const Tune& tune, std::vector<MIDITrack>& tracks) {
        for (MIDITrack& track : tracks) {
            if (track.track_number == 0) continue;
            const auto voice_it = tune.voices.find(track.voice_number);
            if (voice_it != tune.voices.end() && voice_it->second.channel >= 0) {
                track.channel = voice_it->second.channel;
                channel_manager.assignExplicitChannel(track.voice_number, track.channel);
            }
        }
        for (MIDITrack& track : tracks) {
            if (track.track_number == 0) continue;
            if (track.channel < 0) {
                track.channel = channel_manager.assignChannel(track.voice_number);
            }
        }
        for (MIDITrack& track : tracks) {
            if (track.track_number == 0) continue;
            const auto voice_it = tune.voices.find(track.voice_number);
            if (voice_it != tune.voices.end()) {
                addProgramChange(voice_it->second.instrument, track.channel, 0.0, track);
            }
        }
    }

    void processFeature(const Tune& tune, const Feature& feature, MIDITrack& track, double& max_end_time) {
        const auto voice_it = tune.voices.find(track.voice_number);
        if (voice_it == tune.voices.end()) {
            return;
        }
        const VoiceContext& voice = voice_it->second;
        const double timestamp = timestampToBeats(feature.ts, voice);

        switch (feature.type) {
        case FeatureType::note: {
            processActiveNotes(timestamp, track);
            scheduleNoteOn(feature.note.midi_note, feature.note.velocity, track.channel, timestamp, track);
            const double note_off_time = timestamp + durationToBeats(feature.note.duration, voice);
            scheduleNoteOff(feature.note.midi_note, track.channel, note_off_time);
            max_end_time = std::max(max_end_time, note_off_time);
            break;
        }
        case FeatureType::rest: {
            const double rest_end = timestamp + durationToBeats(feature.rest.duration, voice);
            processActiveNotes(rest_end, track);
            max_end_time = std::max(max_end_time, rest_end);
            break;
        }
        case FeatureType::chord: {
            processActiveNotes(timestamp, track);
            for (const NoteFeature& note : feature.chord.notes) {
                scheduleNoteOn(note.midi_note, note.velocity, track.channel, timestamp, track);
                const double note_off_time = timestamp + durationToBeats(feature.chord.duration, voice);
                scheduleNoteOff(note.midi_note, track.channel, note_off_time);
                max_end_time = std::max(max_end_time, note_off_time);
            }
            break;
        }
        case FeatureType::gchord: {
            const double note_off_time = timestamp + durationToBeats(feature.gchord.duration, voice);
            if (!playChordsEnabled()) {
                max_end_time = std::max(max_end_time, note_off_time);
                break;
            }
            processActiveNotes(timestamp, track);
            for (int8_t interval : chordIntervals(feature.gchord.chord_type)) {
                const int midi_value = std::max(0, std::min(127, static_cast<int>(feature.gchord.root_note) + interval + voice.transpose));
                const uint8_t midi_note = static_cast<uint8_t>(midi_value);
                scheduleNoteOn(midi_note, voice.velocity, track.channel, timestamp, track);
                scheduleNoteOff(midi_note, track.channel, note_off_time);
            }
            max_end_time = std::max(max_end_time, note_off_time);
            break;
        }
        default:
            break;
        }

        current_time = std::max(current_time, timestamp);
    }

    void generateTrackEvents(const Tune& tune, MIDITrack& track) {
        if (track.track_number == 0) {
            return;
        }
        current_time = 0.0;
        double max_end_time = 0.0;

        for (const Feature& feature : tune.features) {
            if (feature.voice_id == track.voice_number) {
                processFeature(tune, feature, track, max_end_time);
            }
        }

        flushActiveNotes(track);
        addEndOfTrack(std::max(current_time, max_end_time), track);
    }

    void processTempoTrack(const Tune& tune, MIDITrack& track) {
        addTempo(tune.default_tempo.bpm, 0.0, track);
        addTimeSignature(tune.default_timesig.num, tune.default_timesig.denom, 0.0, track);
        addKeySignature(tune.default_key.sharps, tune.default_key.is_major, 0.0, track);
        if (!tune.title.empty()) {
            addText(tune.title, 0.0, track);
        }

        double max_end_time = 0.0;
        for (const Feature& feature : tune.features) {
            const auto voice_it = tune.voices.find(feature.voice_id);
            const double ts_beats = voice_it != tune.voices.end()
                ? timestampToBeats(feature.ts, voice_it->second)
                : feature.ts * static_cast<double>(tune.default_timesig.denom);

            switch (feature.type) {
            case FeatureType::tempo: addTempo(feature.tempo.bpm, ts_beats, track); break;
            case FeatureType::time: addTimeSignature(feature.time.num, feature.time.denom, ts_beats, track); break;
            case FeatureType::key: addKeySignature(feature.key.sharps, feature.key.is_major, ts_beats, track); break;
            default: break;
            }

            if (voice_it != tune.voices.end()) {
                const VoiceContext& voice = voice_it->second;
                switch (feature.type) {
                case FeatureType::note: max_end_time = std::max(max_end_time, ts_beats + durationToBeats(feature.note.duration, voice)); break;
                case FeatureType::rest: max_end_time = std::max(max_end_time, ts_beats + durationToBeats(feature.rest.duration, voice)); break;
                case FeatureType::chord: max_end_time = std::max(max_end_time, ts_beats + durationToBeats(feature.chord.duration, voice)); break;
                case FeatureType::gchord: max_end_time = std::max(max_end_time, ts_beats + durationToBeats(feature.gchord.duration, voice)); break;
                default: break;
                }
            }
        }

        addEndOfTrack(max_end_time, track);
    }

    bool generateMIDI(const Tune& tune, std::vector<MIDITrack>& tracks) {
        channel_manager.reset();
        current_time = 0.0;
        current_tempo = tune.default_tempo.bpm;
        active_notes.clear();

        createTracks(tune, tracks);
        assignChannels(tune, tracks);
        for (MIDITrack& track : tracks) {
            generateTrackEvents(tune, track);
        }
        if (!tracks.empty()) {
            processTempoTrack(tune, tracks[0]);
        }
        return true;
    }
};

uint8_t trackEventOrder(const MIDIEvent& event) {
    switch (event.type) {
    case MIDIEventType::meta_tempo: return 0;
    case MIDIEventType::note_off: return 1;
    case MIDIEventType::control_change: return 2;
    case MIDIEventType::program_change: return 3;
    case MIDIEventType::note_on: return 4;
    default: return 5;
    }
}

struct TempoSegment {
    double start_beat = 0.0;
    double start_ns = 0.0;
    double ns_per_beat = 0.0;
};

struct ScheduledEvent {
    uint64_t time_ns = 0;
    uint8_t source_channel = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t order = 0;
};

int32_t extractTempoBpm(const MIDIEvent& event, int32_t fallback_bpm) {
    if (event.type != MIDIEventType::meta_tempo || event.meta_data.size() < 3) {
        return fallback_bpm;
    }
    const uint32_t mpq = (static_cast<uint32_t>(event.meta_data[0]) << 16) |
        (static_cast<uint32_t>(event.meta_data[1]) << 8) |
        static_cast<uint32_t>(event.meta_data[2]);
    return mpq == 0 ? fallback_bpm : std::max(1, static_cast<int32_t>(60000000 / mpq));
}

std::vector<TempoSegment> buildTempoSegments(int32_t default_bpm, const MIDITrack* tempo_track) {
    std::vector<TempoSegment> segments;
    int32_t current_bpm = std::max(1, default_bpm);
    double current_start_beat = 0.0;
    double current_start_ns = 0.0;

    if (tempo_track) {
        for (const MIDIEvent& event : tempo_track->events) {
            if (event.type != MIDIEventType::meta_tempo) {
                continue;
            }
            const double beat = std::max(0.0, event.timestamp);
            if (beat > current_start_beat) {
                segments.push_back(TempoSegment{ current_start_beat, current_start_ns, 60000000000.0 / static_cast<double>(current_bpm) });
                current_start_ns += (beat - current_start_beat) * (60000000000.0 / static_cast<double>(current_bpm));
                current_start_beat = beat;
            }
            current_bpm = extractTempoBpm(event, current_bpm);
        }
    }

    segments.push_back(TempoSegment{ current_start_beat, current_start_ns, 60000000000.0 / static_cast<double>(current_bpm) });
    return segments;
}

uint64_t beatToNs(double beat, const std::vector<TempoSegment>& segments) {
    TempoSegment chosen = segments.front();
    for (const TempoSegment& segment : segments) {
        if (segment.start_beat > beat) {
            break;
        }
        chosen = segment;
    }
    const double ns = chosen.start_ns + (beat - chosen.start_beat) * chosen.ns_per_beat;
    return static_cast<uint64_t>(std::max(0.0, ns));
}

uint16_t collectChannelMask(const std::vector<ScheduledEvent>& events) {
    uint16_t mask = 0;
    for (const ScheduledEvent& event : events) {
        if (event.source_channel < 16) {
            mask |= static_cast<uint16_t>(1u << event.source_channel);
        }
    }
    return mask;
}

std::vector<ScheduledEvent> flattenTracks(std::vector<MIDITrack>& tracks, int32_t default_bpm) {
    const std::vector<TempoSegment> segments = buildTempoSegments(default_bpm, tracks.empty() ? nullptr : &tracks[0]);
    std::vector<ScheduledEvent> events;

    for (MIDITrack& track : tracks) {
        std::sort(track.events.begin(), track.events.end(), [](const MIDIEvent& a, const MIDIEvent& b) {
            if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
            return trackEventOrder(a) < trackEventOrder(b);
        });

        for (const MIDIEvent& event : track.events) {
            if (event.channel < 0 || event.channel > 15) {
                continue;
            }

            std::optional<uint8_t> status;
            switch (event.type) {
            case MIDIEventType::note_on: status = 0x90; break;
            case MIDIEventType::note_off: status = 0x80; break;
            case MIDIEventType::program_change: status = 0xC0; break;
            case MIDIEventType::control_change: status = 0xB0; break;
            default: break;
            }
            if (!status.has_value()) {
                continue;
            }

            events.push_back(ScheduledEvent{
                beatToNs(std::max(0.0, event.timestamp), segments),
                static_cast<uint8_t>(event.channel),
                *status,
                event.data1,
                event.data2,
                trackEventOrder(event),
            });
        }
    }

    std::sort(events.begin(), events.end(), [](const ScheduledEvent& a, const ScheduledEvent& b) {
        if (a.time_ns != b.time_ns) return a.time_ns < b.time_ns;
        if (a.order != b.order) return a.order < b.order;
        if (a.source_channel != b.source_channel) return a.source_channel < b.source_channel;
        if (a.status != b.status) return a.status < b.status;
        if (a.data1 != b.data1) return a.data1 < b.data1;
        return a.data2 < b.data2;
    });

    return events;
}

void writeBigEndian32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void writeBigEndian16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

void writeVarLen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t buffer[4];
    size_t index = 0;
    buffer[index] = static_cast<uint8_t>(value & 0x7f);
    value >>= 7;
    while (value > 0) {
        ++index;
        buffer[index] = static_cast<uint8_t>((value & 0x7f) | 0x80);
        value >>= 7;
    }
    do {
        out.push_back(buffer[index]);
    } while (index-- > 0);
}

std::vector<uint8_t> writeMIDIFile(std::vector<MIDITrack> tracks, int32_t ticks_per_quarter) {
    std::vector<uint8_t> output;
    output.insert(output.end(), { 'M', 'T', 'h', 'd' });
    writeBigEndian32(output, 6);
    writeBigEndian16(output, 1);
    writeBigEndian16(output, static_cast<uint16_t>(tracks.size()));
    writeBigEndian16(output, static_cast<uint16_t>(ticks_per_quarter));

    for (MIDITrack& track : tracks) {
        std::sort(track.events.begin(), track.events.end(), [](const MIDIEvent& a, const MIDIEvent& b) {
            return a.timestamp < b.timestamp;
        });

        std::vector<uint8_t> track_data;
        double last_time = 0.0;
        for (const MIDIEvent& event : track.events) {
            const int32_t delta = std::max(0, static_cast<int32_t>((event.timestamp - last_time) * static_cast<double>(ticks_per_quarter)));
            writeVarLen(track_data, static_cast<uint32_t>(delta));

            switch (event.type) {
            case MIDIEventType::note_on:
                track_data.push_back(static_cast<uint8_t>(0x90 | static_cast<uint8_t>(event.channel)));
                track_data.push_back(event.data1);
                track_data.push_back(event.data2);
                break;
            case MIDIEventType::note_off:
                track_data.push_back(static_cast<uint8_t>(0x80 | static_cast<uint8_t>(event.channel)));
                track_data.push_back(event.data1);
                track_data.push_back(event.data2);
                break;
            case MIDIEventType::program_change:
                track_data.push_back(static_cast<uint8_t>(0xC0 | static_cast<uint8_t>(event.channel)));
                track_data.push_back(event.data1);
                break;
            case MIDIEventType::control_change:
                track_data.push_back(static_cast<uint8_t>(0xB0 | static_cast<uint8_t>(event.channel)));
                track_data.push_back(event.data1);
                track_data.push_back(event.data2);
                break;
            case MIDIEventType::meta_tempo:
                track_data.insert(track_data.end(), { 0xff, 0x51 });
                writeVarLen(track_data, static_cast<uint32_t>(event.meta_data.size()));
                track_data.insert(track_data.end(), event.meta_data.begin(), event.meta_data.end());
                break;
            case MIDIEventType::meta_time_signature:
                track_data.insert(track_data.end(), { 0xff, 0x58 });
                writeVarLen(track_data, static_cast<uint32_t>(event.meta_data.size()));
                track_data.insert(track_data.end(), event.meta_data.begin(), event.meta_data.end());
                break;
            case MIDIEventType::meta_key_signature:
                track_data.insert(track_data.end(), { 0xff, 0x59 });
                writeVarLen(track_data, static_cast<uint32_t>(event.meta_data.size()));
                track_data.insert(track_data.end(), event.meta_data.begin(), event.meta_data.end());
                break;
            case MIDIEventType::meta_text:
                track_data.insert(track_data.end(), { 0xff, 0x01 });
                writeVarLen(track_data, static_cast<uint32_t>(event.meta_data.size()));
                track_data.insert(track_data.end(), event.meta_data.begin(), event.meta_data.end());
                break;
            case MIDIEventType::meta_end_of_track:
                track_data.insert(track_data.end(), { 0xff, 0x2f, 0x00 });
                break;
            }
            last_time = event.timestamp;
        }

        output.insert(output.end(), { 'M', 'T', 'r', 'k' });
        writeBigEndian32(output, static_cast<uint32_t>(track_data.size()));
        output.insert(output.end(), track_data.begin(), track_data.end());
    }

    return output;
}

enum class ABCPlaybackState : int32_t {
    stopped = 0,
    playing = 1,
    paused = 2,
};

struct StoredMusic {
    uint32_t id = 0;
    std::string title;
    std::string composer;
    float tempo_bpm = 120.0f;
    uint16_t used_channels_mask = 0;
    std::vector<ScheduledEvent> events;
    std::vector<uint8_t> midi_blob;
};

struct Playback {
    uint32_t asset_id = 0;
    float volume = 1.0f;
    ABCPlaybackState state = ABCPlaybackState::stopped;
    size_t event_index = 0;
    uint64_t elapsed_before_pause_ns = 0;
    uint64_t start_ns = 0;
    std::array<uint8_t, 16> channel_map{};
};

struct ABCRuntime {
    std::mutex mutex;
    std::thread worker;
    bool shutdown = false;
    HMIDIOUT midi_out = nullptr;
    bool initialized = false;
    float master_volume = 1.0f;
    uint32_t next_music_id = 1;
    std::array<bool, 16> channels_in_use{};
    std::vector<StoredMusic> assets;
    std::vector<Playback> playbacks;
};

ABCRuntime g_abc;

float clampF32(float value, float min_value, float max_value) {
    return std::max(min_value, std::min(max_value, value));
}

uint64_t nowNs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

StoredMusic* runtimeFindAssetById(ABCRuntime& runtime, uint32_t id) {
    for (StoredMusic& asset : runtime.assets) {
        if (asset.id == id) {
            return &asset;
        }
    }
    return nullptr;
}

std::vector<StoredMusic>::iterator runtimeFindAssetIt(ABCRuntime& runtime, uint32_t id) {
    return std::find_if(runtime.assets.begin(), runtime.assets.end(), [id](const StoredMusic& asset) {
        return asset.id == id;
    });
}

void sendShortMessage(ABCRuntime& runtime, uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) {
    if (!runtime.midi_out) {
        return;
    }
    const DWORD message = static_cast<DWORD>(status | (channel & 0x0f)) |
        (static_cast<DWORD>(data1) << 8u) |
        (static_cast<DWORD>(data2) << 16u);
    (void)midiOutShortMsg(runtime.midi_out, message);
}

void sendAllNotesOff(ABCRuntime& runtime, uint8_t channel) {
    sendShortMessage(runtime, 0xB0, channel, 123, 0);
    sendShortMessage(runtime, 0xB0, channel, 120, 0);
    sendShortMessage(runtime, 0xB0, channel, 64, 0);
}

uint8_t playbackChannelVolume(const ABCRuntime& runtime, const Playback& playback) {
    const float combined = clampF32(runtime.master_volume * playback.volume, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(combined * 127.0f));
}

void refreshPlaybackChannelVolumes(ABCRuntime& runtime, const Playback& playback) {
    const uint8_t volume = playbackChannelVolume(runtime, playback);
    for (uint8_t mapped : playback.channel_map) {
        if (mapped == 0xff) {
            continue;
        }
        sendShortMessage(runtime, 0xB0, mapped, 7, volume);
        sendShortMessage(runtime, 0xB0, mapped, 11, 127);
    }
}

void releasePlaybackChannels(ABCRuntime& runtime, const Playback& playback) {
    for (uint8_t mapped : playback.channel_map) {
        if (mapped == 0xff) {
            continue;
        }
        sendAllNotesOff(runtime, mapped);
        runtime.channels_in_use[mapped] = false;
    }
}

bool allocatePlaybackChannels(ABCRuntime& runtime, const StoredMusic& asset, std::array<uint8_t, 16>& out_map) {
    out_map.fill(0xff);

    if ((asset.used_channels_mask & static_cast<uint16_t>(1u << 9)) != 0) {
        if (runtime.channels_in_use[9]) {
            return false;
        }
        runtime.channels_in_use[9] = true;
        out_map[9] = 9;
    }

    std::vector<uint8_t> reserved;
    for (uint32_t source_channel = 0; source_channel < 16; ++source_channel) {
        if (source_channel == 9) {
            continue;
        }
        const uint16_t bit = static_cast<uint16_t>(1u << source_channel);
        if ((asset.used_channels_mask & bit) == 0) {
            continue;
        }

        std::optional<uint8_t> found;
        for (uint32_t candidate = 0; candidate < 16; ++candidate) {
            if (candidate == 9) {
                continue;
            }
            if (!runtime.channels_in_use[candidate]) {
                found = static_cast<uint8_t>(candidate);
                break;
            }
        }

        if (!found.has_value()) {
            if (out_map[9] != 0xff) {
                runtime.channels_in_use[9] = false;
            }
            for (uint8_t channel : reserved) {
                runtime.channels_in_use[channel] = false;
            }
            out_map.fill(0xff);
            return false;
        }

        runtime.channels_in_use[*found] = true;
        out_map[source_channel] = *found;
        reserved.push_back(*found);
    }

    return true;
}

bool initABCRuntimeLocked(ABCRuntime& runtime);
void abcWorkerMain();

bool initABCRuntimeLocked(ABCRuntime& runtime) {
    if (runtime.initialized) {
        return true;
    }

    HMIDIOUT handle = nullptr;
    if (midiOutOpen(&handle, MIDI_MAPPER, 0, 0, 0) != MMSYSERR_NOERROR) {
        wingui_set_last_error_string_internal("wingui_abc_init: midiOutOpen failed");
        return false;
    }

    runtime.midi_out = handle;
    runtime.initialized = true;
    runtime.shutdown = false;
    if (!runtime.worker.joinable()) {
        runtime.worker = std::thread(abcWorkerMain);
    }
    wingui_clear_last_error_internal();
    return true;
}

void stopAllLocked(ABCRuntime& runtime) {
    if (runtime.midi_out) {
        (void)midiOutReset(runtime.midi_out);
    }
    for (bool& channel : runtime.channels_in_use) {
        channel = false;
    }
    runtime.playbacks.clear();
}

StoredMusic compileMusicAsset(uint32_t id, std::string_view abc_text) {
    ABCParser parser;
    Tune tune;
    if (!parser.parse(abc_text, tune)) {
        wingui_set_last_error_string_internal("wingui_abc: parse failed");
        return {};
    }

    MIDIGenerator generator;
    std::vector<MIDITrack> tracks;
    (void)generator.generateMIDI(tune, tracks);

    StoredMusic asset;
    asset.id = id;
    asset.title = tune.title;
    asset.composer = tune.composer;
    asset.tempo_bpm = static_cast<float>(tune.default_tempo.bpm);
    asset.events = flattenTracks(tracks, tune.default_tempo.bpm);
    asset.used_channels_mask = collectChannelMask(asset.events);
    asset.midi_blob = writeMIDIFile(tracks, generator.ticks_per_quarter);
    wingui_clear_last_error_internal();
    return asset;
}

void dispatchDueEventsLocked(ABCRuntime& runtime) {
    const uint64_t current_ns = nowNs();
    size_t index = 0;
    while (index < runtime.playbacks.size()) {
        bool should_remove = false;
        Playback& playback = runtime.playbacks[index];
        if (playback.state == ABCPlaybackState::paused) {
            ++index;
            continue;
        }

        StoredMusic* asset = runtimeFindAssetById(runtime, playback.asset_id);
        if (!asset) {
            should_remove = true;
        } else {
            while (playback.event_index < asset->events.size()) {
                const ScheduledEvent& event = asset->events[playback.event_index];
                const uint64_t deadline = playback.start_ns + event.time_ns;
                if (deadline > current_ns) {
                    break;
                }

                const uint8_t mapped = playback.channel_map[event.source_channel];
                if (mapped != 0xff) {
                    uint8_t velocity = event.data2;
                    if ((event.status & 0xF0) == 0x90 && velocity != 0) {
                        const float scaled = static_cast<float>(velocity) * clampF32(runtime.master_volume * playback.volume, 0.0f, 1.0f);
                        velocity = static_cast<uint8_t>(std::lround(clampF32(scaled, 0.0f, 127.0f)));
                    }
                    sendShortMessage(runtime, event.status, mapped, event.data1, velocity);
                }
                ++playback.event_index;
            }

            if (playback.event_index >= asset->events.size()) {
                should_remove = true;
            }
        }

        if (should_remove) {
            Playback finished = runtime.playbacks[index];
            runtime.playbacks.erase(runtime.playbacks.begin() + static_cast<std::ptrdiff_t>(index));
            releasePlaybackChannels(runtime, finished);
            continue;
        }

        ++index;
    }
}

void abcWorkerMain() {
    while (true) {
        g_abc.mutex.lock();
        const bool shutting_down = g_abc.shutdown;
        if (!shutting_down) {
            dispatchDueEventsLocked(g_abc);
        }
        g_abc.mutex.unlock();

        if (shutting_down) {
            break;
        }

        std::this_thread::sleep_for(g_abc.playbacks.empty() ? std::chrono::milliseconds(10) : std::chrono::milliseconds(1));
    }
}

uint32_t addAssetLocked(ABCRuntime& runtime, std::string_view abc_text) {
    const uint32_t id = runtime.next_music_id++;
    StoredMusic asset = compileMusicAsset(id, abc_text);
    if (asset.id == 0) {
        return 0;
    }
    runtime.assets.push_back(std::move(asset));
    return id;
}

bool startPlaybackLocked(ABCRuntime& runtime, uint32_t asset_id, float volume) {
    StoredMusic* asset = runtimeFindAssetById(runtime, asset_id);
    if (!asset) {
        wingui_set_last_error_string_internal("wingui_abc_play: asset id was not found");
        return false;
    }
    if (!initABCRuntimeLocked(runtime)) {
        return false;
    }

    std::array<uint8_t, 16> channel_map{};
    if (!allocatePlaybackChannels(runtime, *asset, channel_map)) {
        wingui_set_last_error_string_internal("wingui_abc_play: no free MIDI channels available");
        return false;
    }

    Playback playback;
    playback.asset_id = asset_id;
    playback.volume = clampF32(volume, 0.0f, 1.5f);
    playback.state = ABCPlaybackState::playing;
    playback.event_index = 0;
    playback.elapsed_before_pause_ns = 0;
    playback.start_ns = nowNs();
    playback.channel_map = channel_map;
    runtime.playbacks.push_back(playback);
    refreshPlaybackChannelVolumes(runtime, runtime.playbacks.back());
    wingui_clear_last_error_internal();
    return true;
}

void shutdownABCRuntimeLocked(ABCRuntime& runtime) {
    if (!runtime.initialized) {
        return;
    }

    runtime.shutdown = true;
    std::thread worker = std::move(runtime.worker);
    runtime.mutex.unlock();
    if (worker.joinable()) {
        worker.join();
    }
    runtime.mutex.lock();

    stopAllLocked(runtime);
    if (runtime.midi_out) {
        (void)midiOutClose(runtime.midi_out);
        runtime.midi_out = nullptr;
    }
    runtime.initialized = false;
    runtime.shutdown = false;
}

} // namespace

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_init(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return initABCRuntimeLocked(g_abc) ? 1 : 0;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_shutdown(void) {
    std::unique_lock<std::mutex> lock(g_abc.mutex);
    shutdownABCRuntimeLocked(g_abc);
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_abc_load_utf8(const char* abc_text_utf8) {
    if (!abc_text_utf8 || !*abc_text_utf8) {
        wingui_set_last_error_string_internal("wingui_abc_load_utf8: abc text was empty");
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return addAssetLocked(g_abc, abc_text_utf8);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_play_utf8(const char* abc_text_utf8, float volume) {
    if (!abc_text_utf8 || !*abc_text_utf8) {
        wingui_set_last_error_string_internal("wingui_abc_play_utf8: abc text was empty");
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    const uint32_t id = addAssetLocked(g_abc, abc_text_utf8);
    if (id == 0) {
        return 0;
    }
    return startPlaybackLocked(g_abc, id, volume) ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_play_utf8_simple(const char* abc_text_utf8) {
    return wingui_abc_play_utf8(abc_text_utf8, 1.0f);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_play(uint32_t music_id, float volume) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return startPlaybackLocked(g_abc, music_id, volume) ? 1 : 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_play_simple(uint32_t music_id) {
    return wingui_abc_play(music_id, 1.0f);
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_stop_all(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    stopAllLocked(g_abc);
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_pause_all(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    const uint64_t current_ns = nowNs();
    for (Playback& playback : g_abc.playbacks) {
        if (playback.state != ABCPlaybackState::playing) {
            continue;
        }
        playback.elapsed_before_pause_ns = current_ns - playback.start_ns;
        playback.state = ABCPlaybackState::paused;
        for (uint8_t mapped : playback.channel_map) {
            if (mapped != 0xff) {
                sendAllNotesOff(g_abc, mapped);
            }
        }
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_resume_all(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    const uint64_t current_ns = nowNs();
    for (Playback& playback : g_abc.playbacks) {
        if (playback.state != ABCPlaybackState::paused) {
            continue;
        }
        playback.start_ns = current_ns - playback.elapsed_before_pause_ns;
        playback.state = ABCPlaybackState::playing;
        refreshPlaybackChannelVolumes(g_abc, playback);
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_set_master_volume(float volume) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    g_abc.master_volume = clampF32(volume, 0.0f, 1.0f);
    for (const Playback& playback : g_abc.playbacks) {
        refreshPlaybackChannelVolumes(g_abc, playback);
    }
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API float WINGUI_CALL wingui_abc_get_master_volume(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return g_abc.master_volume;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_free(uint32_t music_id) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);

    size_t playback_index = 0;
    while (playback_index < g_abc.playbacks.size()) {
        if (g_abc.playbacks[playback_index].asset_id == music_id) {
            Playback playback = g_abc.playbacks[playback_index];
            g_abc.playbacks.erase(g_abc.playbacks.begin() + static_cast<std::ptrdiff_t>(playback_index));
            releasePlaybackChannels(g_abc, playback);
        } else {
            ++playback_index;
        }
    }

    const auto it = runtimeFindAssetIt(g_abc, music_id);
    if (it == g_abc.assets.end()) {
        wingui_set_last_error_string_internal("wingui_abc_free: asset id was not found");
        return 0;
    }
    g_abc.assets.erase(it);
    wingui_clear_last_error_internal();
    return 1;
}

extern "C" WINGUI_API void WINGUI_CALL wingui_abc_free_all(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    stopAllLocked(g_abc);
    g_abc.assets.clear();
    wingui_clear_last_error_internal();
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_is_playing(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return g_abc.playbacks.empty() ? 0 : 1;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_is_playing_id(uint32_t music_id) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    for (const Playback& playback : g_abc.playbacks) {
        if (playback.asset_id == music_id) {
            return 1;
        }
    }
    return 0;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_state(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    bool has_paused = false;
    for (const Playback& playback : g_abc.playbacks) {
        if (playback.state == ABCPlaybackState::playing) {
            return static_cast<int32_t>(ABCPlaybackState::playing);
        }
        if (playback.state == ABCPlaybackState::paused) {
            has_paused = true;
        }
    }
    return static_cast<int32_t>(has_paused ? ABCPlaybackState::paused : ABCPlaybackState::stopped);
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_exists(uint32_t music_id) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return runtimeFindAssetById(g_abc, music_id) ? 1 : 0;
}

extern "C" WINGUI_API uint32_t WINGUI_CALL wingui_abc_count(void) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    return static_cast<uint32_t>(g_abc.assets.size());
}

extern "C" WINGUI_API float WINGUI_CALL wingui_abc_get_tempo(uint32_t music_id) {
    std::lock_guard<std::mutex> lock(g_abc.mutex);
    const StoredMusic* asset = runtimeFindAssetById(g_abc, music_id);
    return asset ? asset->tempo_bpm : 0.0f;
}

extern "C" WINGUI_API int32_t WINGUI_CALL wingui_abc_export_midi_utf8(uint32_t music_id, const char* path_utf8) {
    if (!path_utf8 || !*path_utf8) {
        wingui_set_last_error_string_internal("wingui_abc_export_midi_utf8: output path was empty");
        return 0;
    }

    std::vector<uint8_t> midi_blob;
    {
        std::lock_guard<std::mutex> lock(g_abc.mutex);
        const StoredMusic* asset = runtimeFindAssetById(g_abc, music_id);
        if (!asset) {
            wingui_set_last_error_string_internal("wingui_abc_export_midi_utf8: asset id was not found");
            return 0;
        }
        midi_blob = asset->midi_blob;
    }

    std::ofstream file(path_utf8, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        wingui_set_last_error_string_internal("wingui_abc_export_midi_utf8: failed to open output file");
        return 0;
    }
    file.write(reinterpret_cast<const char*>(midi_blob.data()), static_cast<std::streamsize>(midi_blob.size()));
    if (!file.good()) {
        wingui_set_last_error_string_internal("wingui_abc_export_midi_utf8: failed to write MIDI file");
        return 0;
    }
    wingui_clear_last_error_internal();
    return 1;
}