// SPDX-License-Identifier: GPL-3.0-or-later
// gui_smoke_test.cpp
//
// Proves the Qt6 GUI actually runs, not just compiles: constructs the real
// MainWindow against disposable scratch filesystem state (QT_QPA_PLATFORM
// must be set to "offscreen" by the caller/CMake test runner), checks the
// folder-listing model populates from real directory contents, and drives
// one action (create-folder) through the exact same code path the toolbar
// uses, then verifies both the filesystem and the model reflect it.
//
// Ordinary new test code, not part of the clean-room reconstruction.

#include <QApplication>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "main_window.hpp"

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& p, const std::string& content) {
    std::ofstream out(p);
    out << content;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    fs::path scratch = fs::temp_directory_path() / "xfm_gui_smoke_test";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    write_file(scratch / "alpha.txt", "alpha");
    write_file(scratch / "beta.txt", "beta");
    fs::create_directories(scratch / "subdir");

    xfm::gui::MainWindow window(scratch);
    assert(window.current_directory() == scratch);

    // Three real entries: alpha.txt, beta.txt, subdir.
    assert(window.listed_row_count() == 3);
    std::printf("[ok] gui_smoke: MainWindow constructed and populated %d real entries from %s\n",
                window.listed_row_count(), scratch.c_str());

    // Drive the exact same code path the "New Folder" toolbar action uses.
    window.trigger_new_folder("brand_new_folder");
    assert(fs::is_directory(scratch / "brand_new_folder"));
    assert(window.listed_row_count() == 4);
    std::printf("[ok] gui_smoke: New Folder action created %s and the model now shows %d entries\n",
                (scratch / "brand_new_folder").c_str(), window.listed_row_count());

    // Drive the exact same code path the "Rename" toolbar action uses.
    QString original_name = window.listed_entry_name(0);
    assert(!original_name.isEmpty());
    window.trigger_rename(0, "renamed_target");
    assert(fs::exists(scratch / "renamed_target"));
    assert(!fs::exists(scratch / original_name.toStdString()));
    assert(window.listed_row_count() == 4);
    std::printf("[ok] gui_smoke: Rename action renamed \"%s\" to renamed_target via the real file_operations module\n",
                original_name.toStdString().c_str());

    fs::remove_all(scratch);
    std::printf("\nGUI smoke test passed: window constructs, model populates, actions execute for real.\n");
    return 0;
}
