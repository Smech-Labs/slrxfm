// SPDX-License-Identifier: GPL-3.0-or-later
// transfer_worker.hpp
//
// New UI engineering (not reverse-engineered): runs BulkTransferJob
// (file_operations.hpp) on a worker QThread instead of blocking the GUI
// thread, closing the gap flagged in docs/ARCHITECTURE.md's roadmap
// ("BulkTransferJob::run() is still synchronous/blocking, called directly
// from the GUI thread").
//
// The tricky part isn't running the copy off-thread -- it's that
// BulkTransferJob's ConflictCallback must still be answered with a real
// QMessageBox, which only a GUI-thread event loop may show. This class
// takes the conflict callback as a std::function the caller (MainWindow)
// builds -- showing the dialog -- and marshals every invocation back onto
// whichever QObject the caller designates as living on the GUI thread via
// QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection), which
// blocks only the worker thread while the GUI thread's event loop (still
// running normally, since starting the worker thread returns immediately)
// services the request. This is the standard Qt pattern for a worker
// thread that needs a synchronous answer from the GUI thread.

#pragma once

#include <QObject>
#include <QString>

#include <filesystem>
#include <functional>
#include <vector>

#include "file_operations.hpp"

namespace xfm::gui {

namespace fs = std::filesystem;

class TransferWorker : public QObject {
    Q_OBJECT

public:
    using ConflictAsker = std::function<ConflictResolution(const fs::path&)>;

    // `gui_thread_object` must be a QObject living on the GUI thread (the
    // MainWindow itself, typically) -- it is used only as the target of
    // the blocking-queued invocation that runs `ask_conflict` safely.
    TransferWorker(std::vector<fs::path> sources, fs::path destination_dir, BulkTransferJob::Mode mode,
                   QObject* gui_thread_object, ConflictAsker ask_conflict);

public slots:
    void run();

signals:
    void progressChanged(qint64 bytes_done, qint64 bytes_total, QString current_item);
    void finished(bool success, QString error_message);

private:
    std::vector<fs::path> sources_;
    fs::path destination_dir_;
    BulkTransferJob::Mode mode_;
    QObject* gui_thread_object_;
    ConflictAsker ask_conflict_;
};

} // namespace xfm::gui
