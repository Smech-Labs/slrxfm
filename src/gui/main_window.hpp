// SPDX-License-Identifier: GPL-3.0-or-later
// main_window.hpp
//
// New UI engineering (not reverse-engineered): the SLRXFM main window.
// It is a thin Qt6 shell -- folder tree + address bar + file-detail table
// -- that calls into the existing clean-room logic modules
// (file_operations.hpp, trash_manager.hpp, symlink_dialog.hpp,
// directory_listing.hpp) for every actual filesystem operation. No file
// operation, trash, or symlink logic is reimplemented here.
//
// Overall layout (toolbar above a tree+detail split, address bar between
// them) is an ordinary, generic file-manager convention; no Xandros
// screenshot or shipped UI resource was consulted or reproduced. See
// docs/CLEAN_ROOM.md for why that distinction matters for this project.

#pragma once

#include <QMainWindow>

#include <filesystem>

#include "directory_tree_model.hpp"
#include "directory_list_model.hpp"
#include "file_operations.hpp"
#include "trash_manager.hpp"

class QLineEdit;
class QTreeView;
class QTableView;
class QAction;
class QToolBar;
class QProgressDialog;

namespace xfm::gui {

namespace fs = std::filesystem;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    // `start_dir` lets tests point the window at disposable scratch state
    // instead of the real user's home directory.
    explicit MainWindow(fs::path start_dir, QWidget* parent = nullptr);

    // Exposed for headless testing: current directory, and the live list
    // model's row count, without reaching into private widgets.
    fs::path current_directory() const { return current_dir_; }
    int listed_row_count() const;
    QString listed_entry_name(int row) const;

    // Test hooks that invoke the exact same code path the toolbar/menu
    // actions use, so a headless test proves the real wiring, not a
    // separate stand-in.
    void trigger_new_folder(const QString& name);
    void trigger_rename(int row, const QString& new_name);

private slots:
    void navigate_to(const fs::path& dir);
    void on_address_bar_activated();
    void on_tree_selection_changed();
    void on_list_double_clicked(const QModelIndex& index);
    void on_list_context_menu(const QPoint& pos);

    void action_up();
    void action_refresh();
    void action_new_folder();
    void action_copy();
    void action_cut();
    void action_paste();
    void action_delete();
    void action_rename();
    void action_move_to_trash();
    void action_empty_trash();
    void action_create_symlink();

    // Connected to TransferWorker's signals -- see transfer_worker.hpp.
    // Always run on this (GUI) thread, regardless of which thread the
    // worker emits from, because Qt auto-queues cross-thread signal
    // delivery for AutoConnection connections.
    void on_transfer_progress(qint64 bytes_done, qint64 bytes_total, QString current_item);
    void on_transfer_finished(bool success, QString error_message);

private:
    void setup_ui();
    void setup_actions();
    void update_action_enablement();
    std::vector<fs::path> selected_paths() const;
    ConflictResolution ask_paste_conflict(const fs::path& destination);

    fs::path current_dir_;
    fs::path trash_root_;
    std::unique_ptr<TrashManager> trash_;

    std::vector<fs::path> clipboard_;
    bool clipboard_is_cut_ = false;
    bool transfer_in_progress_ = false;
    QProgressDialog* progress_dialog_ = nullptr;

    QLineEdit* address_bar_ = nullptr;
    QTreeView* tree_view_ = nullptr;
    QTableView* list_view_ = nullptr;
    DirectoryTreeModel* tree_model_ = nullptr;
    DirectoryListModel* list_model_ = nullptr;

    QAction* act_up_ = nullptr;
    QAction* act_refresh_ = nullptr;
    QAction* act_new_folder_ = nullptr;
    QAction* act_copy_ = nullptr;
    QAction* act_cut_ = nullptr;
    QAction* act_paste_ = nullptr;
    QAction* act_delete_ = nullptr;
    QAction* act_rename_ = nullptr;
    QAction* act_move_to_trash_ = nullptr;
    QAction* act_empty_trash_ = nullptr;
    QAction* act_create_symlink_ = nullptr;
};

} // namespace xfm::gui
