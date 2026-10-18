// SPDX-License-Identifier: GPL-3.0-or-later
// network_browsing.hpp
//
// Clean-room reimplementation of the BEHAVIOR observed for remote-location
// browsing in XandrosFileManager. Evidence (from embedded strings, no
// decompiled call sites for this pass -- see analysis/notes.md for the
// "flagged as ambiguous" caveat on WebDAV specifically):
//   - CSMBFileInfo / DEFAULTWORKGROUP / mountNetworkShare / CanDisconnectShare
//     / DisconnectFileSystem  -> SMB (Windows network) browsing normalizes
//     into the SAME EntryInfo-shaped data local files use (see
//     directory_listing.hpp), rather than a parallel/separate model.
//   - CFtpSiteItem / CFTPFileItem / CFtpSessionList / FindAndExpand ->
//     bookmarked FTP sites, lazily listed on tree expansion; actual
//     transfers delegated to a separate "FtpAgent" helper process (same
//     out-of-process pattern as CopyAgent, see file_operations.hpp).
//   - WebDAV: only a hook name (slotXFMWebDataFinished) was found inside
//     this binary; the real implementation lives in the sibling
//     libxfmwebdata.so, NOT analyzed this pass. The abstraction below
//     models WebDAV the same way as the other two remote kinds for
//     interface consistency, but that specific backend's real wire
//     behavior is unconfirmed -- treat WebDavSite below as a plausible
//     placeholder shape, not a verified reconstruction.
//
// Modern target: C++20. A real implementation would plug in libsmbclient
// (or GIO's smb:// backend) for SMB, libcurl for FTP/WebDAV, instead of a
// bespoke protocol stack; this file models only the listing/connection
// *interface* the original GUI seems to have relied on (async-capable,
// capability-queryable, same shape as local directories), not protocol
// wire details.

#pragma once

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "directory_listing.hpp"

namespace xfm {

// Listing a remote location can fail for reasons local disks rarely hit:
// auth required, host unreachable, share not found. The legacy error__
// (QString) signal on CViewManager suggests these were surfaced as a
// single user-facing error string rather than typed exceptions, which we
// preserve here as an explicit result type instead of throwing.
struct RemoteListingOutcome {
    std::optional<std::vector<EntryInfo>> entries;
    std::string error_message; // non-empty only when entries is nullopt
};

using RemoteListingCallback = std::function<void(RemoteListingOutcome)>;

// Credentials prompt hook: SMB/FTP/WebDAV all potentially need auth. The
// original presumably reused one login dialog across all three remote
// kinds, which we model as one callback shared by every backend below
// rather than one per protocol.
struct Credentials {
    std::string username;
    std::string password;
};
using CredentialPrompt = std::function<std::optional<Credentials>(const std::string& realm_or_host)>;

// Base for the three remote "site" kinds. Listing is asynchronous
// (std::future-based) to mirror the responsiveness the external-agent /
// KIO-job pattern elsewhere in the app clearly prioritized -- a modal,
// blocking listing call would stall the whole UI on a slow or dead host.
class RemoteSite : public IBrowsableLocation {
public:
    RemoteSite(std::string host, std::string remote_path, CredentialPrompt prompt)
        : host_(std::move(host)), remote_path_(std::move(remote_path)), prompt_(std::move(prompt)) {}

    std::string display_path() const override { return host_ + ":" + remote_path_; }

    // Synchronous facade required by IBrowsableLocation; real callers
    // should prefer list_children_async() below and keep the UI responsive.
    std::optional<std::vector<EntryInfo>> list_children() const override {
        return list_children_async().get().entries;
    }

    std::future<RemoteListingOutcome> list_children_async() const {
        return std::async(std::launch::async, [this]() { return do_list(); });
    }

    ItemCapabilities capabilities() const override {
        // Remote shares/sites: copy/cut generally allowed if currently
        // connected with write access; local-only operations like
        // "create subfolder" require a successful prior connection check.
        // Kept conservative (read-mostly) since write capability truly
        // depends on a live connection this static model can't assume.
        return ItemCapabilities{
            .can_copy = true,
            .can_cut = false,
            .can_delete = false,
            .can_rename = false,
            .can_create_subfolder = false,
            .can_move_to_trash = false,
        };
    }

protected:
    virtual RemoteListingOutcome do_list() const = 0;

    std::optional<Credentials> maybe_prompt_for_credentials() const {
        return prompt_ ? prompt_(host_) : std::nullopt;
    }

    std::string host_;
    std::string remote_path_;
    CredentialPrompt prompt_;
};

// SMB share browsing. mountNetworkShare()/DisconnectFileSystem() in the
// original suggest an explicit connect/mount step distinct from listing;
// we keep that as connect()/disconnect() rather than folding it into
// do_list() implicitly.
class SmbShare final : public RemoteSite {
public:
    SmbShare(std::string host, std::string share, std::string workgroup, CredentialPrompt prompt)
        : RemoteSite(std::move(host), "/" + share, std::move(prompt)),
          workgroup_(workgroup.empty() ? "WORKGROUP" : std::move(workgroup)) {}

    bool connect() {
        if (connected_) return true;
        auto creds = maybe_prompt_for_credentials();
        // Real implementation: hand host_/share/workgroup_/creds to
        // libsmbclient's smbc_wrapper_connect-equivalent call.
        connected_ = creds.has_value() || allow_anonymous_;
        return connected_;
    }

    void disconnect() { connected_ = false; } // CanDisconnectShare / DisconnectFileSystem

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<SmbShare>(host_, remote_path_.substr(1) + "/" + name, workgroup_, prompt_);
    }

private:
    RemoteListingOutcome do_list() const override {
        if (!connected_) {
            return {std::nullopt, "Not connected to " + host_};
        }
        // Real implementation: smbc_opendir/readdir over remote_path_,
        // normalized into EntryInfo exactly like LocalDirectory does.
        return {std::vector<EntryInfo>{}, {}};
    }

    std::string workgroup_;
    bool connected_ = false;
    bool allow_anonymous_ = true;
};

// FTP site browsing (listing only; transfers are intentionally NOT
// implemented here -- the legacy app delegated those to a separate
// "FtpAgent" process, matching the CopyAgent delegation pattern in
// file_operations.hpp; a modern port should keep that separation, e.g.
// a worker thread or process using libcurl's easy FTP upload/download).
class FtpSite final : public RemoteSite {
public:
    FtpSite(std::string host, std::string remote_path, CredentialPrompt prompt)
        : RemoteSite(std::move(host), std::move(remote_path), std::move(prompt)) {}

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<FtpSite>(host_, remote_path_ + "/" + name, prompt_);
    }

private:
    RemoteListingOutcome do_list() const override {
        auto creds = maybe_prompt_for_credentials();
        if (!creds) {
            return {std::nullopt, "Login required for " + host_};
        }
        // Real implementation: libcurl CURLOPT_URL "ftp://host/path/" with
        // CURLOPT_DIRLISTONLY + a write callback parsed into EntryInfo.
        return {std::vector<EntryInfo>{}, {}};
    }
};

// Placeholder shape only -- see the file header caveat. Not a verified
// reconstruction of libxfmwebdata's actual behavior.
class WebDavSite final : public RemoteSite {
public:
    WebDavSite(std::string host, std::string remote_path, CredentialPrompt prompt)
        : RemoteSite(std::move(host), std::move(remote_path), std::move(prompt)) {}

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<WebDavSite>(host_, remote_path_ + "/" + name, prompt_);
    }

private:
    RemoteListingOutcome do_list() const override {
        // Real implementation: HTTP PROPFIND (depth 1) over WebDAV,
        // parsing the multistatus XML response into EntryInfo records.
        return {std::nullopt, "WebDAV backend not verified against libxfmwebdata.so (out of scope)"};
    }
};

} // namespace xfm
