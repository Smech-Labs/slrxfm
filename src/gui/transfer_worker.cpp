// SPDX-License-Identifier: GPL-3.0-or-later
// transfer_worker.cpp -- see transfer_worker.hpp for the threading design.

#include "transfer_worker.hpp"

#include <QMetaObject>

namespace xfm::gui {

TransferWorker::TransferWorker(std::vector<fs::path> sources, fs::path destination_dir, BulkTransferJob::Mode mode,
                                QObject* gui_thread_object, ConflictAsker ask_conflict)
    : sources_(std::move(sources)),
      destination_dir_(std::move(destination_dir)),
      mode_(mode),
      gui_thread_object_(gui_thread_object),
      ask_conflict_(std::move(ask_conflict)) {}

void TransferWorker::run() {
    BulkTransferJob job(sources_, destination_dir_, mode_);

    auto conflict_cb = [this](const fs::path& destination) -> ConflictResolution {
        ConflictResolution resolution = ConflictResolution::Cancel;
        // Blocks this worker thread (not the GUI thread) until the GUI
        // thread's event loop runs the lambda and a real dialog answer
        // comes back.
        QMetaObject::invokeMethod(
            gui_thread_object_,
            [this, destination, &resolution]() { resolution = ask_conflict_(destination); },
            Qt::BlockingQueuedConnection);
        return resolution;
    };

    auto progress_cb = [this](const FileOpProgress& progress) {
        emit progressChanged(static_cast<qint64>(progress.bytes_done), static_cast<qint64>(progress.bytes_total),
                              QString::fromStdString(progress.current_item));
    };

    FileOpResult result = job.run(conflict_cb, progress_cb);
    emit finished(result.success, QString::fromStdString(result.error_message));
}

} // namespace xfm::gui
