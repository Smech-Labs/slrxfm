// SPDX-License-Identifier: GPL-3.0-or-later
// app_main.cpp — SLRXFM GUI entry point. New UI engineering; see
// main_window.hpp for the clean-room note on this layer.

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include <filesystem>

#include "main_window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("slrxfm"));
    QApplication::setApplicationDisplayName(QStringLiteral("SLRXFM"));
    QApplication::setOrganizationName(QStringLiteral("Smech Labs"));

    std::filesystem::path start_dir;
    if (argc > 1) {
        start_dir = argv[1];
    } else {
        QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        start_dir = home.toStdString();
    }

    xfm::gui::MainWindow window(start_dir);
    window.show();

    return QApplication::exec();
}
