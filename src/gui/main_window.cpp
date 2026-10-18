// SPDX-License-Identifier: GPL-3.0-or-later
// main_window.cpp — see main_window.hpp for the clean-room/UI-engineering
// note. This file wires Qt widgets to the existing logic modules; it does
// not implement filesystem, trash, or symlink behavior itself.

#include "main_window.hpp"

#include <QAction>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QThread>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include "file_operations.hpp"
#include "symlink_dialog.hpp"
#include "transfer_worker.hpp"

namespace xfm::gui {

namespace {

fs::path default_trash_root() {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (data_dir.isEmpty()) {
        data_dir = QDir::homePath() + "/.local/share/slrxfm";
    }
    return fs::path(data_dir.toStdString()) / "trash";
}

} // namespace

MainWindow::MainWindow(fs::path start_dir, QWidget* parent)
    : QMainWindow(parent),
      current_dir_(std::move(start_dir)),
      trash_root_(default_trash_root()),
      trash_(std::make_unique<TrashManager>(trash_root_)) {
    setWindowTitle(QStringLiteral("SLRXFM — Smech Labs Remastered Xandros File Manager"));
    resize(900, 600);

    setup_ui();
    setup_actions();

    navigate_to(current_dir_);
}

void MainWindow::setup_ui() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    address_bar_ = new QLineEdit(central);
    layout->addWidget(address_bar_);
    connect(address_bar_, &QLineEdit::returnPressed, this, &MainWindow::on_address_bar_activated);

    auto* splitter = new QSplitter(Qt::Horizontal, central);

    tree_model_ = new DirectoryTreeModel(current_dir_.root_path().empty() ? current_dir_ : current_dir_.root_path(), this);
    tree_view_ = new QTreeView(splitter);
    tree_view_->setModel(tree_model_);
    tree_view_->setHeaderHidden(true);
    connect(tree_view_, &QTreeView::clicked, this, &MainWindow::on_tree_selection_changed);

    list_model_ = new DirectoryListModel(this);
    list_view_ = new QTableView(splitter);
    list_view_->setModel(list_model_);
    list_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_view_->horizontalHeader()->setStretchLastSection(true);
    list_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_view_, &QTableView::doubleClicked, this, &MainWindow::on_list_double_clicked);
    connect(list_view_, &QTableView::customContextMenuRequested, this, &MainWindow::on_list_context_menu);
    connect(list_view_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::update_action_enablement);

    splitter->addWidget(tree_view_);
    splitter->addWidget(list_view_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    layout->addWidget(splitter);
    setCentralWidget(central);
}

void MainWindow::setup_actions() {
    auto* toolbar = new QToolBar(QStringLiteral("Main"), this);
    addToolBar(toolbar);

    act_up_ = toolbar->addAction(QStringLiteral("Up"), this, &MainWindow::action_up);
    act_refresh_ = toolbar->addAction(QStringLiteral("Refresh"), this, &MainWindow::action_refresh);
    toolbar->addSeparator();
    act_new_folder_ = toolbar->addAction(QStringLiteral("New Folder"), this, &MainWindow::action_new_folder);
    toolbar->addSeparator();
    act_copy_ = toolbar->addAction(QStringLiteral("Copy"), this, &MainWindow::action_copy);
    act_cut_ = toolbar->addAction(QStringLiteral("Cut"), this, &MainWindow::action_cut);
    act_paste_ = toolbar->addAction(QStringLiteral("Paste"), this, &MainWindow::action_paste);
    act_rename_ = toolbar->addAction(QStringLiteral("Rename"), this, &MainWindow::action_rename);
    act_delete_ = toolbar->addAction(QStringLiteral("Delete"), this, &MainWindow::action_delete);
    toolbar->addSeparator();
    act_move_to_trash_ = toolbar->addAction(QStringLiteral("Move to Trash"), this, &MainWindow::action_move_to_trash);
    act_empty_trash_ = toolbar->addAction(QStringLiteral("Empty Trash"), this, &MainWindow::action_empty_trash);
    toolbar->addSeparator();
    act_create_symlink_ = toolbar->addAction(QStringLiteral("Create Symlink"), this, &MainWindow::action_create_symlink);

    update_action_enablement();
}

void MainWindow::update_action_enablement() {
    bool has_selection = !list_view_->selectionModel()->selectedRows().isEmpty();
    bool single_selection = list_view_->selectionModel()->selectedRows().size() == 1;
    act_copy_->setEnabled(has_selection);
    act_cut_->setEnabled(has_selection);
    act_delete_->setEnabled(has_selection);
    act_rename_->setEnabled(single_selection);
    act_move_to_trash_->setEnabled(has_selection);
    act_create_symlink_->setEnabled(single_selection);
    act_paste_->setEnabled(!clipboard_.empty());
}

int MainWindow::listed_row_count() const {
    return list_model_ ? list_model_->rowCount({}) : 0;
}

QString MainWindow::listed_entry_name(int row) const {
    if (const EntryInfo* entry = list_model_->entry_at(row)) {
        return QString::fromStdString(entry->name);
    }
    return {};
}

void MainWindow::navigate_to(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) {
        return;
    }
    current_dir_ = dir;
    address_bar_->setText(QString::fromStdString(current_dir_.string()));
    list_model_->set_location(std::make_unique<LocalDirectory>(current_dir_));
    update_action_enablement();
}

void MainWindow::on_address_bar_activated() {
    navigate_to(fs::path(address_bar_->text().toStdString()));
}

void MainWindow::on_tree_selection_changed() {
    QModelIndex index = tree_view_->currentIndex();
    navigate_to(tree_model_->path_for(index));
}

void MainWindow::on_list_double_clicked(const QModelIndex& index) {
    const EntryInfo* entry = list_model_->entry_at(index.row());
    if (entry && entry->is_directory) {
        navigate_to(current_dir_ / entry->name);
    }
}

void MainWindow::on_list_context_menu(const QPoint& pos) {
    QMenu menu(list_view_);
    menu.addAction(act_copy_);
    menu.addAction(act_cut_);
    menu.addAction(act_paste_);
    menu.addAction(act_rename_);
    menu.addAction(act_delete_);
    menu.addSeparator();
    menu.addAction(act_move_to_trash_);
    menu.addSeparator();
    menu.addAction(act_create_symlink_);
    menu.exec(list_view_->viewport()->mapToGlobal(pos));
}

std::vector<fs::path> MainWindow::selected_paths() const {
    std::vector<fs::path> paths;
    for (const QModelIndex& index : list_view_->selectionModel()->selectedRows()) {
        if (const EntryInfo* entry = list_model_->entry_at(index.row())) {
            paths.push_back(current_dir_ / entry->name);
        }
    }
    return paths;
}

void MainWindow::action_up() {
    fs::path parent = current_dir_.parent_path();
    if (!parent.empty() && parent != current_dir_) {
        navigate_to(parent);
    }
}

void MainWindow::action_refresh() {
    list_model_->refresh();
    tree_model_->invalidate(tree_view_->currentIndex());
}

void MainWindow::action_new_folder() {
    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"),
                                          QLineEdit::Normal, QStringLiteral("New Folder"), &ok);
    if (!ok || name.isEmpty()) return;
    trigger_new_folder(name);
}

void MainWindow::trigger_new_folder(const QString& name) {
    FileOpResult result = create_subfolder(current_dir_, name.toStdString());
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("New Folder"), QString::fromStdString(result.error_message));
        return;
    }
    action_refresh();
}

void MainWindow::action_copy() {
    clipboard_ = selected_paths();
    clipboard_is_cut_ = false;
    update_action_enablement();
}

void MainWindow::action_cut() {
    clipboard_ = selected_paths();
    clipboard_is_cut_ = true;
    update_action_enablement();
}

ConflictResolution MainWindow::ask_paste_conflict(const fs::path& destination) {
    // Invoked on the GUI thread only -- either directly (this method isn't
    // reached any other way) or marshaled there by TransferWorker via
    // QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection), so a
    // real modal dialog is always safe to show here.
    auto answer = QMessageBox::question(
        this, QStringLiteral("File exists"),
        QStringLiteral("\"%1\" already exists here. Overwrite?").arg(QString::fromStdString(destination.filename().string())),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (answer == QMessageBox::Yes) return ConflictResolution::Overwrite;
    if (answer == QMessageBox::No) return ConflictResolution::Skip;
    return ConflictResolution::Cancel;
}

void MainWindow::action_paste() {
    if (clipboard_.empty() || transfer_in_progress_) return;

    auto mode = clipboard_is_cut_ ? BulkTransferJob::Mode::Move : BulkTransferJob::Mode::Copy;
    auto* worker = new TransferWorker(clipboard_, current_dir_, mode, this,
                                       [this](const fs::path& destination) { return ask_paste_conflict(destination); });
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    transfer_in_progress_ = true;
    act_paste_->setEnabled(false);

    delete progress_dialog_;
    progress_dialog_ = new QProgressDialog(QStringLiteral("Transferring..."), QString(), 0, 100, this);
    progress_dialog_->setWindowModality(Qt::NonModal);
    progress_dialog_->setMinimumDuration(200);
    progress_dialog_->setAutoClose(false);
    progress_dialog_->setAutoReset(false);

    connect(thread, &QThread::started, worker, &TransferWorker::run);
    connect(worker, &TransferWorker::progressChanged, this, &MainWindow::on_transfer_progress);
    connect(worker, &TransferWorker::finished, this, &MainWindow::on_transfer_finished);
    connect(worker, &TransferWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    if (clipboard_is_cut_) {
        clipboard_.clear();
    }

    thread->start();
}

void MainWindow::on_transfer_progress(qint64 bytes_done, qint64 bytes_total, QString current_item) {
    if (!progress_dialog_) return;
    if (bytes_total > 0) {
        progress_dialog_->setMaximum(static_cast<int>(bytes_total));
        progress_dialog_->setValue(static_cast<int>(bytes_done));
    }
    progress_dialog_->setLabelText(QStringLiteral("Transferring \"%1\"...").arg(current_item));
}

void MainWindow::on_transfer_finished(bool success, QString error_message) {
    transfer_in_progress_ = false;
    if (progress_dialog_) {
        progress_dialog_->close();
        progress_dialog_->deleteLater();
        progress_dialog_ = nullptr;
    }
    if (!success) {
        QMessageBox::warning(this, QStringLiteral("Paste"), error_message);
    }
    update_action_enablement();
    action_refresh();
}

void MainWindow::action_delete() {
    auto paths = selected_paths();
    if (paths.empty()) return;
    auto answer = QMessageBox::question(this, QStringLiteral("Delete"),
                                         QStringLiteral("Permanently delete %1 item(s)? This cannot be undone.").arg(paths.size()));
    if (answer != QMessageBox::Yes) return;

    for (const auto& path : paths) {
        FileOpResult result = delete_entry(path);
        if (!result.success) {
            QMessageBox::warning(this, QStringLiteral("Delete"), QString::fromStdString(result.error_message));
        }
    }
    action_refresh();
}

void MainWindow::action_rename() {
    auto rows = list_view_->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    int row = rows.front().row();
    const EntryInfo* entry = list_model_->entry_at(row);
    if (!entry) return;

    bool ok = false;
    QString new_name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"),
                                              QLineEdit::Normal, QString::fromStdString(entry->name), &ok);
    if (!ok || new_name.isEmpty()) return;
    trigger_rename(row, new_name);
}

void MainWindow::trigger_rename(int row, const QString& new_name) {
    const EntryInfo* entry = list_model_->entry_at(row);
    if (!entry) return;
    FileOpResult result = rename_entry(current_dir_ / entry->name, new_name.toStdString());
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("Rename"), QString::fromStdString(result.error_message));
        return;
    }
    action_refresh();
}

void MainWindow::action_move_to_trash() {
    auto paths = selected_paths();
    if (paths.empty()) return;
    for (const auto& path : paths) {
        if (!trash_->move_to_trash(path)) {
            QMessageBox::warning(this, QStringLiteral("Move to Trash"),
                                  QStringLiteral("Could not move \"%1\" to trash.").arg(QString::fromStdString(path.filename().string())));
        }
    }
    action_refresh();
}

void MainWindow::action_empty_trash() {
    if (trash_->is_empty()) {
        QMessageBox::information(this, QStringLiteral("Empty Trash"), QStringLiteral("Trash is already empty."));
        return;
    }
    auto answer = QMessageBox::question(this, QStringLiteral("Empty Trash"),
                                         QStringLiteral("Permanently delete all items in the trash?"));
    if (answer != QMessageBox::Yes) return;
    if (!trash_->empty_trash()) {
        QMessageBox::warning(this, QStringLiteral("Empty Trash"), QStringLiteral("Could not empty the trash."));
    }
}

void MainWindow::action_create_symlink() {
    auto rows = list_view_->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    const EntryInfo* entry = list_model_->entry_at(rows.front().row());
    if (!entry) return;

    fs::path source = current_dir_ / entry->name;
    SymlinkDialogController controller(source, entry->name + " (link)", current_dir_);

    bool ok = false;
    QString link_name = QInputDialog::getText(this, QStringLiteral("Create Symlink"), QStringLiteral("Link name:"),
                                               QLineEdit::Normal, QString::fromStdString(controller.suggested_name()), &ok);
    if (!ok || link_name.isEmpty()) return;

    controller.set_link_name(link_name.toStdString());
    SymlinkDialogResult result = controller.validate_and_accept();
    if (result.outcome != SymlinkDialogOutcome::Accepted) {
        QMessageBox::warning(this, QStringLiteral("Create Symlink"), QString::fromStdString(result.error));
        return;
    }
    if (!SymlinkDialogController::create_symlink(result)) {
        QMessageBox::warning(this, QStringLiteral("Create Symlink"), QStringLiteral("Failed to create the symlink."));
        return;
    }
    action_refresh();
}

} // namespace xfm::gui
