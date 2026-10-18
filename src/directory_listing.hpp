// SPDX-License-Identifier: GPL-3.0-or-later
// directory_listing.hpp
//
// Clean-room, from-scratch reimplementation of the BEHAVIOR observed in
// XandrosFileManager's item/tree model (classes named via embedded Qt-moc /
// RTTI strings: CNetworkTreeItem, CListViewItem, CLocalFileItem, CHomeItem,
// CMyDocumentsItem, CFtpSiteItem, CSMBFileInfo, CWindowsTreeItem). This is
// NOT a port or transcription of any decompiled code — it is a fresh design
// that reproduces the understood behavior: a single polymorphic "browsable
// location" model that normalizes local-filesystem, SMB, and FTP entries
// into one capability-queryable item so a dual-pane UI (tree + detail view)
// can treat every kind of location uniformly.
//
// Modern target: C++20, std::filesystem, no legacy Qt2/KDE2 API assumptions.
// A real UI would sit on Qt6 (QAbstractItemModel) or GTK4 (GListModel)
// instead of the original QListView-item subclassing approach; the model
// below is UI-toolkit-agnostic on purpose.

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace xfm {

namespace fs = std::filesystem;

// Unified stat-like record. The legacy binary funneled both real local
// stat64() results and SMB directory-listing results into one struct
// (CSMBFileInfo) so capability checks never needed to branch on origin.
// We keep that idea but make it an explicit value type instead of an
// in/out parameter filled by a C-style helper.
struct EntryInfo {
    std::string name;
    std::uintmax_t size_bytes = 0;
    std::chrono::system_clock::time_point modified_time{};
    bool is_directory = false;
    bool is_symlink = false;
    bool is_hidden = false;
    bool readable = true;
    bool writable = true;
};

// Capability set a UI uses to enable/disable actions. The original code
// queried these per concrete item subclass (CanDoCopy, CanDoCut,
// CanDoNewFolder, CanRestoreFile, CanCreateSubfolder, CanRename, ...).
// We collapse that into one POD returned by each backend, since the
// *logic* observed was "ask the item, don't assume," not the specific
// C++ dispatch mechanism used to do it.
struct ItemCapabilities {
    bool can_copy = false;
    bool can_cut = false;
    bool can_delete = false;
    bool can_rename = false;
    bool can_create_subfolder = false;
    bool can_move_to_trash = false;
};

// Abstract browsable location. Concrete backends: LocalDirectory, SmbShare,
// FtpSite, TrashLocation, SpecialFolder (Home/MyDocuments-equivalents).
// This mirrors the *role* CNetworkTreeItem played (a per-location strategy
// object queried by the view layer) without mirroring its implementation.
class IBrowsableLocation {
public:
    virtual ~IBrowsableLocation() = default;

    // Human-readable path/address shown in the location bar.
    virtual std::string display_path() const = 0;

    // Lists immediate children. Returns std::nullopt on a listing error
    // (permission denied, host unreachable, etc.) rather than throwing --
    // the legacy UI clearly treated listing failures as recoverable,
    // user-facing conditions (error__12CViewManager(QString) handler),
    // not exceptional program states.
    virtual std::optional<std::vector<EntryInfo>> list_children() const = 0;

    virtual ItemCapabilities capabilities() const = 0;

    // Child location is only meaningful for directory entries.
    virtual std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const = 0;
};

// Local filesystem backend (stands in for CLocalFileItem / CHomeItem /
// CMyDocumentsItem — those were specializations distinguished mainly by
// their *root* path and a couple of capability overrides, not by distinct
// listing logic, so one parameterized class covers them cleanly here).
class LocalDirectory final : public IBrowsableLocation {
public:
    explicit LocalDirectory(fs::path path, bool is_protected_special_folder = false)
        : path_(std::move(path)), protected_special_folder_(is_protected_special_folder) {}

    std::string display_path() const override { return path_.string(); }

    std::optional<std::vector<EntryInfo>> list_children() const override {
        std::error_code ec;
        if (!fs::is_directory(path_, ec) || ec) {
            return std::nullopt;
        }

        std::vector<EntryInfo> entries;
        for (const auto& dirent : fs::directory_iterator(path_, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            entries.push_back(describe(dirent));
        }
        if (ec) {
            return std::nullopt;
        }
        return entries;
    }

    ItemCapabilities capabilities() const override {
        std::error_code ec;
        const bool writable = !protected_special_folder_ && is_path_writable(path_, ec);
        return ItemCapabilities{
            .can_copy = true,
            .can_cut = writable,
            .can_delete = writable,
            .can_rename = writable,
            .can_create_subfolder = writable,
            .can_move_to_trash = writable,
        };
    }

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<LocalDirectory>(path_ / name);
    }

private:
    static EntryInfo describe(const fs::directory_entry& dirent) {
        std::error_code ec;
        EntryInfo info;
        info.name = dirent.path().filename().string();
        info.is_hidden = !info.name.empty() && info.name.front() == '.';
        info.is_symlink = dirent.is_symlink(ec);
        info.is_directory = dirent.is_directory(ec);
        if (!info.is_directory) {
            info.size_bytes = dirent.file_size(ec);
        }
        auto ftime = dirent.last_write_time(ec);
        if (!ec) {
            // Manual epoch-relative conversion instead of std::chrono::clock_cast:
            // clock_cast's clock_time_conversion<file_clock, system_clock>
            // specialization isn't reliably available across toolchains, but
            // both clocks share the same epoch in every real implementation,
            // so a duration_cast of time_since_epoch is portable in practice.
            info.modified_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                std::chrono::system_clock::time_point{ftime.time_since_epoch()});
        }
        return info;
    }

    static bool is_path_writable(const fs::path& p, std::error_code& ec) {
        auto perms = fs::status(p, ec).permissions();
        if (ec) return false;
        return (perms & fs::perms::owner_write) != fs::perms::none;
    }

    fs::path path_;
    bool protected_special_folder_;
};

} // namespace xfm
