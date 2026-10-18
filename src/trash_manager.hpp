// SPDX-License-Identifier: GPL-3.0-or-later
// trash_manager.hpp
//
// Clean-room reimplementation of the BEHAVIOR observed for Trash/"Dumpster"
// handling in XandrosFileManager (evidence: CTrashItem, CTrashEntryInfo,
// CTrashEntryItem, IsTrashEmpty, emptyTrash__10CTrashItem, slotEmptyDumpster,
// slotEmptyTrash, the literal path string "/Desktop/Trash", and
// CanMoveToTrash/CanRestoreFile capability predicates). The two call sites
// slotEmptyDumpster() and slotEmptyTrash() both existing in the string
// table is flagged in analysis/notes.md as an ambiguity -- here we model it
// as ONE mechanism with two historical user-facing names, since the
// observed capability surface (IsTrashEmpty/EmptyTrash/CanRestoreFile) is
// singular.
//
// This is a fresh design of the mechanism, not a transcription of any
// decompiled routine. Modern target: C++20 + std::filesystem. A real
// desktop integration today would prefer the freedesktop.org Trash spec
// (XDG_DATA_HOME/Trash/{files,info}, with .trashinfo sidecar files) over a
// bespoke "~/Desktop/Trash" directory -- noted inline below.

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace xfm {

namespace fs = std::filesystem;

// Mirrors CTrashEntryInfo: enough metadata to restore an item to where it
// came from, kept alongside (not inside) the trashed content itself.
struct TrashEntryInfo {
    std::string trashed_name;   // name actually used inside the trash dir
    std::string original_path;  // absolute path before trashing
    std::chrono::system_clock::time_point trashed_at;
};

class TrashManager {
public:
    // Legacy behavior used a fixed "<Desktop>/Trash" directory. We keep the
    // same "one well-known directory" idea but make the root configurable
    // so a modern port can point it at the XDG trash location instead.
    explicit TrashManager(fs::path trash_root) : trash_root_(std::move(trash_root)) {
        std::error_code ec;
        fs::create_directories(trash_root_, ec);
    }

    // CanMoveToTrash(CNetworkTreeItem*) equivalent: only real, writable,
    // local filesystem entries outside the trash itself are eligible.
    bool can_move_to_trash(const fs::path& path) const {
        std::error_code ec;
        if (!fs::exists(path, ec) || ec) return false;
        if (is_inside_trash(path)) return false;
        auto parent_perms = fs::status(path.parent_path(), ec).permissions();
        if (ec) return false;
        return (parent_perms & fs::perms::owner_write) != fs::perms::none;
    }

    // Moves a path into the trash directory and records restore metadata.
    // This is the real-world meaning of "emptyTrash__10CTrashItem"'s sibling
    // operation (trashing one item) -- the legacy name suggested per-item
    // trash objects (CTrashItem) each knew how to insert themselves.
    bool move_to_trash(const fs::path& path) {
        if (!can_move_to_trash(path)) return false;

        std::string trashed_name = unique_trash_name(path.filename().string());
        fs::path trashed_path = trash_root_ / trashed_name;

        std::error_code ec;
        fs::rename(path, trashed_path, ec);
        if (ec) return false;

        write_sidecar(trashed_name, path.string());
        return true;
    }

    // CanRestoreFile / restore flow: move back to original_path if free.
    bool restore(const std::string& trashed_name) {
        auto info = read_sidecar(trashed_name);
        if (!info) return false;

        std::error_code ec;
        if (fs::exists(info->original_path, ec)) {
            return false; // original location reoccupied; caller should prompt
        }

        fs::rename(trash_root_ / trashed_name, info->original_path, ec);
        if (ec) return false;

        fs::remove(sidecar_path(trashed_name), ec);
        return true;
    }

    // IsTrashEmpty()
    bool is_empty() const {
        std::error_code ec;
        if (!fs::exists(trash_root_, ec)) return true;
        for (const auto& entry : fs::directory_iterator(trash_root_, ec)) {
            if (entry.path().extension() != ".trashinfo") {
                return false;
            }
        }
        return true;
    }

    // EmptyTrash() / slotEmptyDumpster() / slotEmptyTrash(): permanently
    // delete everything, including sidecar metadata.
    bool empty_trash() {
        std::error_code ec;
        if (!fs::exists(trash_root_, ec)) return true;
        for (const auto& entry : fs::directory_iterator(trash_root_, ec)) {
            fs::remove_all(entry.path(), ec);
            if (ec) return false;
        }
        return true;
    }

    std::vector<TrashEntryInfo> list_entries() const {
        std::vector<TrashEntryInfo> result;
        std::error_code ec;
        if (!fs::exists(trash_root_, ec)) return result;
        for (const auto& entry : fs::directory_iterator(trash_root_, ec)) {
            if (entry.path().extension() != ".trashinfo") continue;
            if (auto info = read_sidecar(entry.path().stem().string())) {
                result.push_back(*info);
            }
        }
        return result;
    }

private:
    bool is_inside_trash(const fs::path& path) const {
        std::error_code ec;
        auto rel = fs::relative(path, trash_root_, ec);
        return !ec && !rel.empty() && rel.native().front() != '.';
    }

    std::string unique_trash_name(const std::string& preferred) const {
        fs::path candidate = trash_root_ / preferred;
        std::string name = preferred;
        int suffix = 1;
        while (fs::exists(candidate)) {
            fs::path p(preferred);
            name = p.stem().string() + "_" + std::to_string(suffix++) + p.extension().string();
            candidate = trash_root_ / name;
        }
        return name;
    }

    fs::path sidecar_path(const std::string& trashed_name) const {
        return trash_root_ / (trashed_name + ".trashinfo");
    }

    void write_sidecar(const std::string& trashed_name, const std::string& original_path) const {
        std::ofstream out(sidecar_path(trashed_name));
        out << "OriginalPath=" << original_path << '\n';
        auto now = std::chrono::system_clock::now();
        out << "TrashedAtEpochSeconds="
            << std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() << '\n';
    }

    std::optional<TrashEntryInfo> read_sidecar(const std::string& trashed_name) const {
        std::ifstream in(sidecar_path(trashed_name));
        if (!in) return std::nullopt;

        TrashEntryInfo info;
        info.trashed_name = trashed_name;
        std::string line;
        while (std::getline(in, line)) {
            constexpr std::string_view path_prefix = "OriginalPath=";
            if (line.rfind(path_prefix, 0) == 0) {
                info.original_path = line.substr(path_prefix.size());
            }
        }
        return info;
    }

    fs::path trash_root_;
};

} // namespace xfm
