// SPDX-License-Identifier: GPL-3.0-or-later
// transfer_worker_test.cpp
//
// Proves TransferWorker (src/gui/transfer_worker.hpp) actually runs
// BulkTransferJob on a worker QThread and correctly marshals its
// ConflictCallback back onto the calling (here: main) thread via
// QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection) --
// the exact mechanism MainWindow::action_paste() relies on, but
// exercised directly and headlessly (QtCore only, no QApplication/
// widgets/display needed) rather than through the full GUI.
//
// Ordinary new test code, not part of the clean-room reconstruction.

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#include "transfer_worker.hpp"

namespace fs = std::filesystem;
using xfm::gui::TransferWorker;
using xfm::BulkTransferJob;
using xfm::ConflictResolution;

namespace {

void write_file(const fs::path& p, const std::string& content) {
    std::ofstream out(p);
    out << content;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    fs::path scratch = fs::temp_directory_path() / "xfm_transfer_worker_test";
    fs::remove_all(scratch);
    fs::path src_dir = scratch / "src";
    fs::path dst_dir = scratch / "dst";
    fs::create_directories(src_dir);
    fs::create_directories(dst_dir);
    write_file(src_dir / "payload.txt", "some real bytes to copy");
    // Pre-existing file at the destination forces the conflict path.
    write_file(dst_dir / "collide.txt", "old");
    write_file(src_dir / "collide.txt", "new");

    const std::thread::id main_thread_id = std::this_thread::get_id();
    std::atomic<bool> conflict_asker_ran_on_main_thread{false};
    std::atomic<int> progress_calls{0};

    auto conflict_asker = [&](const fs::path&) -> ConflictResolution {
        // If TransferWorker's blocking-queued marshaling is correct, this
        // runs on the thread that owns `gui_thread_object` below (main),
        // never on the worker thread that called it.
        conflict_asker_ran_on_main_thread = (std::this_thread::get_id() == main_thread_id);
        return ConflictResolution::Overwrite;
    };

    // Stands in for MainWindow: a QObject living on the main thread, used
    // only as the blocking-invocation target.
    QObject gui_thread_stand_in;

    auto* worker = new TransferWorker({src_dir / "payload.txt", src_dir / "collide.txt"}, dst_dir,
                                       BulkTransferJob::Mode::Copy, &gui_thread_stand_in, conflict_asker);
    auto* thread = new QThread();
    worker->moveToThread(thread);

    bool success = false;
    QString error_message;

    QEventLoop loop;
    QObject::connect(thread, &QThread::started, worker, &TransferWorker::run);
    QObject::connect(worker, &TransferWorker::progressChanged, &gui_thread_stand_in,
                      [&](qint64, qint64, QString) { ++progress_calls; });
    QObject::connect(worker, &TransferWorker::finished, &gui_thread_stand_in,
                      [&](bool ok, QString msg) {
                          success = ok;
                          error_message = msg;
                          thread->quit();
                      });
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);

    thread->start();
    loop.exec();

    thread->wait();
    delete thread;
    delete worker;

    assert(success && "real copy+conflict-overwrite transfer should succeed");
    assert(fs::exists(dst_dir / "payload.txt"));
    assert(fs::exists(dst_dir / "collide.txt"));
    {
        std::ifstream in(dst_dir / "collide.txt");
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(content == "new" && "conflict resolution should have overwritten with the source's content");
    }
    assert(progress_calls.load() > 0 && "progressChanged should have fired at least once");
    assert(conflict_asker_ran_on_main_thread.load() &&
           "ConflictAsker must be marshaled back onto the GUI-thread object, not run on the worker thread");

    fs::remove_all(scratch);
    std::printf(
        "[ok] transfer_worker: async copy ran on a worker thread, reported %d progress update(s), "
        "and marshaled its conflict question back onto the main thread correctly\n",
        progress_calls.load());
    return 0;
}
