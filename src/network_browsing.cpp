// SPDX-License-Identifier: GPL-3.0-or-later
// network_browsing.cpp
//
// Real backend implementations for network_browsing.hpp's three remote
// "site" kinds. This is ordinary new engineering against well-documented
// public libraries (libsmbclient, libcurl) -- it is NOT reverse-engineered
// and carries no clean-room concerns; see the note at the top of
// network_browsing.hpp and docs/CLEAN_ROOM.md for why the WebDAV backend
// specifically still makes no behavioral-parity claim against the
// original (unanalyzed) libxfmwebdata.so.
//
// Each backend is compiled in only when CMake found the corresponding
// library (SLRXFM_HAVE_LIBSMBCLIENT / SLRXFM_HAVE_LIBCURL); otherwise
// do_list() reports that plainly as a listing error rather than silently
// returning an empty/successful listing.

#include "network_browsing.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>

#if defined(SLRXFM_HAVE_LIBSMBCLIENT)
#include <libsmbclient.h>
#include <sys/stat.h>
#endif

#if defined(SLRXFM_HAVE_LIBCURL)
#include <curl/curl.h>
#endif

namespace xfm {

// ---------------------------------------------------------------------
// SMB (libsmbclient)
// ---------------------------------------------------------------------
#if defined(SLRXFM_HAVE_LIBSMBCLIENT)
namespace {

// libsmbclient's simple/legacy API (smbc_init + the global implicit
// context) takes a plain C function pointer for auth, not a std::function,
// so credentials for the in-flight call are stashed here for the duration
// of do_list(). This is the conventional way to use the simple API with
// per-call credentials; it does mean concurrent SMB listings on different
// threads share one implicit context, a real limitation of the simple API
// (a context-per-SMBCCTX design via smbc_new_context() would remove it,
// at the cost of noticeably more setup code) -- acceptable for this
// browse-one-share-at-a-time use case, called out here rather than
// papered over.
thread_local const Credentials* g_pending_smb_creds = nullptr;
thread_local std::string g_pending_smb_workgroup;

void smb_auth_callback(const char* /*srv*/, const char* /*shr*/, char* wg, int wglen, char* un, int unlen,
                        char* pw, int pwlen) {
    if (!g_pending_smb_workgroup.empty()) {
        std::snprintf(wg, static_cast<std::size_t>(wglen), "%s", g_pending_smb_workgroup.c_str());
    }
    if (g_pending_smb_creds) {
        std::snprintf(un, static_cast<std::size_t>(unlen), "%s", g_pending_smb_creds->username.c_str());
        std::snprintf(pw, static_cast<std::size_t>(pwlen), "%s", g_pending_smb_creds->password.c_str());
    }
}

} // namespace
#endif

RemoteListingOutcome SmbShare::do_list() const {
    if (!connected_) {
        return {std::nullopt, "Not connected to " + host_};
    }
#if defined(SLRXFM_HAVE_LIBSMBCLIENT)
    g_pending_smb_creds = creds_.has_value() ? &*creds_ : nullptr;
    g_pending_smb_workgroup = workgroup_;

    if (smbc_init(&smb_auth_callback, 0) != 0) {
        return {std::nullopt, "smbc_init failed: " + std::string(std::strerror(errno))};
    }

    const std::string url = "smb://" + host_ + remote_path_;
    int dh = smbc_opendir(url.c_str());
    if (dh < 0) {
        return {std::nullopt, "Could not open " + url + ": " + std::strerror(errno)};
    }

    std::vector<EntryInfo> entries;
    struct smbc_dirent* dirent = nullptr;
    while ((dirent = smbc_readdir(static_cast<unsigned int>(dh))) != nullptr) {
        std::string name(dirent->name, dirent->namelen);
        if (name == "." || name == "..") continue;
        // Only normal files/directories belong in a browsable listing --
        // workgroup/server/share entries only appear at shallower URLs
        // than the ones this class constructs (host+share), so in
        // practice this filter is mostly a defensive no-op here.
        if (dirent->smbc_type != SMBC_DIR && dirent->smbc_type != SMBC_FILE) continue;

        EntryInfo info;
        info.name = name;
        info.is_directory = (dirent->smbc_type == SMBC_DIR);
        info.is_hidden = !name.empty() && name.front() == '.';

        struct stat st{};
        if (smbc_stat((url + "/" + name).c_str(), &st) == 0) {
            info.size_bytes = info.is_directory ? 0 : static_cast<std::uintmax_t>(st.st_size);
            info.modified_time = std::chrono::system_clock::from_time_t(st.st_mtime);
            info.writable = (st.st_mode & S_IWUSR) != 0;
        }
        entries.push_back(std::move(info));
    }
    smbc_closedir(static_cast<unsigned int>(dh));
    g_pending_smb_creds = nullptr;
    return {std::move(entries), {}};
#else
    return {std::nullopt, "This build was compiled without libsmbclient support"};
#endif
}

// ---------------------------------------------------------------------
// FTP (libcurl, RFC 3659 MLSD)
// ---------------------------------------------------------------------
#if defined(SLRXFM_HAVE_LIBCURL)
namespace {

std::size_t curl_write_to_string(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Parses one line of a trimmed MLSD fact list ("type=dir;modify=...;"),
// extracting just the facts this module cares about.
EntryInfo parse_mlsd_line(const std::string& facts, const std::string& name) {
    EntryInfo info;
    info.name = name;
    info.is_hidden = !name.empty() && name.front() == '.';

    std::istringstream fact_stream(facts);
    std::string fact;
    while (std::getline(fact_stream, fact, ';')) {
        auto eq = fact.find('=');
        if (eq == std::string::npos) continue;
        std::string key = fact.substr(0, eq);
        std::string value = fact.substr(eq + 1);
        for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (key == "type") {
            info.is_directory = (value == "dir" || value == "cdir" || value == "pdir");
        } else if (key == "size") {
            info.size_bytes = std::strtoull(value.c_str(), nullptr, 10);
        } else if (key == "modify" && value.size() >= 14) {
            // YYYYMMDDHHMMSS[.sss], always UTC per RFC 3659.
            std::tm tm{};
            tm.tm_year = std::stoi(value.substr(0, 4)) - 1900;
            tm.tm_mon = std::stoi(value.substr(4, 2)) - 1;
            tm.tm_mday = std::stoi(value.substr(6, 2));
            tm.tm_hour = std::stoi(value.substr(8, 2));
            tm.tm_min = std::stoi(value.substr(10, 2));
            tm.tm_sec = std::stoi(value.substr(12, 2));
            info.modified_time = std::chrono::system_clock::from_time_t(timegm(&tm));
        }
    }
    return info;
}

} // namespace
#endif

RemoteListingOutcome FtpSite::do_list() const {
    auto creds = maybe_prompt_for_credentials();
    if (!creds) {
        return {std::nullopt, "Login required for " + host_};
    }
#if defined(SLRXFM_HAVE_LIBCURL)
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {std::nullopt, "Failed to initialize libcurl"};
    }

    std::string url = "ftp://" + host_ + remote_path_;
    if (url.empty() || url.back() != '/') url += '/';

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, creds->username.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, creds->password.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "MLSD");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        std::string error = "FTP MLSD listing of " + url + " failed: " + curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        return {std::nullopt, error};
    }
    curl_easy_cleanup(curl);

    std::vector<EntryInfo> entries;
    std::istringstream lines(response);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string facts = line.substr(0, space);
        std::string name = line.substr(space + 1);
        if (name == "." || name == "..") continue;

        EntryInfo info = parse_mlsd_line(facts, name);
        if (facts.find("type=cdir") != std::string::npos || facts.find("type=pdir") != std::string::npos) {
            continue; // "." / ".." equivalents under MLSD's own type facts
        }
        entries.push_back(std::move(info));
    }
    return {std::move(entries), {}};
#else
    return {std::nullopt, "This build was compiled without libcurl support"};
#endif
}

// ---------------------------------------------------------------------
// WebDAV (libcurl, RFC 4918 PROPFIND) -- generic client, NOT a
// libxfmwebdata.so reconstruction. See the file header above.
// ---------------------------------------------------------------------
#if defined(SLRXFM_HAVE_LIBCURL)
namespace {

// Strips an XML namespace prefix ("D:response" -> "response") so this
// tolerant scanner works regardless of which prefix a given WebDAV server
// happens to use (seen in the wild: "D:", "d:", "lp1:", no prefix at all).
std::string local_tag_name(std::string tag) {
    auto colon = tag.find(':');
    return colon == std::string::npos ? tag : tag.substr(colon + 1);
}

// Finds the text content of the first <...tag...>...</...tag...> element
// (any namespace prefix) within [start, end) of xml. Minimal by design --
// this is a tolerant scanner for well-formed multistatus bodies as
// real-world DAV servers (Apache mod_dav, nginx-dav-ext, Nextcloud/ownCloud)
// emit them, not a general-purpose XML parser.
std::optional<std::string> find_element_text(const std::string& xml, const std::string& tag, std::size_t start,
                                              std::size_t end) {
    std::size_t pos = start;
    while (pos < end) {
        std::size_t lt = xml.find('<', pos);
        if (lt == std::string::npos || lt >= end) return std::nullopt;
        std::size_t gt = xml.find('>', lt);
        if (gt == std::string::npos || gt >= end) return std::nullopt;
        std::string open_tag = xml.substr(lt + 1, gt - lt - 1);
        bool self_closing = !open_tag.empty() && open_tag.back() == '/';
        if (self_closing) open_tag.pop_back();
        std::string name = local_tag_name(open_tag.substr(0, open_tag.find(' ') == std::string::npos
                                                                   ? open_tag.size()
                                                                   : open_tag.find(' ')));
        if (name == tag) {
            if (self_closing) return std::string{};
            std::size_t close = xml.find("</", gt + 1);
            if (close == std::string::npos) return std::nullopt;
            return xml.substr(gt + 1, close - gt - 1);
        }
        pos = gt + 1;
    }
    return std::nullopt;
}

// WebDAV servers vary on whether <D:href> is a full absolute URL
// ("http://host/path") or a path-only reference ("/path") -- both are
// RFC 4918-legal. This strips scheme+host when present so comparisons
// against our own request path work either way.
std::string href_path_only(const std::string& href) {
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        std::size_t scheme_end = href.find("://") + 3;
        std::size_t path_start = href.find('/', scheme_end);
        return path_start == std::string::npos ? std::string("/") : href.substr(path_start);
    }
    return href;
}

// Drops a trailing slash (except for the bare root) so "/a/", "/a", and
// the occasional doubled-up variant all compare equal.
std::string normalize_dav_path(std::string path) {
    if (path.empty()) return "/";
    if (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

std::string url_decode(const std::string& encoded) {
    std::string out;
    out.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            int value = std::stoi(encoded.substr(i + 1, 2), nullptr, 16);
            out.push_back(static_cast<char>(value));
            i += 2;
        } else {
            out.push_back(encoded[i]);
        }
    }
    return out;
}

} // namespace
#endif

RemoteListingOutcome WebDavSite::do_list() const {
#if defined(SLRXFM_HAVE_LIBCURL)
    auto creds = maybe_prompt_for_credentials();

    CURL* curl = curl_easy_init();
    if (!curl) {
        return {std::nullopt, "Failed to initialize libcurl"};
    }

    std::string url = "http://" + host_ + remote_path_;
    if (url.empty() || url.back() != '/') url += '/';

    static const char* body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "<D:prop><D:resourcetype/><D:getcontentlength/><D:getlastmodified/></D:prop>"
        "</D:propfind>";

    std::string response;
    curl_slist* headers = curl_slist_append(nullptr, "Depth: 1");
    headers = curl_slist_append(headers, "Content-Type: text/xml; charset=\"utf-8\"");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    if (creds) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, creds->username.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, creds->password.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        return {std::nullopt, "WebDAV PROPFIND of " + url + " failed: " + curl_easy_strerror(rc)};
    }
    if (status != 207 && status != 200) {
        return {std::nullopt, "WebDAV PROPFIND of " + url + " returned HTTP " + std::to_string(status)};
    }

    // Path of the collection itself, used below to skip the <D:response>
    // entry that describes the requested collection rather than one of
    // its children -- every WebDAV server includes that self-entry per
    // RFC 4918 Depth:1 semantics. Compared path-only since servers are
    // free to return either absolute-URL or path-only hrefs.
    std::string self_path = normalize_dav_path(remote_path_);

    std::vector<EntryInfo> entries;
    std::size_t search_pos = 0;
    while (true) {
        // Find the next "<...response>" opening tag, any namespace prefix.
        std::size_t open_gt = std::string::npos;
        std::size_t lt = response.find('<', search_pos);
        while (lt != std::string::npos) {
            std::size_t gt = response.find('>', lt);
            if (gt == std::string::npos) { lt = std::string::npos; break; }
            std::string tag_content = response.substr(lt + 1, gt - lt - 1);
            if (!tag_content.empty() && tag_content.front() != '/' && tag_content.front() != '?') {
                std::string bare = tag_content;
                if (!bare.empty() && bare.back() == '/') bare.pop_back();
                std::size_t sp = bare.find_first_of(" \t");
                std::string tagname = sp == std::string::npos ? bare : bare.substr(0, sp);
                if (local_tag_name(tagname) == "response") {
                    open_gt = gt;
                    break;
                }
            }
            lt = response.find('<', gt + 1);
        }
        if (open_gt == std::string::npos) break;

        // Find the matching "</...response>" closing tag.
        std::size_t close_start = std::string::npos;
        std::size_t close_end = std::string::npos;
        std::size_t scan = open_gt + 1;
        while (true) {
            std::size_t cs = response.find("</", scan);
            if (cs == std::string::npos) break;
            std::size_t ce = response.find('>', cs);
            if (ce == std::string::npos) break;
            if (local_tag_name(response.substr(cs + 2, ce - cs - 2)) == "response") {
                close_start = cs;
                close_end = ce;
                break;
            }
            scan = ce + 1;
        }
        if (close_start == std::string::npos) break;

        std::size_t block_start = open_gt + 1;
        std::size_t block_end = close_start;

        auto href = find_element_text(response, "href", block_start, block_end);
        auto content_length = find_element_text(response, "getcontentlength", block_start, block_end);
        auto last_modified = find_element_text(response, "getlastmodified", block_start, block_end);

        std::string block_lower = response.substr(block_start, block_end - block_start);
        for (auto& c : block_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool is_collection = block_lower.find("collection") != std::string::npos;

        if (href) {
            std::string decoded_href = normalize_dav_path(url_decode(*href));
            std::string decoded_href_path = normalize_dav_path(href_path_only(decoded_href));

            if (decoded_href_path != self_path) {
                auto last_slash = decoded_href_path.find_last_of('/');
                std::string name =
                    last_slash == std::string::npos ? decoded_href_path : decoded_href_path.substr(last_slash + 1);
                if (!name.empty()) {
                    EntryInfo info;
                    info.name = name;
                    info.is_directory = is_collection;
                    info.is_hidden = name.front() == '.';
                    if (content_length && !content_length->empty()) {
                        info.size_bytes = std::strtoull(content_length->c_str(), nullptr, 10);
                    }
                    // getlastmodified is RFC 1123 formatted; parsed loosely
                    // via strptime since it's display-only metadata here.
                    if (last_modified && !last_modified->empty()) {
                        std::tm tm{};
                        if (strptime(last_modified->c_str(), "%a, %d %b %Y %H:%M:%S", &tm) != nullptr) {
                            info.modified_time = std::chrono::system_clock::from_time_t(timegm(&tm));
                        }
                    }
                    entries.push_back(std::move(info));
                }
            }
        }

        search_pos = close_end + 1;
    }

    return {std::move(entries), {}};
#else
    (void)maybe_prompt_for_credentials();
    return {std::nullopt, "This build was compiled without libcurl support"};
#endif
}

} // namespace xfm
