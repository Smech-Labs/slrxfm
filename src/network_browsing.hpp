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
//     libxfmwebdata.so, NOT analyzed in this pass. The abstraction below
//     models WebDAV the same way as the other two remote kinds for
//     interface consistency, but that specific backend's real wire
//     behavior remains unconfirmed against libxfmwebdata.so.
//
// Backend implementations: as of this revision, SmbShare, FtpSite, and
// WebDavSite are REAL, working backends (libsmbclient for SMB, libcurl for
// FTP/WebDAV -- see network_browsing.cpp), not stubs. This is ordinary new
// engineering against well-documented public APIs, NOT reverse-engineering
// -- it carries no clean-room concerns of its own. The WebDAV backend
// specifically is a functionally real, generic RFC 4918 PROPFIND client;
// it does NOT claim or imply behavioral parity with the original Xandros
// libxfmwebdata.so, which remains unanalyzed and out of scope. See
// docs/CLEAN_ROOM.md for that standing ambiguity.

#pragma once

#include <chrono>
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
// do_list() implicitly. Real listing is implemented in network_browsing.cpp
// via libsmbclient (smbc_opendir/smbc_readdir/smbc_stat), gated behind
// SLRXFM_HAVE_LIBSMBCLIENT (set by CMake when libsmbclient was found).
class SmbShare final : public RemoteSite {
public:
    SmbShare(std::string host, std::string share, std::string workgroup, CredentialPrompt prompt)
        : RemoteSite(std::move(host), "/" + share, std::move(prompt)),
          workgroup_(workgroup.empty() ? "WORKGROUP" : std::move(workgroup)) {}

    bool connect() {
        if (connected_) return true;
        creds_ = maybe_prompt_for_credentials();
        connected_ = creds_.has_value() || allow_anonymous_;
        return connected_;
    }

    void disconnect() { // CanDisconnectShare / DisconnectFileSystem
        connected_ = false;
        creds_.reset();
    }

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        auto child = std::make_unique<SmbShare>(host_, remote_path_.substr(1) + "/" + name, workgroup_, prompt_);
        child->connected_ = connected_;
        child->creds_ = creds_;
        return child;
    }

private:
    RemoteListingOutcome do_list() const override;

    std::string workgroup_;
    std::optional<Credentials> creds_;
    bool connected_ = false;
    bool allow_anonymous_ = true;
};

// FTP site browsing (listing only; transfers are intentionally NOT
// implemented here -- the legacy app delegated those to a separate
// "FtpAgent" process, matching the CopyAgent delegation pattern in
// file_operations.hpp; a modern port should keep that separation). Real
// listing is implemented in network_browsing.cpp via libcurl, using the
// RFC 3659 MLSD command for a machine-readable (not scraped) directory
// listing, gated behind SLRXFM_HAVE_LIBCURL.
class FtpSite final : public RemoteSite {
public:
    FtpSite(std::string host, std::string remote_path, CredentialPrompt prompt)
        : RemoteSite(std::move(host), std::move(remote_path), std::move(prompt)) {}

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<FtpSite>(host_, remote_path_ + "/" + name, prompt_);
    }

private:
    RemoteListingOutcome do_list() const override;
};

// Generic, functionally real RFC 4918 WebDAV client (PROPFIND, Depth: 1),
// implemented via libcurl in network_browsing.cpp, gated behind
// SLRXFM_HAVE_LIBCURL. IMPORTANT: this is ordinary new WebDAV-protocol
// engineering, not a reconstruction of libxfmwebdata.so -- see the file
// header above and docs/CLEAN_ROOM.md. Do not treat successful listings
// here as evidence of parity with the original Xandros backend.
class WebDavSite final : public RemoteSite {
public:
    WebDavSite(std::string host, std::string remote_path, CredentialPrompt prompt)
        : RemoteSite(std::move(host), std::move(remote_path), std::move(prompt)) {}

    std::unique_ptr<IBrowsableLocation> open_child(const std::string& name) const override {
        return std::make_unique<WebDavSite>(host_, remote_path_ + "/" + name, prompt_);
    }

private:
    RemoteListingOutcome do_list() const override;
};

} // namespace xfm
