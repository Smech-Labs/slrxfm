// SPDX-License-Identifier: GPL-3.0-or-later
// file_operations.hpp
//
// Clean-room reimplementation of the BEHAVIOR observed for file copy/move/
// delete/rename in XandrosFileManager. Evidence (from strings/embedded
// Qt-moc tables, not decompiled logic): CopyRequest/CutRequest/DeleteRequest
// handlers on CRightPanel/CLeftTreeView forward to CViewManager, which does
// NOT perform the byte copying itself -- the binary imports fork/execv/
// popen/system and ships a separate "CopyAgent" helper executable, strongly
// suggesting copy/move operations are delegated to an out-of-process worker
// (keeping the GUI responsive and sharing one progress/IPC implementation
// across XandrosFileManager, CopyAgent and FtpAgent). Delete/rename, by
// contrast, look in-process and name-collision-checked ("File or folder
// with such name already exists"-shaped string) before proceeding.
//
// This file re-expresses that *design*, not any decompiled routine: an
// async job abstraction for copy/move (external-process-shaped, like the
// original), and synchronous, collision-checked delete/rename.
//
// Modern target: C++20, std::filesystem; a real app would likely use a
// std::jthread + std::stop_token worker (or a QThread/QProcess pair under
// Qt6) instead of spawning a whole second executable, but the external-
// helper-process shape is kept here deliberately since it is the behavior
// actually observed (and it still has the real-world benefit that a crash
// mid-copy cannot bring down the file manager window).

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace xfm {

namespace fs = std::filesystem;

enum class ConflictResolution { Overwrite, Skip, Rename, Cancel };

// Called synchronously by the file-op layer whenever the destination
// already has an entry with the same name. Mirrors the "Do you want to
// overwrite existing file" confirmation observed in the string table.
using ConflictCallback = std::function<ConflictResolution(const fs::path& destination)>;

struct FileOpProgress {
    std::uintmax_t bytes_done = 0;
    std::uintmax_t bytes_total = 0;
    std::string current_item;
};

using ProgressCallback = std::function<void(const FileOpProgress&)>;

struct FileOpResult {
    bool success = false;
    std::string error_message; // empty on success
};

// Represents one bulk copy/move job, mirroring the out-of-process
// "CopyAgent" delegation model: the caller enqueues a batch of sources
// bound for one destination directory, then drives it to completion
// (here modeled as a blocking call returning a result; a real
// implementation would run this on a worker thread/process and surface
// progress via the callback as the original UI clearly did).
class BulkTransferJob {
public:
    enum class Mode { Copy, Move };

    BulkTransferJob(std::vector<fs::path> sources, fs::path destination_dir, Mode mode)
        : sources_(std::move(sources)), destination_dir_(std::move(destination_dir)), mode_(mode) {}

    FileOpResult run(const ConflictCallback& on_conflict, const ProgressCallback& on_progress) const {
        std::error_code ec;
        if (!fs::is_directory(destination_dir_, ec) || ec) {
            return {false, "Destination is not a directory"};
        }

        std::uintmax_t total_bytes = compute_total_bytes();
        std::uintmax_t done_bytes = 0;

        for (const auto& source : sources_) {
            fs::path dest = destination_dir_ / source.filename();

            if (fs::exists(dest, ec)) {
                ConflictResolution resolution = on_conflict ? on_conflict(dest) : ConflictResolution::Cancel;
                switch (resolution) {
                    case ConflictResolution::Cancel:
                        return {false, "Cancelled by user"};
                    case ConflictResolution::Skip:
                        continue;
                    case ConflictResolution::Rename:
                        dest = unique_sibling_name(dest);
                        break;
                    case ConflictResolution::Overwrite:
                        fs::remove_all(dest, ec);
                        break;
                }
            }

            const auto copy_options = fs::copy_options::recursive | fs::copy_options::copy_symlinks;
            fs::copy(source, dest, copy_options, ec);
            if (ec) {
                return {false, "Failed to transfer: " + source.string() + " (" + ec.message() + ")"};
            }

            if (mode_ == Mode::Move) {
                fs::remove_all(source, ec);
                if (ec) {
                    return {false, "Copied but could not remove source: " + source.string()};
                }
            }

            std::error_code size_ec;
            done_bytes += fs::is_directory(dest, size_ec) ? 0 : fs::file_size(dest, size_ec);
            if (on_progress) {
                on_progress(FileOpProgress{done_bytes, total_bytes, source.filename().string()});
            }
        }

        return {true, {}};
    }

private:
    std::uintmax_t compute_total_bytes() const {
        std::uintmax_t total = 0;
        std::error_code ec;
        for (const auto& source : sources_) {
            if (fs::is_regular_file(source, ec)) {
                total += fs::file_size(source, ec);
            }
        }
        return total;
    }

    static fs::path unique_sibling_name(const fs::path& preferred) {
        fs::path candidate = preferred;
        int suffix = 1;
        while (fs::exists(candidate)) {
            candidate = preferred.parent_path() /
                        (preferred.stem().string() + " (" + std::to_string(suffix++) + ")" + preferred.extension().string());
        }
        return candidate;
    }

    std::vector<fs::path> sources_;
    fs::path destination_dir_;
    Mode mode_;
};

// Rename: in-process, synchronous, collision-checked -- matches the
// in-place-label-edit behavior implied by fileRename()/CheckLabels().
inline FileOpResult rename_entry(const fs::path& existing_path, const std::string& new_name) {
    if (new_name.empty() || new_name.find('/') != std::string::npos) {
        return {false, "Invalid name"};
    }
    fs::path target = existing_path.parent_path() / new_name;
    std::error_code ec;
    if (fs::exists(target, ec)) {
        return {false, "File or folder with such name already exists"};
    }
    fs::rename(existing_path, target, ec);
    if (ec) {
        return {false, ec.message()};
    }
    return {true, {}};
}

// Delete: capability-gated by the caller (see directory_listing.hpp's
// ItemCapabilities::can_delete) before this is ever invoked, matching
// CanDelete()/CanDeleteLocalPath() guarding DeleteRequest().
inline FileOpResult delete_entry(const fs::path& path) {
    std::error_code ec;
    auto removed = fs::remove_all(path, ec);
    if (ec) {
        return {false, ec.message()};
    }
    if (removed == 0) {
        return {false, "Nothing was deleted (path did not exist)"};
    }
    return {true, {}};
}

} // namespace xfm
