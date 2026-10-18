// SPDX-License-Identifier: GPL-3.0-or-later
// symlink_dialog.hpp
//
// Clean-room reimplementation of the BEHAVIOR observed for CSymLinkDialog /
// CSymLinkDialogData in XandrosFileManager. Evidence is limited to
// constructor signatures recovered from embedded strings:
//   CSymLinkDialog(const char*, const char*, QWidget*, const char*)
//   CSymLinkDialogData(QWidget*, const char*)
// which is the classic Qt2 KDE pattern of separating a dialog's editable
// field state ("Data" object) from the dialog shell itself, constructed
// with a source path, a suggested link name, a parent widget, and an
// internal Qt object name. No button/validation logic was decompiled for
// this pass (flagged in analysis/notes.md) -- the behavior below is a
// plausible, conservative reconstruction of what such a dialog does
// (confirm source + let the user edit the destination link name/location,
// validate before accept), written fresh rather than inferred
// instruction-by-instruction.
//
// Modern target: this is written as a UI-framework-agnostic controller/
// presenter (validation + accept/cancel outcome) that a Qt6 QDialog or a
// GTK4 dialog could both wrap; no legacy Qt2 signal/slot macros are used.

#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace xfm {

namespace fs = std::filesystem;

enum class SymlinkDialogOutcome { Accepted, Cancelled };

struct SymlinkDialogResult {
    SymlinkDialogOutcome outcome = SymlinkDialogOutcome::Cancelled;
    fs::path link_path;      // where the new symlink will be created
    fs::path link_target;    // what it points at (the original source)
    std::string error;       // set when validation rejected an Accept attempt
};

// Presenter/controller for "create a symbolic link to <source>". A UI layer
// owns the actual widgets and calls validate_and_accept() when the user
// presses OK, and cancel() when they dismiss the dialog.
class SymlinkDialogController {
public:
    SymlinkDialogController(fs::path source_path, std::string suggested_link_name, fs::path destination_dir)
        : source_path_(std::move(source_path)),
          link_name_(std::move(suggested_link_name)),
          destination_dir_(std::move(destination_dir)) {}

    const std::string& suggested_name() const { return link_name_; }

    // Called as the user edits the proposed link name field.
    void set_link_name(std::string name) { link_name_ = std::move(name); }

    SymlinkDialogResult validate_and_accept() const {
        SymlinkDialogResult result;
        result.link_target = source_path_;
        result.link_path = destination_dir_ / link_name_;

        if (link_name_.empty()) {
            result.error = "Link name cannot be empty";
            return result;
        }
        if (link_name_.find('/') != std::string::npos) {
            result.error = "Link name cannot contain a path separator";
            return result;
        }

        std::error_code ec;
        if (!fs::exists(source_path_, ec)) {
            result.error = "Source no longer exists";
            return result;
        }
        if (fs::exists(result.link_path, ec)) {
            result.error = "File or folder with such name already exists";
            return result;
        }

        result.outcome = SymlinkDialogOutcome::Accepted;
        return result;
    }

    static SymlinkDialogResult cancel() {
        return SymlinkDialogResult{SymlinkDialogOutcome::Cancelled, {}, {}, {}};
    }

    // Performs the actual filesystem operation once validate_and_accept()
    // has returned Accepted; kept separate from validation so a UI can
    // show validation errors inline without touching the filesystem.
    static bool create_symlink(const SymlinkDialogResult& accepted_result) {
        if (accepted_result.outcome != SymlinkDialogOutcome::Accepted) return false;
        std::error_code ec;
        if (fs::is_directory(accepted_result.link_target, ec)) {
            fs::create_directory_symlink(accepted_result.link_target, accepted_result.link_path, ec);
        } else {
            fs::create_symlink(accepted_result.link_target, accepted_result.link_path, ec);
        }
        return !ec;
    }

private:
    fs::path source_path_;
    std::string link_name_;
    fs::path destination_dir_;
};

} // namespace xfm
