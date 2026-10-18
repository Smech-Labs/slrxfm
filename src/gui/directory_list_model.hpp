// SPDX-License-Identifier: GPL-3.0-or-later
// directory_list_model.hpp
//
// New UI engineering: a Qt6 QAbstractTableModel adapter exposing the
// contents of one xfm::IBrowsableLocation (src/directory_listing.hpp) as
// a Name/Size/Modified table, the way the detail/icon pane of a classic
// dual-pane file manager does. All listing logic is delegated to
// IBrowsableLocation::list_children() -- this file only shapes that data
// for QAbstractItemView, it does not reimplement filesystem access.

#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QFileIconProvider>
#include <QVariant>

#include <memory>
#include <vector>

#include "directory_listing.hpp"

namespace xfm::gui {

class DirectoryListModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { NameColumn = 0, SizeColumn = 1, ModifiedColumn = 2, ColumnCount = 3 };
    static constexpr int EntryNameRole = Qt::UserRole + 1;
    static constexpr int IsDirectoryRole = Qt::UserRole + 2;

    explicit DirectoryListModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    // Takes ownership of a freshly-constructed location and (re)loads its
    // children immediately; returns false (and leaves the model empty) on
    // a listing failure, mirroring IBrowsableLocation's fail-with-a-reason
    // contract rather than throwing.
    bool set_location(std::unique_ptr<IBrowsableLocation> location) {
        beginResetModel();
        location_ = std::move(location);
        entries_.clear();
        bool ok = false;
        if (location_) {
            if (auto listed = location_->list_children()) {
                entries_ = std::move(*listed);
                ok = true;
            }
        }
        endResetModel();
        return ok;
    }

    void refresh() {
        if (!location_) return;
        beginResetModel();
        if (auto listed = location_->list_children()) {
            entries_ = std::move(*listed);
        } else {
            entries_.clear();
        }
        endResetModel();
    }

    const IBrowsableLocation* location() const { return location_.get(); }

    int rowCount(const QModelIndex& parent) const override {
        return parent.isValid() ? 0 : static_cast<int>(entries_.size());
    }

    int columnCount(const QModelIndex& parent) const override {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
            case NameColumn: return QStringLiteral("Name");
            case SizeColumn: return QStringLiteral("Size");
            case ModifiedColumn: return QStringLiteral("Modified");
            default: return {};
        }
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size())) {
            return {};
        }
        const EntryInfo& e = entries_[static_cast<std::size_t>(index.row())];

        if (role == EntryNameRole) return QString::fromStdString(e.name);
        if (role == IsDirectoryRole) return e.is_directory;

        if (role == Qt::DecorationRole && index.column() == NameColumn) {
            return icon_provider_.icon(e.is_directory ? QFileIconProvider::Folder : QFileIconProvider::File);
        }

        if (role != Qt::DisplayRole) return {};
        switch (index.column()) {
            case NameColumn:
                return QString::fromStdString(e.name);
            case SizeColumn:
                return e.is_directory ? QString("—") : QString::number(e.size_bytes);
            case ModifiedColumn: {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(e.modified_time.time_since_epoch()).count();
                return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(secs)).toString(Qt::ISODate);
            }
            default:
                return {};
        }
    }

    const EntryInfo* entry_at(int row) const {
        if (row < 0 || row >= static_cast<int>(entries_.size())) return nullptr;
        return &entries_[static_cast<std::size_t>(row)];
    }

private:
    std::unique_ptr<IBrowsableLocation> location_;
    std::vector<EntryInfo> entries_;
    mutable QFileIconProvider icon_provider_;
};

} // namespace xfm::gui
