#include "EditorBridge.h"

#include <fstream>

#include "platform/NativeFileDialog.h"

namespace {

// Reads a small resource file (see EDITOR_RESOURCES_DIR in CMakeLists.txt)
// straight into memory -- same read-the-whole-file-at-once shape as
// PcgFile::load(), just for the couple-KB Init Program templates rather
// than a whole .PCG. Returns an empty vector if the file can't be opened;
// callers distinguish "empty" from "a real empty file" by checking size
// against the destination bank's own record size anyway (see
// PcgFile::resolveDuplicates()'s validation), so no separate ok/error
// signal is needed here.
std::vector<uint8_t> readResourceFile(const std::string& relativePath) {
    std::ifstream file(std::string(EDITOR_RESOURCES_DIR) + "/" + relativePath, std::ios::binary);
    if (!file) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string stringArg(const choc::value::ValueView& args, size_t index) {
    if (args.isArray() && args.size() > index) return args[static_cast<uint32_t>(index)].getWithDefault<std::string>({});
    return {};
}

int intArg(const choc::value::ValueView& args, size_t index, int fallback = -1) {
    if (!args.isArray() || args.size() <= index) return fallback;
    return static_cast<int>(args[static_cast<uint32_t>(index)].getWithDefault<double>(fallback));
}

bool boolArg(const choc::value::ValueView& args, size_t index, bool fallback = false) {
    if (!args.isArray() || args.size() <= index) return fallback;
    return args[static_cast<uint32_t>(index)].getWithDefault<bool>(fallback);
}

// Reads args[index] as a nested JS array of numbers (e.g. a raw record's
// bytes) into a std::vector<uint8_t>. Empty if the argument is missing or
// isn't an array -- callers (getSongRecordBytes/putSongRecordBytes) treat an
// unexpected size as a validation failure, same as any other bad input.
std::vector<uint8_t> bytesArg(const choc::value::ValueView& args, size_t index) {
    std::vector<uint8_t> result;
    if (!args.isArray() || args.size() <= index) return result;
    auto arr = args[static_cast<uint32_t>(index)];
    if (!arr.isArray()) return result;
    result.reserve(arr.size());
    for (uint32_t i = 0; i < arr.size(); ++i) result.push_back(static_cast<uint8_t>(arr[i].getWithDefault<double>(0)));
    return result;
}

// Reads args[index] as a JS array of {srcBank, srcNumber, dstBank, dstNumber?}
// objects (the user's chosen destination bank -- and, optionally, exact free
// slot -- per unresolved Program, gathered by the cross-dataset Combi copy
// panel) into std::vector<kronos::PcgFile::ProgramPlacement>. `dstNumber`
// defaults to -1 (ProgramPlacement's own "let apply() pick the first free
// slot" sentinel) when omitted, so older-shaped placement objects (bank
// only) still work unchanged. Empty if the argument is missing or isn't an
// array.
std::vector<kronos::PcgFile::ProgramPlacement> placementsArg(const choc::value::ValueView& args, size_t index) {
    std::vector<kronos::PcgFile::ProgramPlacement> result;
    if (!args.isArray() || args.size() <= index) return result;
    auto arr = args[static_cast<uint32_t>(index)];
    if (!arr.isArray()) return result;
    result.reserve(arr.size());
    for (uint32_t i = 0; i < arr.size(); ++i) {
        auto element = arr[i];
        kronos::PcgFile::ProgramPlacement placement;
        placement.srcBank = static_cast<int>(element["srcBank"].getWithDefault<double>(0));
        placement.srcNumber = static_cast<int>(element["srcNumber"].getWithDefault<double>(0));
        placement.dstBank = static_cast<int>(element["dstBank"].getWithDefault<double>(0));
        placement.dstNumber = static_cast<int>(element["dstNumber"].getWithDefault<double>(-1));
        result.push_back(placement);
    }
    return result;
}

choc::value::Value bytesToValue(const std::vector<uint8_t>& bytes) {
    return choc::value::createArray(static_cast<uint32_t>(bytes.size()),
                                     [&](uint32_t i) { return static_cast<int32_t>(bytes[i]); });
}

// Bounds-checked lookup into the `[bank][number]` counts PcgFile::setlistUsageCounts()
// returns -- a bank/number beyond what any slot referenced simply has no entry, i.e. 0 uses.
int countAt(const std::vector<std::vector<int>>& counts, int bank, int number) {
    if (bank < 0 || bank >= static_cast<int>(counts.size())) return 0;
    if (number < 0 || number >= static_cast<int>(counts[bank].size())) return 0;
    return counts[bank][number];
}

// Deliberately no fontSizeName()/timbreStatusName()/programBankTypeName()
// here anymore (removed 2026-08-15) -- this bridge sends raw enum values
// (static_cast<int>) for FontSize/TimbreStatus/ProgramBankType now, not
// formatted display strings. Per-project convention: C++ only handles raw
// data and bulk/native-speed operations; naming/formatting for display is
// an encoder/decoder-layer (JS) responsibility. This also removed a real,
// confirmed duplicate -- fontSizeName()'s exact mapping already existed
// independently in frontend/components/kronos/setlist-editor-comment-and-
// font.js (FONT_SIZE_BY_VALUE), used for the real byte-level editable path; this
// bridge's copy was only ever feeding a REAL-ONLY summary label.
// PROGRAM_BANK_TYPE_NAMES (frontend/pane.js) and a local FONT_SIZE_NAMES
// (frontend/pane-setlist-editor.js) are the new JS-side homes for the
// other two.

choc::value::Value setlistUsagesToValue(const std::vector<kronos::SetlistUsage>& usages) {
    auto result = choc::value::createEmptyArray();
    for (const auto& usage : usages) {
        auto v = choc::value::createObject("SetlistUsage");
        v.setMember("setlistIndex", usage.setlistIndex);
        v.setMember("setlistName", usage.setlistName);
        v.setMember("songIndex", usage.songIndex);
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value combiUsagesToValue(const std::vector<kronos::CombiUsage>& usages) {
    auto result = choc::value::createEmptyArray();
    for (const auto& usage : usages) {
        auto v = choc::value::createObject("CombiUsage");
        v.setMember("bank", usage.bank);
        v.setMember("number", usage.number);
        v.setMember("name", usage.name);
        v.setMember("active", usage.active);
        result.addArrayElement(v);
    }
    return result;
}

}  // namespace

choc::value::Value EditorBridge::makeOk() {
    auto v = choc::value::createObject("Result");
    v.setMember("ok", true);
    return v;
}

choc::value::Value EditorBridge::makeError(const std::string& error) {
    auto v = choc::value::createObject("Result");
    v.setMember("ok", false);
    v.setMember("error", error);
    return v;
}

// The user closing the dialog without picking a file is expected, everyday
// behavior, not an error -- callers (openFileDialog()) shouldn't log or
// display it as one. `datasetId` stays absent so the frontend's existing
// `if (!result.ok)` error-handling path is never triggered by it.
choc::value::Value EditorBridge::makeCancelled() {
    auto v = choc::value::createObject("Result");
    v.setMember("ok", true);
    v.setMember("cancelled", true);
    return v;
}

choc::value::Value EditorBridge::songToValue(const kronos::Song& song) {
    auto v = choc::value::createObject("SongEntry");
    v.setMember("index", song.index);
    v.setMember("label", song.name);
    // From SBK1 -- see docs/content/format/index.md §4.3-4.4 for how these were decoded.
    v.setMember("paramsFound", song.params.found);
    v.setMember("isProgram", song.params.isProgram);
    v.setMember("bank", song.params.bank);
    v.setMember("number", song.params.number);
    v.setMember("color", song.params.color);
    v.setMember("holdTime", song.params.holdTime);
    v.setMember("volume", song.params.volume);
    v.setMember("fontSize", static_cast<int>(song.params.fontSize));
    v.setMember("transpose", song.params.transpose);
    v.setMember("comment", song.comment);
    // The actual Combi's own name (cross-referenced from CMB1/CBK1) --
    // empty for Programs (not implemented yet) or if not found.
    v.setMember("instrumentName", song.instrumentName);
    return v;
}

choc::value::Value EditorBridge::programToValue(const kronos::ProgramInfo& program) {
    auto v = choc::value::createObject("ProgramInfo");
    v.setMember("bank", program.bank);
    v.setMember("number", program.number);
    v.setMember("name", program.name);
    // "HD-1"/"EXi" -- see ProgramBankType's doc comment in PcgFile.h. Not
    // yet cross-checked against a real backup's actual bytes, see
    // docs/external/README.md's caveat before trusting this in the UI.
    v.setMember("bankType", static_cast<int>(program.bankType));
    // Raw 0-9 value only -- see ProgramInfo::exiAlgorithmType's doc comment.
    // Only meaningful when bankType is Exi; the JS layer decides the actual
    // engine name and whether to show it at all, same "C++ decodes, JS
    // presents" split as every other formatted field in this bridge.
    v.setMember("exiAlgorithmType", program.exiAlgorithmType);
    return v;
}

choc::value::Value EditorBridge::combiToValue(const kronos::CombiInfo& combi) {
    auto v = choc::value::createObject("CombiInfo");
    v.setMember("bank", combi.bank);
    v.setMember("number", combi.number);
    v.setMember("name", combi.name);
    // Each Timbre's raw Program reference (see docs/content/format/index.md's "Combi
    // Timbre references" section) -- bankName is "" when this raw code
    // hasn't been identified yet, non-empty for a raw code confirmed by
    // name but with NO PBK1 file-order index (kronos::timbreBankName()'s
    // own doc comment -- e.g. GM, permanently indexless), and "" again for
    // a code that DOES have a confirmed index: for that last case,
    // library.js's formatTimbreRef() derives the name from
    // rawBankCode/PROGRAM_BANK_NAMES itself rather than reading it from
    // here, so there's exactly one place that name is spelled out. "" from
    // the first case falls back to showing the numeric code honestly
    // instead of a guessed name.
    auto timbres = choc::value::createEmptyArray();
    for (const auto& t : combi.timbres) {
        auto tv = choc::value::createObject("TimbreRef");
        tv.setMember("number", t.number);
        tv.setMember("rawBankCode", t.rawBankCode);
        tv.setMember("bankName", kronos::timbreBankName(t.rawBankCode));
        tv.setMember("isDefault", t.isDefault);
        // Off does NOT imply isDefault -- a Timbre can hold a real,
        // non-zero Program reference while switched off (see TimbreRef's
        // doc comment in PcgFile.h). Exposed separately so the UI can show
        // "referenced but inactive" rather than conflating the two.
        tv.setMember("status", static_cast<int>(t.status));
        timbres.addArrayElement(tv);
    }
    v.setMember("timbres", timbres);
    return v;
}

kronos::Setlist* EditorBridge::setlistOf(int datasetId, int setlistIndex) {
    auto it = m_datasets.find(datasetId);
    if (it == m_datasets.end()) return nullptr;
    auto& setlists = it->second.file.setlists();
    if (setlistIndex < 0 || setlistIndex >= static_cast<int>(setlists.size())) return nullptr;
    return &setlists[static_cast<size_t>(setlistIndex)];
}

kronos::PcgFile* EditorBridge::fileOf(int datasetId) {
    auto it = m_datasets.find(datasetId);
    if (it == m_datasets.end()) return nullptr;
    return &it->second.file;
}

choc::value::Value EditorBridge::datasetResultValue(int datasetId, const Dataset& dataset) {
    auto result = makeOk();
    result.setMember("datasetId", datasetId);
    result.setMember("displayName", dataset.displayName);
    result.setMember("setlistCount", static_cast<int>(dataset.file.setlists().size()));
    result.setMember("dirty", dataset.file.isDirty());
    return result;
}

choc::value::Value EditorBridge::finishOpen(Dataset dataset) {
    const int datasetId = m_nextDatasetId++;
    auto result = datasetResultValue(datasetId, dataset);  // read before the move below
    m_datasets[datasetId] = std::move(dataset);
    notifyDatasetsChanged();
    return result;
}

choc::value::Value EditorBridge::openFileAtPath(const std::string& path) {
    // Don't load the same file twice -- if a dataset opened from this exact
    // path is already loaded, just return its existing info (with
    // alreadyOpen:true) so the frontend shows/selects it instead of
    // duplicating it in memory. Only meaningful now that every open path
    // goes through a real filesystem path (openFileDialog()); drag-and-drop
    // never had one to compare.
    for (const auto& [datasetId, dataset] : m_datasets) {
        if (dataset.displayName == path) {
            auto result = datasetResultValue(datasetId, dataset);
            result.setMember("alreadyOpen", true);
            return result;
        }
    }

    Dataset dataset;
    dataset.displayName = path;
    std::string error;
    if (!dataset.file.load(path, error)) return makeError(error);

    return finishOpen(std::move(dataset));
}

choc::value::Value EditorBridge::openFile(const choc::value::ValueView& args) {
    const std::string path = stringArg(args, 0);
    if (path.empty()) return makeError("openFile requires a file path");
    return openFileAtPath(path);
}

choc::value::Value EditorBridge::openFileDialog(const choc::value::ValueView&) {
    if (!kronos::isNativeFileDialogSupported()) {
        return makeError("Native file dialogs aren't supported on this platform yet.");
    }
    auto path = kronos::showOpenFileDialog("Open a Korg Kronos .PCG/.SNG backup");
    if (!path) return makeCancelled();
    return openFileAtPath(*path);
}

choc::value::Value EditorBridge::listDatasets(const choc::value::ValueView&) {
    auto result = choc::value::createEmptyArray();
    for (const auto& [datasetId, dataset] : m_datasets) {
        auto v = choc::value::createObject("Dataset");
        v.setMember("datasetId", datasetId);
        v.setMember("displayName", dataset.displayName);
        v.setMember("setlistCount", static_cast<int>(dataset.file.setlists().size()));
        v.setMember("dirty", dataset.file.isDirty());
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value EditorBridge::closeDataset(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    if (m_datasets.erase(datasetId) > 0) notifyDatasetsChanged();
    return makeOk();
}

void EditorBridge::addDatasetsChangedListener(DatasetsChangedListener listener) {
    m_datasetsChangedListeners.push_back(std::move(listener));
}

void EditorBridge::notifyDatasetsChanged() {
    for (const auto& listener : m_datasetsChangedListeners) listener();
}

choc::value::Value EditorBridge::isDatasetDirty(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto it = m_datasets.find(datasetId);
    if (it == m_datasets.end()) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = makeOk();
    result.setMember("dirty", it->second.file.isDirty());
    return result;
}

choc::value::Value EditorBridge::listSetlists(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto it = m_datasets.find(datasetId);
    if (it == m_datasets.end()) return choc::value::createEmptyArray();

    auto result = choc::value::createEmptyArray();
    for (const auto& setlist : it->second.file.setlists()) {
        auto v = choc::value::createObject("Setlist");
        v.setMember("index", setlist.index);
        v.setMember("name", setlist.name.empty() ? "(unnamed)" : setlist.name);
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value EditorBridge::getEntries(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);

    const auto* setlist = setlistOf(datasetId, setlistIndex);
    if (setlist == nullptr) return choc::value::createEmptyArray();

    auto result = choc::value::createEmptyArray();
    for (const auto& song : setlist->songs) result.addArrayElement(songToValue(song));
    return result;
}

choc::value::Value EditorBridge::copyEntry(const choc::value::ValueView& args) {
    const int srcDatasetId = intArg(args, 0);
    const int srcSetlistIndex = intArg(args, 1);
    const int srcIndex = intArg(args, 2);
    const int dstDatasetId = intArg(args, 3);
    const int dstSetlistIndex = intArg(args, 4);
    const int dstIndex = intArg(args, 5);

    auto* srcSetlist = setlistOf(srcDatasetId, srcSetlistIndex);
    auto* dstSetlist = setlistOf(dstDatasetId, dstSetlistIndex);
    if (srcSetlist == nullptr || dstSetlist == nullptr) {
        return makeError("Source or destination Set List not loaded");
    }

    const int srcCount = static_cast<int>(srcSetlist->songs.size());
    const int dstCount = static_cast<int>(dstSetlist->songs.size());
    if (srcIndex < 0 || srcIndex >= srcCount || dstIndex < 0 || dstIndex >= dstCount) {
        return makeError("Entry index out of range");
    }

    const int dstOriginalIndex = dstSetlist->songs[dstIndex].index;
    kronos::Song copied = srcSetlist->songs[srcIndex];
    copied.index = dstOriginalIndex;  // keep destination slot's position, only its content changes
    dstSetlist->songs[dstIndex] = std::move(copied);
    return makeOk();
}

choc::value::Value EditorBridge::getSongRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const int songIndex = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto bytes = file->songRecordBytes(setlistIndex, songIndex);
    if (!bytes.has_value()) return makeError("No SBK1 record for that Set List slot");

    auto result = makeOk();
    result.setMember("bytes", bytesToValue(*bytes));
    return result;
}

choc::value::Value EditorBridge::putSongRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const int songIndex = intArg(args, 2);
    const std::vector<uint8_t> bytes = bytesArg(args, 3);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->putSongRecordBytes(setlistIndex, songIndex, bytes)) {
        return makeError("Couldn't write that Set List slot's record (wrong size, or index out of range)");
    }
    return makeOk();
}

choc::value::Value EditorBridge::getNameRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const int songIndex = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto bytes = file->nameRecordBytes(setlistIndex, songIndex);
    if (!bytes.has_value()) return makeError("No SDB1 record for that Set List slot");

    auto result = makeOk();
    result.setMember("bytes", bytesToValue(*bytes));
    return result;
}

choc::value::Value EditorBridge::putNameRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const int songIndex = intArg(args, 2);
    const std::vector<uint8_t> bytes = bytesArg(args, 3);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->putNameRecordBytes(setlistIndex, songIndex, bytes)) {
        return makeError("Couldn't write that Set List slot's name record (wrong size, or index out of range)");
    }
    return makeOk();
}

choc::value::Value EditorBridge::getProgramRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int number = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto bytes = file->programRecordBytes(bank, number);
    if (!bytes.has_value()) return makeError("No Program record at that bank/number");

    auto result = makeOk();
    result.setMember("bytes", bytesToValue(*bytes));
    return result;
}

choc::value::Value EditorBridge::putProgramRecordBytes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int number = intArg(args, 2);
    const std::vector<uint8_t> bytes = bytesArg(args, 3);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->putProgramRecordBytes(bank, number, bytes)) {
        return makeError("Couldn't write that Program record (wrong size, or bank/number out of range)");
    }
    return makeOk();
}

void EditorBridge::lockProgramRecord(int datasetId, int bank, int number) {
    m_lockedProgramRecords.insert({datasetId, bank, number});
}

void EditorBridge::unlockProgramRecord(int datasetId, int bank, int number) {
    m_lockedProgramRecords.erase({datasetId, bank, number});
}

bool EditorBridge::isProgramRecordLocked(int datasetId, int bank, int number) const {
    return m_lockedProgramRecords.count({datasetId, bank, number}) > 0;
}

std::optional<std::vector<uint8_t>> EditorBridge::getProgramRecordBytesRaw(int datasetId, int bank, int number) {
    auto* file = fileOf(datasetId);
    if (file == nullptr) return std::nullopt;
    return file->programRecordBytes(bank, number);
}

bool EditorBridge::putProgramRecordBytesRaw(int datasetId, int bank, int number, const std::vector<uint8_t>& bytes) {
    auto* file = fileOf(datasetId);
    if (file == nullptr) return false;
    return file->putProgramRecordBytes(bank, number, bytes);
}

choc::value::Value EditorBridge::reorderSongEntry(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const int fromIndex = intArg(args, 2);
    const int toIndex = intArg(args, 3);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->reorderSong(setlistIndex, fromIndex, toIndex)) {
        return makeError("Couldn't reorder that Set List slot (index out of range, or no SBK1/SDB1 data)");
    }
    return makeOk();
}

choc::value::Value EditorBridge::copySetlistEntries(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int srcSetlistIndex = intArg(args, 1);
    const int dstSetlistIndex = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->copySetlist(srcSetlistIndex, dstSetlistIndex)) {
        return makeError("Couldn't copy that Set List (index out of range)");
    }
    return makeOk();
}

choc::value::Value EditorBridge::sortSetlistEntries(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int setlistIndex = intArg(args, 1);
    const bool ascending = boolArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!file->sortSetlist(setlistIndex, ascending)) {
        return makeError("Couldn't sort that Set List (index out of range, or no SBK1/SDB1 data)");
    }
    return makeOk();
}

choc::value::Value EditorBridge::saveFileAs(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const std::string path = stringArg(args, 1);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    std::string error;
    if (!file->save(path, error)) return makeError(error);
    return makeOk();
}

choc::value::Value EditorBridge::saveFileDialog(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    if (!kronos::isNativeFileDialogSupported()) {
        return makeError("Native file dialogs aren't supported on this platform yet.");
    }

    // Pre-fill the dialog with the dataset's own filename (not its full
    // path -- the dialog's own starting directory covers that part), so
    // "Save As..." defaults to overwriting the file this dataset was
    // opened from unless the user picks somewhere else.
    std::string suggestedName;
    auto it = m_datasets.find(datasetId);
    if (it != m_datasets.end()) {
        const std::string& displayName = it->second.displayName;
        auto slash = displayName.find_last_of("/\\");
        suggestedName = slash == std::string::npos ? displayName : displayName.substr(slash + 1);
    }

    auto path = kronos::showSaveFileDialog("Save Korg Kronos .PCG/.SNG backup as", suggestedName);
    if (!path) return makeCancelled();

    std::string error;
    if (!file->save(*path, error)) return makeError(error);

    auto result = makeOk();
    result.setMember("path", *path);
    return result;
}

choc::value::Value EditorBridge::listPrograms(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto* file = fileOf(datasetId);
    if (file == nullptr) return choc::value::createEmptyArray();

    // Computed once here rather than per-row (programSetlistUsages() per
    // Program would be O(programs x songs) instead of O(songs)) -- see
    // PcgFile::setlistUsageCounts()'s doc comment.
    auto setlistCounts = file->setlistUsageCounts(/*isProgram=*/true);
    auto combiCounts = file->combiUsageCounts();

    auto result = choc::value::createEmptyArray();
    for (const auto& program : file->programs()) {
        auto v = programToValue(program);
        v.setMember("setlistReferenceCount", countAt(setlistCounts, program.bank, program.number));
        // Only banks where isConfirmedTimbreProgramBank() is true have a
        // real count -- see PcgFile::combiUsageCounts()'s doc comment for
        // why the rest can't be computed without risking a wrong answer.
        const bool available = kronos::isConfirmedTimbreProgramBank(program.bank);
        v.setMember("combiReferenceCountAvailable", available);
        v.setMember("combiReferenceCount", available ? countAt(combiCounts, program.bank, program.number) : 0);
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value EditorBridge::listCombis(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto* file = fileOf(datasetId);
    if (file == nullptr) return choc::value::createEmptyArray();

    auto setlistCounts = file->setlistUsageCounts(/*isProgram=*/false);

    auto result = choc::value::createEmptyArray();
    for (const auto& combi : file->combis()) {
        auto v = combiToValue(combi);
        const int count = countAt(setlistCounts, combi.bank, combi.number);
        v.setMember("setlistReferenceCount", count);
        // Full usage list too (not just the count) so the UI can show Set
        // List name "badges" for combis with few enough references --
        // skip the per-row query entirely when the bulk count says zero.
        v.setMember("setlistUsages", count > 0
                                          ? setlistUsagesToValue(file->combiSetlistUsages(combi.bank, combi.number))
                                          : choc::value::createEmptyArray());
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value EditorBridge::getProgramUsage(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int number = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = makeOk();
    result.setMember("setlistUsages", setlistUsagesToValue(file->programSetlistUsages(bank, number)));
    // Only true for banks where isConfirmedTimbreProgramBank() holds --
    // this flag is what lets the UI say "not available for this bank"
    // honestly instead of implying "zero Combi usages found".
    const bool combiAvailable = kronos::isConfirmedTimbreProgramBank(bank);
    result.setMember("combiUsagesAvailable", combiAvailable);
    result.setMember("combiUsages", combiAvailable ? combiUsagesToValue(file->combiUsagesForProgram(bank, number))
                                                    : choc::value::createEmptyArray());
    return result;
}

choc::value::Value EditorBridge::findDuplicatePrograms(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto* file = fileOf(datasetId);
    if (file == nullptr) return choc::value::createEmptyArray();

    auto result = choc::value::createEmptyArray();
    for (const auto& group : file->findDuplicatePrograms()) {
        auto groupValue = choc::value::createEmptyArray();
        for (const auto& program : group) {
            auto v = programToValue(program);
            v.setMember("setlistUsageCount",
                        static_cast<int>(file->programSetlistUsages(program.bank, program.number).size()));
            const bool combiAvailable = kronos::isConfirmedTimbreProgramBank(program.bank);
            v.setMember("combiUsageCountAvailable", combiAvailable);
            v.setMember("combiUsageCount", combiAvailable
                                                ? static_cast<int>(file->combiUsagesForProgram(program.bank, program.number).size())
                                                : 0);
            groupValue.addArrayElement(v);
        }
        result.addArrayElement(groupValue);
    }
    return result;
}

choc::value::Value EditorBridge::getProgramBankTypes(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto* file = fileOf(datasetId);
    if (file == nullptr) return choc::value::createEmptyArray();

    auto result = choc::value::createEmptyArray();
    for (const auto& entry : file->programBankTypes()) {
        auto v = choc::value::createObject("ProgramBankTypeEntry");
        v.setMember("bank", entry.bank);
        v.setMember("bankType", static_cast<int>(entry.bankType));
        result.addArrayElement(v);
    }
    return result;
}

choc::value::Value EditorBridge::getDatasetInternals(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = makeOk();

    auto topLevelChunks = choc::value::createEmptyArray();
    for (const auto& tag : file->topLevelChunkTags()) topLevelChunks.addArrayElement(choc::value::Value(tag));
    result.setMember("topLevelChunks", topLevelChunks);

    auto programBanks = choc::value::createEmptyArray();
    for (const auto& b : file->programBankInfo()) {
        auto v = choc::value::createObject("ProgramBankInfo");
        v.setMember("index", b.index);
        v.setMember("bankType", static_cast<int>(b.bankType));
        v.setMember("numRecords", b.numRecords);
        v.setMember("bytesPerRecord", b.bytesPerRecord);
        programBanks.addArrayElement(v);
    }
    result.setMember("programBanks", programBanks);

    auto combiBanks = choc::value::createEmptyArray();
    for (const auto& b : file->combiBankInfo()) {
        auto v = choc::value::createObject("CombiBankInfo");
        v.setMember("index", b.index);
        v.setMember("numRecords", b.numRecords);
        v.setMember("bytesPerRecord", b.bytesPerRecord);
        combiBanks.addArrayElement(v);
    }
    result.setMember("combiBanks", combiBanks);

    return result;
}

namespace {

std::string programCopyErrorMessage(kronos::PcgFile::ProgramCopyError error) {
    switch (error) {
        case kronos::PcgFile::ProgramCopyError::BankTypeMismatch:
            return "Can't copy: source and destination banks are different engine types (HD-1/EXi) -- "
                   "a Program can only be loaded into a bank of the matching type.";
        case kronos::PcgFile::ProgramCopyError::RecordSizeMismatch:
            return "Can't copy: source and destination banks don't share the same record size.";
        case kronos::PcgFile::ProgramCopyError::OutOfRange:
            return "Can't copy: source or destination bank/number is out of range.";
        case kronos::PcgFile::ProgramCopyError::TargetSlotOccupied:
            return "Can't copy: the destination slot already holds a different Program.";
        case kronos::PcgFile::ProgramCopyError::DuplicateExists:
            return "Can't copy: a byte-identical Program already exists in the destination dataset.";
        default:
            return "Can't copy: unknown error.";
    }
}

}  // namespace

choc::value::Value EditorBridge::copyProgram(const choc::value::ValueView& args) {
    const int srcDatasetId = intArg(args, 0);
    const int srcBank = intArg(args, 1);
    const int srcNumber = intArg(args, 2);
    const int dstDatasetId = intArg(args, 3);
    const int dstBank = intArg(args, 4);
    const int dstNumber = intArg(args, 5);

    auto* srcFile = fileOf(srcDatasetId);
    if (srcFile == nullptr) return makeError("Source dataset is no longer open.");
    auto* dstFile = fileOf(dstDatasetId);
    if (dstFile == nullptr) return makeError("Destination dataset is no longer open.");

    // Refuse if EITHER side is open in an external editor (e.g. a private-
    // module EXi parameter editor window, see lockProgramRecord()'s own
    // doc comment) -- copying FROM a locked source risks grabbing a stale
    // snapshot the editor hasn't written back yet; copying ONTO a locked
    // destination would silently get overwritten the moment that editor's
    // own (already-open, already-fetched) bytes get written back. Checked
    // before either file is touched.
    if (isProgramRecordLocked(srcDatasetId, srcBank, srcNumber)) {
        return makeError("Can't copy: the source Program is open in an editor.");
    }
    if (isProgramRecordLocked(dstDatasetId, dstBank, dstNumber)) {
        return makeError("Can't copy: the destination Program is open in an editor.");
    }

    auto error = dstFile->copyProgramFrom(*srcFile, srcBank, srcNumber, dstBank, dstNumber);
    if (error.has_value()) return makeError(programCopyErrorMessage(*error));
    return makeOk();
}

choc::value::Value EditorBridge::swapProgram(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bankA = intArg(args, 1);
    const int numberA = intArg(args, 2);
    const int bankB = intArg(args, 3);
    const int numberB = intArg(args, 4);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    // Same reasoning as copyProgram() above -- a swap mutates BOTH slots'
    // bytes out from under either one's own open editor, if either has one.
    if (isProgramRecordLocked(datasetId, bankA, numberA) || isProgramRecordLocked(datasetId, bankB, numberB)) {
        return makeError("Can't swap: one of these Programs is open in an editor.");
    }

    auto result = file->swapPrograms(bankA, numberA, bankB, numberB);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    value.setMember("combiRefsRepointed", result.combiRefsRepointed);
    value.setMember("combiRefsSkipped", result.combiRefsSkipped);
    return value;
}

choc::value::Value EditorBridge::resolveDuplicateProgram(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int number = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    const auto hd1Bytes = readResourceFile("Init-Program-HD1.raw");
    const auto exiBytes = readResourceFile("Init-Program-EXi.raw");
    if (hd1Bytes.empty() || exiBytes.empty()) {
        return makeError("Couldn't read the Init Program template files from " + std::string(EDITOR_RESOURCES_DIR));
    }

    auto result = file->resolveDuplicates(bank, number, hd1Bytes, exiBytes);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("clearedPrograms", result.clearedPrograms);
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    value.setMember("combiRefsRepointed", result.combiRefsRepointed);
    value.setMember("combiRefsSkipped", result.combiRefsSkipped);
    return value;
}

choc::value::Value EditorBridge::resetProgram(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int number = intArg(args, 2);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    const auto hd1Bytes = readResourceFile("Init-Program-HD1.raw");
    const auto exiBytes = readResourceFile("Init-Program-EXi.raw");
    if (hd1Bytes.empty() || exiBytes.empty()) {
        return makeError("Couldn't read the Init Program template files from " + std::string(EDITOR_RESOURCES_DIR));
    }

    auto result = file->resetProgram(bank, number, hd1Bytes, exiBytes);
    if (!result.ok) return makeError(result.error);

    return makeOk();
}

choc::value::Value EditorBridge::swapCombis(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bankA = intArg(args, 1);
    const int numberA = intArg(args, 2);
    const int bankB = intArg(args, 3);
    const int numberB = intArg(args, 4);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = file->swapCombis(bankA, numberA, bankB, numberB);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    return value;
}

choc::value::Value EditorBridge::moveCombiWithinBank(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int bank = intArg(args, 1);
    const int fromNumber = intArg(args, 2);
    const int toNumber = intArg(args, 3);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = file->moveCombiWithinBank(bank, fromNumber, toNumber);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    return value;
}

choc::value::Value EditorBridge::moveCombiToBank(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int srcBank = intArg(args, 1);
    const int srcNumber = intArg(args, 2);
    const int dstBank = intArg(args, 3);
    const int dstNumber = intArg(args, 4);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = file->moveCombiToBank(srcBank, srcNumber, dstBank, dstNumber);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    return value;
}

choc::value::Value EditorBridge::copyCombi(const choc::value::ValueView& args) {
    const int datasetId = intArg(args, 0);
    const int srcBank = intArg(args, 1);
    const int srcNumber = intArg(args, 2);
    const int dstBank = intArg(args, 3);
    const int dstNumber = intArg(args, 4);

    auto* file = fileOf(datasetId);
    if (file == nullptr) return makeError("Dataset " + std::to_string(datasetId) + " has no file loaded");

    auto result = file->copyCombi(srcBank, srcNumber, dstBank, dstNumber);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    return value;
}

choc::value::Value EditorBridge::analyzeCombiCrossDatasetCopy(const choc::value::ValueView& args) {
    const int srcDatasetId = intArg(args, 0);
    const int srcBank = intArg(args, 1);
    const int srcNumber = intArg(args, 2);
    const int dstDatasetId = intArg(args, 3);
    const int dstBank = intArg(args, 4);
    const int dstNumber = intArg(args, 5);

    auto* srcFile = fileOf(srcDatasetId);
    auto* dstFile = fileOf(dstDatasetId);
    if (srcFile == nullptr) return makeError("Dataset " + std::to_string(srcDatasetId) + " has no file loaded");
    if (dstFile == nullptr) return makeError("Dataset " + std::to_string(dstDatasetId) + " has no file loaded");

    auto analysis = dstFile->analyzeCombiCrossDatasetCopy(*srcFile, srcBank, srcNumber, dstBank, dstNumber);
    if (!analysis.ok) return makeError(analysis.error);

    auto value = makeOk();
    auto dependencies = choc::value::createEmptyArray();
    for (const auto& dep : analysis.dependencies) {
        auto dv = choc::value::createObject("TimbreProgramDependency");
        dv.setMember("timbreIndex", dep.timbreIndex);
        dv.setMember("srcBank", dep.srcBank);
        dv.setMember("srcNumber", dep.srcNumber);
        dv.setMember("name", dep.name);
        dv.setMember("bankType", static_cast<int>(dep.bankType));
        dv.setMember("found", dep.found);
        dv.setMember("foundBank", dep.foundBank);
        dv.setMember("foundNumber", dep.foundNumber);
        dependencies.addArrayElement(dv);
    }
    value.setMember("dependencies", dependencies);

    auto unresolved = choc::value::createEmptyArray();
    for (const auto& u : analysis.unresolved) {
        auto uv = choc::value::createObject("UnresolvedProgram");
        uv.setMember("srcBank", u.srcBank);
        uv.setMember("srcNumber", u.srcNumber);
        uv.setMember("name", u.name);
        uv.setMember("bankType", static_cast<int>(u.bankType));
        auto candidateBanks = choc::value::createArray(static_cast<uint32_t>(u.candidateBanks.size()),
                                                          [&](uint32_t i) { return u.candidateBanks[i]; });
        uv.setMember("candidateBanks", candidateBanks);
        unresolved.addArrayElement(uv);
    }
    value.setMember("unresolved", unresolved);

    auto unmappable = choc::value::createEmptyArray();
    for (const auto& u : analysis.unmappableTimbres) {
        auto uv = choc::value::createObject("UnmappableTimbre");
        uv.setMember("timbreIndex", u.timbreIndex);
        uv.setMember("rawBankCode", u.rawBankCode);
        uv.setMember("rawNumber", u.rawNumber);
        unmappable.addArrayElement(uv);
    }
    value.setMember("unmappableTimbres", unmappable);
    return value;
}

choc::value::Value EditorBridge::applyCombiCrossDatasetCopy(const choc::value::ValueView& args) {
    const int srcDatasetId = intArg(args, 0);
    const int srcBank = intArg(args, 1);
    const int srcNumber = intArg(args, 2);
    const int dstDatasetId = intArg(args, 3);
    const int dstBank = intArg(args, 4);
    const int dstNumber = intArg(args, 5);
    const auto placements = placementsArg(args, 6);

    auto* srcFile = fileOf(srcDatasetId);
    auto* dstFile = fileOf(dstDatasetId);
    if (srcFile == nullptr) return makeError("Dataset " + std::to_string(srcDatasetId) + " has no file loaded");
    if (dstFile == nullptr) return makeError("Dataset " + std::to_string(dstDatasetId) + " has no file loaded");

    auto result = dstFile->applyCombiCrossDatasetCopy(*srcFile, srcBank, srcNumber, dstBank, dstNumber, placements);
    if (!result.ok) return makeError(result.error);

    auto value = makeOk();
    value.setMember("setlistRefsRepointed", result.setlistRefsRepointed);
    return value;
}
