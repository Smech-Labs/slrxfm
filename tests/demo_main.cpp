// SPDX-License-Identifier: GPL-3.0-or-later
// demo_main.cpp
//
// Exercises every clean-room module against disposable scratch data to
// prove the reimplementation actually works end-to-end, not just compiles.
// Not part of the behavioral reconstruction itself -- this is ordinary,
// fresh test-harness code.

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "directory_listing.hpp"
#include "file_operations.hpp"
#include "network_browsing.hpp"
#include "symlink_dialog.hpp"
#include "trash_manager.hpp"

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& p, const std::string& content) {
    std::ofstream out(p);
    out << content;
}

void test_directory_listing(const fs::path& root) {
    fs::path dir = root / "docs";
    fs::create_directories(dir);
    write_file(dir / "a.txt", "hello");
    write_file(dir / ".hidden", "secret");

    xfm::LocalDirectory local(dir);
    auto entries = local.list_children();
    assert(entries.has_value());
    assert(entries->size() == 2);

    bool saw_hidden = false, saw_visible = false;
    for (const auto& e : *entries) {
        if (e.name == ".hidden") { assert(e.is_hidden); saw_hidden = true; }
        if (e.name == "a.txt") { assert(!e.is_hidden); assert(e.size_bytes == 5); saw_visible = true; }
    }
    assert(saw_hidden && saw_visible);
    assert(local.capabilities().can_copy);
    std::printf("[ok] directory_listing: listed %zu entries in %s\n", entries->size(), dir.c_str());
}

void test_file_operations(const fs::path& root) {
    fs::path src_dir = root / "src";
    fs::path dst_dir = root / "dst";
    fs::create_directories(src_dir);
    fs::create_directories(dst_dir);
    write_file(src_dir / "file1.txt", "data");

    xfm::BulkTransferJob copy_job({src_dir / "file1.txt"}, dst_dir, xfm::BulkTransferJob::Mode::Copy);
    auto result = copy_job.run(
        [](const fs::path&) { return xfm::ConflictResolution::Overwrite; },
        [](const xfm::FileOpProgress&) {});
    assert(result.success);
    assert(fs::exists(dst_dir / "file1.txt"));
    assert(fs::exists(src_dir / "file1.txt")); // copy, not move -- source survives

    auto rename_result = xfm::rename_entry(dst_dir / "file1.txt", "file1_renamed.txt");
    assert(rename_result.success);
    assert(fs::exists(dst_dir / "file1_renamed.txt"));

    auto delete_result = xfm::delete_entry(dst_dir / "file1_renamed.txt");
    assert(delete_result.success);
    assert(!fs::exists(dst_dir / "file1_renamed.txt"));

    std::printf("[ok] file_operations: copy, rename, delete round-tripped cleanly\n");
}

void test_trash_manager(const fs::path& root) {
    fs::path work_dir = root / "work";
    fs::path trash_dir = root / "trash";
    fs::create_directories(work_dir);
    write_file(work_dir / "doomed.txt", "goodbye");

    xfm::TrashManager trash(trash_dir);
    assert(trash.is_empty());
    assert(trash.can_move_to_trash(work_dir / "doomed.txt"));
    assert(trash.move_to_trash(work_dir / "doomed.txt"));
    assert(!fs::exists(work_dir / "doomed.txt"));
    assert(!trash.is_empty());

    auto entries = trash.list_entries();
    assert(entries.size() == 1);
    assert(entries[0].original_path == (work_dir / "doomed.txt").string());

    assert(trash.restore(entries[0].trashed_name));
    assert(fs::exists(work_dir / "doomed.txt"));
    assert(trash.is_empty());

    std::printf("[ok] trash_manager: move-to-trash and restore round-tripped cleanly\n");
}

void test_symlink_dialog(const fs::path& root) {
    fs::path target = root / "target.txt";
    write_file(target, "linked content");
    fs::path dest_dir = root;

    xfm::SymlinkDialogController controller(target, "target_link.txt", dest_dir);
    auto result = controller.validate_and_accept();
    assert(result.outcome == xfm::SymlinkDialogOutcome::Accepted);
    assert(xfm::SymlinkDialogController::create_symlink(result));
    assert(fs::is_symlink(dest_dir / "target_link.txt"));

    // Re-running with the same name must now fail validation (collision).
    xfm::SymlinkDialogController collide(target, "target_link.txt", dest_dir);
    auto collide_result = collide.validate_and_accept();
    assert(collide_result.outcome == xfm::SymlinkDialogOutcome::Cancelled);
    assert(!collide_result.error.empty());

    std::printf("[ok] symlink_dialog: accept + collision-rejection both behave correctly\n");
}

void test_network_browsing() {
    // No real network I/O -- just proves the polymorphic interface and the
    // async listing facade actually compile and run against each backend.
    xfm::CredentialPrompt no_creds = [](const std::string&) { return std::nullopt; };

    xfm::SmbShare smb("fileserver", "public", "", no_creds);
    auto smb_result = smb.list_children(); // not connected -> nullopt + error
    assert(!smb_result.has_value());

    xfm::FtpSite ftp("ftp.example.org", "/pub", no_creds);
    auto ftp_result = ftp.list_children(); // no credentials -> nullopt + error
    assert(!ftp_result.has_value());

    xfm::WebDavSite webdav("dav.example.org", "/files", no_creds);
    auto webdav_result = webdav.list_children(); // unverified backend -> explicit error
    assert(!webdav_result.has_value());

    std::printf("[ok] network_browsing: SMB/FTP/WebDAV backends instantiate and fail closed as expected\n");
}

} // namespace

int main() {
    fs::path scratch = fs::temp_directory_path() / "xfm_clean_impl_demo";
    fs::remove_all(scratch);
    fs::create_directories(scratch);

    test_directory_listing(scratch);
    test_file_operations(scratch);
    test_trash_manager(scratch);
    test_symlink_dialog(scratch);
    test_network_browsing();

    fs::remove_all(scratch);
    std::printf("\nAll clean-room modules exercised successfully.\n");
    return 0;
}
