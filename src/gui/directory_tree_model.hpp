// SPDX-License-Identifier: GPL-3.0-or-later
// directory_tree_model.hpp
//
// New UI engineering, not reverse-engineered: a Qt6 QAbstractItemModel
// adapter exposing a lazily-loaded local-filesystem folder tree, built on
// top of xfm::LocalDirectory (src/directory_listing.hpp). It does not
// reimplement any filesystem logic itself -- every listing call goes
// through xfm::LocalDirectory::list_children(), filtered down to
// directory entries, exactly like the file-listing panel does for files.
//
// Layout/behavior (tree on the left, detail list on the right, address
// bar above) is an ordinary, modern file-manager convention and was not
// copied from any Xandros screenshot or resource; no pixel/asset content
// from the original application is referenced here. See docs/CLEAN_ROOM.md.

#pragma once

#include <QAbstractItemModel>
#include <QFileIconProvider>
#include <QModelIndex>
#include <QVariant>

#include <filesystem>
#include <memory>
#include <vector>

#include "directory_listing.hpp"

namespace xfm::gui {

namespace fs = std::filesystem;

class DirectoryTreeModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit DirectoryTreeModel(fs::path root, QObject* parent = nullptr)
        : QAbstractItemModel(parent), root_(std::make_unique<Node>(nullptr, std::move(root))) {}

    QModelIndex index(int row, int column, const QModelIndex& parent) const override {
        if (column != 0) return {};
        Node* parent_node = node_for(parent);
        ensure_children_loaded(parent_node);
        if (row < 0 || row >= static_cast<int>(parent_node->children.size())) return {};
        return createIndex(row, 0, parent_node->children[static_cast<std::size_t>(row)].get());
    }

    QModelIndex parent(const QModelIndex& child) const override {
        Node* child_node = node_for(child);
        if (child_node == root_.get() || child_node->parent == nullptr) return {};
        Node* grandparent = child_node->parent->parent;
        int row = row_of(child_node->parent, grandparent);
        return createIndex(row, 0, child_node->parent);
    }

    int rowCount(const QModelIndex& parent) const override {
        Node* parent_node = node_for(parent);
        ensure_children_loaded(parent_node);
        return static_cast<int>(parent_node->children.size());
    }

    int columnCount(const QModelIndex&) const override { return 1; }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid()) return {};
        Node* n = node_for(index);
        if (role == Qt::DisplayRole) {
            auto name = n->path.filename().string();
            return QString::fromStdString(name.empty() ? n->path.string() : name);
        }
        if (role == Qt::DecorationRole) {
            return icon_provider_.icon(QFileIconProvider::Folder);
        }
        if (role == PathRole) {
            return QString::fromStdString(n->path.string());
        }
        return {};
    }

    // Lets the detail panel/address bar ask "what path does this index
    // represent" without the view layer needing to know about fs::path.
    static constexpr int PathRole = Qt::UserRole + 1;

    fs::path path_for(const QModelIndex& index) const {
        if (!index.isValid()) return root_->path;
        return node_for(index)->path;
    }

    // Re-reads children of a node the next time it's expanded (e.g. after
    // an external change like create-folder/rename/delete).
    void invalidate(const QModelIndex& index) {
        Node* n = index.isValid() ? node_for(index) : root_.get();
        beginResetModel();
        n->children.clear();
        n->loaded = false;
        endResetModel();
    }

private:
    struct Node {
        Node* parent;
        fs::path path;
        std::vector<std::unique_ptr<Node>> children;
        bool loaded = false;

        Node(Node* p, fs::path pth) : parent(p), path(std::move(pth)) {}
    };

    Node* node_for(const QModelIndex& index) const {
        if (!index.isValid()) return root_.get();
        return static_cast<Node*>(index.internalPointer());
    }

    static int row_of(Node* node, Node* parent) {
        if (parent == nullptr) return 0;
        for (std::size_t i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == node) return static_cast<int>(i);
        }
        return 0;
    }

    void ensure_children_loaded(Node* n) const {
        if (n->loaded) return;
        n->loaded = true;

        LocalDirectory dir(n->path);
        auto entries = dir.list_children();
        if (!entries) return;

        for (const auto& entry : *entries) {
            if (!entry.is_directory || entry.is_hidden) continue;
            n->children.push_back(std::make_unique<Node>(n, n->path / entry.name));
        }
    }

    std::unique_ptr<Node> root_;
    mutable QFileIconProvider icon_provider_;
};

} // namespace xfm::gui
