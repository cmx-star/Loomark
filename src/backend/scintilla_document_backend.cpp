#include "backend/scintilla_document_backend.h"

#include "backend/scintilla_load_task.h"
#include "core/document_file.h"
#include "core/file_tier.h"

#include <QByteArray>
#include <fstream>

#include <algorithm>
#include <deque>
#include <stdexcept>
#include <vector>

namespace mqt::backend {
namespace {

std::vector<std::size_t> buildPrefixTable(std::string_view needle)
{
    std::vector<std::size_t> prefix(needle.size(), 0);
    std::size_t matched = 0;
    for (std::size_t i = 1; i < needle.size(); ++i) {
        while (matched > 0 && needle[i] != needle[matched]) {
            matched = prefix[matched - 1];
        }
        if (needle[i] == needle[matched]) {
            ++matched;
        }
        prefix[i] = matched;
    }
    return prefix;
}

// Byte cursor used to keep line/column semantics byte-for-byte identical to
// FileDocumentBackend (column counts bytes, both 1-based).
struct Cursor {
    std::uint64_t offset = 0;
    std::uint64_t line = 1;
    std::uint64_t column = 1;
};

void advanceCursor(Cursor& cursor, unsigned char ch)
{
    ++cursor.offset;
    if (ch == '\n') {
        ++cursor.line;
        cursor.column = 1;
    } else {
        ++cursor.column;
    }
}

struct CursorSample {
    std::uint64_t offset = 0;
    mqt::core::TextPosition position {};
};

} // namespace

ScintillaDocumentBackend::ScintillaDocumentBackend(ScintillaEditBase& editor)
    : QObject(&editor)
    , editor_(editor)
{
    editor_.send(SCI_SETCODEPAGE, SC_CP_UTF8);

    // Direct (same-thread) connection: Scintilla emits `modified`
    // synchronously during message processing, so the mutatingDocument_
    // guard is reliable without queueing.
    connect(&editor_, &ScintillaEditBase::modified,
        this, &ScintillaDocumentBackend::onEditorModified);
}

ScintillaDocumentBackend::ScintillaDocumentBackend(ScintillaEditBase& editor,
    const std::filesystem::path& path)
    : ScintillaDocumentBackend(editor)
{
    path_ = path;
    loadFromDisk();
}

ScintillaDocumentBackend::~ScintillaDocumentBackend()
{
    if (loadTask_ != nullptr) {
        loadTask_->cancel();
        delete loadTask_;
        loadTask_ = nullptr;
    }
}

mqt::core::DocumentSnapshot ScintillaDocumentBackend::snapshot() const
{
    return {version_, info_};
}

mqt::core::DocumentInfo ScintillaDocumentBackend::info() const
{
    return info_;
}

std::string ScintillaDocumentBackend::read(mqt::core::ByteRange range) const
{
    const auto sz = range.size();
    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    if (sz > length || range.start > length - sz) {
        throw std::out_of_range("byte range exceeds content size");
    }

    Sci_TextRangeFull rangeFull {};
    rangeFull.chrg.cpMin = static_cast<Scintilla::sptr_t>(range.start);
    rangeFull.chrg.cpMax = static_cast<Scintilla::sptr_t>(range.start + sz);
    std::string out(static_cast<std::size_t>(sz) + 1, '\0');
    rangeFull.lpstrText = out.data();
    editor_.send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<Scintilla::sptr_t>(&rangeFull));
    out.resize(static_cast<std::size_t>(sz));
    return out;
}

mqt::core::LocateResult ScintillaDocumentBackend::locateLines(mqt::core::ByteRange range) const
{
    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    if (range.end > length || range.start > length) {
        throw std::out_of_range("byte range exceeds content size");
    }

    // Mirror FileDocumentBackend: offsets at or past the end never resolve
    // (its byte scan only samples cursor offsets below the content size).
    if (range.start >= length || range.end >= length) {
        throw std::runtime_error("failed to locate line positions");
    }

    const auto toPosition = [&](std::uint64_t offset) {
        const auto lineIndex = editor_.send(SCI_LINEFROMPOSITION,
            static_cast<Scintilla::uptr_t>(offset));
        const auto lineStart = editor_.send(SCI_POSITIONFROMLINE, lineIndex);
        return mqt::core::TextPosition{
            .line = static_cast<std::uint64_t>(lineIndex) + 1,
            .column = offset - static_cast<std::uint64_t>(lineStart) + 1,
        };
    };
    return {toPosition(range.start), toPosition(range.end)};
}

mqt::core::SearchOutcome ScintillaDocumentBackend::search(const mqt::core::SearchQuery& query,
    const std::atomic_bool* cancelFlag) const
{
    if (query.needle.empty()) {
        throw std::invalid_argument("search needle cannot be empty");
    }
    if (query.options.chunkSize == 0 || query.options.maxResults == 0) {
        throw std::invalid_argument("search options must be non-zero");
    }

    // Chunked KMP over the editor buffer; algorithm mirrors
    // FileDocumentBackend::search so both backends share hit semantics
    // (bom-adjusted ranges, cursor samples, truncation, cancellation).
    mqt::core::SearchResult result;
    const auto prefix = buildPrefixTable(query.needle);
    std::string buf;
    buf.resize(query.options.chunkSize);
    std::size_t matched = 0;
    Cursor cursor;
    std::deque<CursorSample> samples;
    samples.push_back({0, {1, 1}});

    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    Sci_TextRangeFull rangeFull {};
    std::uint64_t i = 0;
    while (i < length && !result.truncated) {
        if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
            return {result, true};
        }
        const auto toRead = std::min<std::uint64_t>(query.options.chunkSize, length - i);
        rangeFull.chrg.cpMin = static_cast<Scintilla::sptr_t>(i);
        rangeFull.chrg.cpMax = static_cast<Scintilla::sptr_t>(i + toRead);
        rangeFull.lpstrText = buf.data();
        editor_.send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<Scintilla::sptr_t>(&rangeFull));
        i += toRead;

        for (std::uint64_t j = 0; j < toRead; ++j) {
            const unsigned char ch = static_cast<unsigned char>(buf[static_cast<std::size_t>(j)]);
            samples.push_back({cursor.offset, {cursor.line, cursor.column}});
            bool stopAfterCurrentByte = false;

            while (matched > 0 && ch != static_cast<unsigned char>(query.needle[matched])) {
                matched = prefix[matched - 1];
            }
            if (ch == static_cast<unsigned char>(query.needle[matched])) {
                ++matched;
            }

            const auto startLimit = cursor.offset >= query.needle.size()
                ? cursor.offset - query.needle.size() + 1 : 0;
            while (!samples.empty() && samples.front().offset < startLimit) {
                samples.pop_front();
            }

            if (matched == query.needle.size()) {
                if (result.hits.size() >= query.options.maxResults) {
                    result.truncated = true;
                    stopAfterCurrentByte = true;
                } else {
                    if (samples.empty() || samples.front().offset != startLimit) {
                        throw std::runtime_error("search cursor alignment lost");
                    }
                    result.hits.push_back(mqt::core::SearchHit{
                        .sourceRange = {startLimit + bomOffset_, cursor.offset + 1 + bomOffset_},
                        .position = samples.front().position,
                    });
                    matched = prefix[matched - 1];
                }
            }

            advanceCursor(cursor, ch);
            if (stopAfterCurrentByte) {
                break;
            }
        }
    }

    result.bytesScanned = cursor.offset;
    return {result, false};
}

mqt::core::ApplyResult ScintillaDocumentBackend::apply(std::vector<mqt::core::TextEdit> edits,
    mqt::core::DocumentVersion baseVersion,
    const std::atomic_bool* cancelFlag)
{
    (void)cancelFlag;
    if (baseVersion != version_) {
        return {mqt::core::ApplyError::StaleVersion, version_};
    }

    if (edits.empty()) {
        return {mqt::core::ApplyError::None, version_};
    }

    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    std::uint64_t prevEnd = 0;
    for (const auto& edit : edits) {
        if (edit.start > edit.end) {
            return {mqt::core::ApplyError::RangeInvalid, version_};
        }
        if (edit.end > length) {
            return {mqt::core::ApplyError::RangeInvalid, version_};
        }
        if (edit.start < prevEnd) {
            return {mqt::core::ApplyError::OverlappingEdits, version_};
        }
        prevEnd = edit.end;
    }

    mutatingDocument_ = true;
    editor_.send(SCI_BEGINUNDOACTION);
    // Descending application keeps earlier offsets valid without remapping.
    for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
        if (it->end > it->start) {
            editor_.send(SCI_DELETERANGE,
                static_cast<Scintilla::uptr_t>(it->start),
                static_cast<Scintilla::sptr_t>(it->end - it->start));
        }
        if (!it->newText.empty()) {
            editor_.send(SCI_INSERTTEXT,
                static_cast<Scintilla::uptr_t>(it->start),
                reinterpret_cast<Scintilla::sptr_t>(const_cast<char*>(it->newText.c_str())));
        }
    }
    editor_.send(SCI_ENDUNDOACTION);
    mutatingDocument_ = false;

    std::uint64_t undoDelta = 0;
    for (const auto& edit : edits) {
        undoDelta += (edit.end - edit.start) + edit.newText.size();
    }
    chargeUndoBytes(undoDelta);

    info_.sizeBytes = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    ++version_;
    return {mqt::core::ApplyError::None, version_};
}

mqt::backend::ScintillaDocumentBackend::SearchBatchResult
ScintillaDocumentBackend::searchBatch(const SearchBatchRequest& request,
    const std::atomic_bool* cancelFlag) const
{
    SearchBatchResult result;
    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    if (request.needle.empty() || request.startOffset >= length) {
        result.exhausted = request.startOffset >= length;
        result.nextOffset = length;
        return result;
    }

    sptr_t searchFlags = 0;
    if (request.matchCase) {
        searchFlags |= SCFIND_MATCHCASE;
    }
    if (request.regex) {
        searchFlags |= SCFIND_REGEXP;
    }
    if (request.wholeWord) {
        searchFlags |= SCFIND_WHOLEWORD;
    }
    editor_.send(SCI_SETSEARCHFLAGS, static_cast<uptr_t>(searchFlags));

    QElapsedTimer deadline;
    deadline.start();
    auto pos = static_cast<sptr_t>(request.startOffset);
    const auto windowEnd = static_cast<sptr_t>(
        std::min<std::uint64_t>(request.startOffset + request.maxWindow, length));

    while (pos < windowEnd) {
        if (cancelFlag != nullptr && cancelFlag->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            result.nextOffset = static_cast<std::uint64_t>(pos);
            return result;
        }
        editor_.send(SCI_SETTARGETSTART, static_cast<uptr_t>(pos));
        editor_.send(SCI_SETTARGETEND, static_cast<uptr_t>(windowEnd));
        const auto found = editor_.send(SCI_SEARCHINTARGET,
            static_cast<uptr_t>(request.needle.size()),
            reinterpret_cast<sptr_t>(const_cast<char*>(request.needle.c_str())));
        if (found < 0) {
            // No more matches inside this window.
            result.exhausted = windowEnd >= static_cast<sptr_t>(length);
            result.nextOffset = static_cast<std::uint64_t>(windowEnd);
            return result;
        }

        const auto hitStart = editor_.send(SCI_GETTARGETSTART);
        const auto hitEnd = editor_.send(SCI_GETTARGETEND);
        const auto lineIndex = editor_.send(SCI_LINEFROMPOSITION, hitStart);
        const auto lineStart = editor_.send(SCI_POSITIONFROMLINE, lineIndex);
        result.hits.push_back(mqt::core::SearchHit{
            .sourceRange = {static_cast<std::uint64_t>(hitStart),
                            static_cast<std::uint64_t>(hitEnd)},
            .position = {static_cast<std::uint64_t>(lineIndex) + 1,
                         static_cast<std::uint64_t>(hitStart - lineStart + 1)},
        });

        if (static_cast<std::uint32_t>(result.hits.size()) >= request.maxResults) {
            result.truncated = hitEnd < windowEnd || windowEnd < static_cast<sptr_t>(length);
            result.nextOffset = static_cast<std::uint64_t>(hitEnd);
            return result;
        }
        if (static_cast<std::uint64_t>(deadline.elapsed()) >= request.deadlineMs) {
            result.timeout = true;
            result.nextOffset = static_cast<std::uint64_t>(hitEnd);
            return result;
        }
        // Zero-length regex matches must still advance.
        pos = std::max<sptr_t>(hitEnd, hitStart + 1);
    }
    result.exhausted = true;
    result.nextOffset = static_cast<std::uint64_t>(windowEnd);
    return result;
}

mqt::core::ApplyResult ScintillaDocumentBackend::applyReplace(
    const std::vector<mqt::core::TextEdit>& edits,
    mqt::core::DocumentVersion baseVersion, bool confirmed)
{
    std::uint64_t affected = 0;
    for (const auto& edit : edits) {
        affected += (edit.end - edit.start) + edit.newText.size();
    }
    if (affected > mqt::core::kReplaceConfirmBytes && !confirmed) {
        return {mqt::core::ApplyError::ConfirmationRequired, version_};
    }
    return apply(edits, baseVersion, nullptr);
}

void ScintillaDocumentBackend::save(const std::atomic_bool*)
{
    saveTo(path_);
}

void ScintillaDocumentBackend::saveAs(const std::filesystem::path& path)
{
    path_ = path;
    saveTo(path_);
}

mqt::core::DocumentSnapshot ScintillaDocumentBackend::reload()
{
    loadFromDisk();
    ++version_;
    savedVersion_ = version_; // reloaded content matches the disk baseline
    return snapshot();
}

bool ScintillaDocumentBackend::startBackgroundLoad(const std::filesystem::path& path,
    std::uint64_t blockSize)
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    if (mqt::core::classifyDesktopFile(fileSize) == mqt::core::FileTier::Reject) {
        return false;
    }

    if (loadTask_ != nullptr) {
        loadTask_->cancel();
    }
    setDocumentText(std::string());

    path_ = path;
    bomOffset_ = 0;
    lineIndex_ = mqt::core::SparseLineIndex{};
    fingerprint_ = 0;
    const auto fileInfo = mqt::core::statFile(path);
    mqt::core::DocumentInfo info;
    info.tier = fileInfo.tier;
    info.sizeBytes = fileSize - (fileInfo.hasUtf8Bom ? 3ULL : 0ULL);
    info.hasUtf8Bom = fileInfo.hasUtf8Bom;
    info_ = info;

    loadTask_ = new ScintillaLoadTask(path, blockSize, this);
    connect(loadTask_, &ScintillaLoadTask::chunkReady,
        this, &ScintillaDocumentBackend::onLoadChunk);
    connect(loadTask_, &ScintillaLoadTask::progress,
        this, [this](std::uint64_t loaded, std::uint64_t total) {
            emit loadProgress(loaded, total);
        });
    connect(loadTask_, &ScintillaLoadTask::loadFinished,
        this, &ScintillaDocumentBackend::onLoadTaskFinished);
    loadTask_->start();
    return true;
}

void ScintillaDocumentBackend::cancelBackgroundLoad()
{
    if (loadTask_ != nullptr) {
        loadTask_->cancel();
    }
}

void ScintillaDocumentBackend::onLoadChunk(const QByteArray& bytes)
{
    if (loadTask_ == nullptr) {
        return;
    }
    if (!bytes.isEmpty()) {
        mutatingDocument_ = true;
        editor_.send(SCI_APPENDTEXT, static_cast<uptr_t>(bytes.size()),
            reinterpret_cast<sptr_t>(const_cast<char*>(bytes.constData())));
        mutatingDocument_ = false;
    }
    loadTask_->notifyConsumed();
}

void ScintillaDocumentBackend::onLoadTaskFinished(bool ok, const QString& error)
{
    ScintillaLoadTask* task = loadTask_;
    loadTask_ = nullptr;
    if (task == nullptr) {
        return;
    }

    if (ok) {
        lineIndex_ = task->lineIndex();
        fingerprint_ = task->fingerprint();
        const auto counts = task->newlineCounts();
        info_.sizeBytes = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
        if (mqt::core::newlineStyleKnown(counts)) {
            info_.newlineStyle = mqt::core::classifyNewlines(counts);
            info_.newlineStyleKnown = true;
        }
        fingerprint_ = task->fingerprint();
        baselineFingerprint_ = fingerprint_;
        savedVersion_ = version_;
        undoBytesUsed_ = 0;
        mutatingDocument_ = true;
        editor_.send(SCI_EMPTYUNDOBUFFER);
        mutatingDocument_ = false;
    } else {
        // Cancelled or failed: deterministic cleanup leaves an empty buffer
        // and empty metadata.
        setDocumentText(std::string());
        lineIndex_ = mqt::core::SparseLineIndex{};
        fingerprint_ = 0;
        info_ = mqt::core::DocumentInfo{};
        bomOffset_ = 0;
    }
    task->deleteLater();
    emit loadFinished(ok, error);
}

void ScintillaDocumentBackend::onEditorModified(Scintilla::ModificationFlags type,
    Scintilla::Position position, Scintilla::Position length)
{
    (void)position;
    if (mutatingDocument_) {
        return;
    }
    using Fl = Scintilla::ModificationFlags;
    const bool contentChanged = (type & Fl::InsertText) != Fl(0) ||
                                (type & Fl::DeleteText) != Fl(0);
    if (contentChanged) {
        ++version_;
        if (length > 0) {
            chargeUndoBytes(static_cast<std::uint64_t>(length));
        }
        emit contentsChanged();
    }
}

void ScintillaDocumentBackend::chargeUndoBytes(std::uint64_t bytes)
{
    if (undoBudgetBytes_ == 0) {
        return; // 0 = unlimited
    }
    undoBytesUsed_ += bytes;
    if (undoBytesUsed_ > undoBudgetBytes_) {
        undoBytesUsed_ = 0;
        mutatingDocument_ = true;
        editor_.send(SCI_EMPTYUNDOBUFFER);
        mutatingDocument_ = false;
        emit undoBudgetExceeded();
    }
}

void ScintillaDocumentBackend::loadFromDisk()
{
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path_, ec);
    if (ec) {
        throw std::runtime_error("failed to read file size: " + path_.string());
    }
    // Note: no tier policy here — the backend handles any size; tier gating
    // (dialogs, windowed fallback) is a GUI-open concern.

    std::string raw = mqt::core::readRange(path_, {0, fileSize});
    bomOffset_ = 0;
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        bomOffset_ = 3;
        raw.erase(0, 3);
    }

    setDocumentText(raw);
    info_.tier = mqt::core::FileTier::Normal;
    info_.sizeBytes = raw.size();
    info_.hasUtf8Bom = bomOffset_ > 0;
    info_.newlineStyle = mqt::core::NewlineStyle::None;
    info_.newlineStyleKnown = false;

    mqt::core::FingerprintSink sink;
    // 基线指纹对齐磁盘表示（含 BOM），与 saveTo 的 writtenFingerprint 同口径
    if (bomOffset_ > 0) {
        sink.update("\xEF\xBB\xBF");
    }
    sink.update(raw);
    fingerprint_ = sink.value();
    baselineFingerprint_ = fingerprint_;
    savedVersion_ = version_;
    undoBytesUsed_ = 0;
}

void ScintillaDocumentBackend::saveTo(const std::filesystem::path& path)
{
    const auto length = static_cast<std::uint64_t>(editor_.send(SCI_GETTEXTLENGTH));
    const auto dir = path.parent_path().empty()
        ? std::filesystem::current_path()
        : path.parent_path();
    const auto freeBytes = mqt::core::availableDiskBytes(dir);
    const auto needed = length + (info_.hasUtf8Bom ? 3ULL : 0ULL) + 2ULL * mqt::core::kMiB;
    if (freeBytes < needed) {
        throw std::runtime_error("insufficient disk space to save document");
    }

    // M12 external-change review: the on-disk content must still match the
    // baseline recorded at the last load/save. Saving over an externally
    // modified file would silently destroy those changes. Save-as to a new
    // target skips the review (overwriting an existing target is intent).
    std::error_code existsEc;
    if (path == path_ && std::filesystem::exists(path_, existsEc) && !existsEc) {
        mqt::core::FingerprintSink diskSink;
        std::ifstream diskInput(path_, std::ios::binary);
        if (diskInput) {
            std::string diskChunk(8ULL * mqt::core::kMiB, '\0');
            while (diskInput.read(diskChunk.data(),
                     static_cast<std::streamsize>(diskChunk.size())) ||
                diskInput.gcount() > 0) {
                diskSink.update(std::string_view(diskChunk.data(),
                    static_cast<std::size_t>(diskInput.gcount())));
                if (diskInput.eof()) {
                    break;
                }
            }
            if (diskSink.value() != baselineFingerprint_) {
                throw std::runtime_error(
                    "document changed on disk since the last save");
            }
        }
    }

    mqt::core::AtomicFileWriter writer(path);
    mqt::core::FingerprintSink writtenFingerprint;
    if (info_.hasUtf8Bom) {
        writer.write("\xEF\xBB\xBF");
        writtenFingerprint.update("\xEF\xBB\xBF");
    }
    // Chunked extraction: never materialize a second full copy of the body.
    constexpr std::uint64_t kChunk = 8ULL * mqt::core::kMiB;
    Sci_TextRangeFull rangeFull {};
    std::string buf;
    for (std::uint64_t start = 0; start < length;) {
        const auto toRead = std::min<std::uint64_t>(kChunk, length - start);
        buf.resize(static_cast<std::size_t>(toRead) + 1);
        rangeFull.chrg.cpMin = static_cast<Scintilla::sptr_t>(start);
        rangeFull.chrg.cpMax = static_cast<Scintilla::sptr_t>(start + toRead);
        rangeFull.lpstrText = buf.data();
        const auto got = editor_.send(SCI_GETTEXTRANGEFULL, 0,
            reinterpret_cast<Scintilla::sptr_t>(&rangeFull));
        if (got <= 0) {
            throw std::runtime_error("failed to extract document content");
        }
        writtenFingerprint.update(std::string_view(buf.data(), static_cast<std::size_t>(got)));
        writer.write(std::string_view(buf.data(), static_cast<std::size_t>(got)));
        start += static_cast<std::uint64_t>(got);
    }
    writer.commit();
    info_.sizeBytes = length + (info_.hasUtf8Bom ? 3ULL : 0ULL);

    // M12: settle the save point and the external-change baseline to the
    // content that was just written.
    fingerprint_ = writtenFingerprint.value();
    baselineFingerprint_ = writtenFingerprint.value();
    savedVersion_ = version_;
    undoBytesUsed_ = 0;
}

void ScintillaDocumentBackend::setDocumentText(const std::string& bytes)
{
    if (bytes.find('\0') != std::string::npos) {
        throw std::runtime_error("embedded NUL bytes are outside the P0 document contract");
    }
    mutatingDocument_ = true;
    editor_.send(SCI_SETTEXT, 0,
        reinterpret_cast<sptr_t>(const_cast<char*>(bytes.c_str())));
    // A freshly loaded document has no undo history: without this, SCI_UNDO
    // could rewind past the load itself into an empty buffer.
    editor_.send(SCI_EMPTYUNDOBUFFER);
    mutatingDocument_ = false;
}

} // namespace mqt::backend
