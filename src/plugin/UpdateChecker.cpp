// SPDX-License-Identifier: MIT
#include "UpdateChecker.hpp"

#include <string>

#if HAVE_CURL
#include <curl/curl.h>
#endif

namespace {
const char* kLatestApi =
    "https://api.github.com/repos/angeloruggieridj/obs-playlist-deck/releases/latest";

#if HAVE_CURL
// The expected reply is a few kilobytes of JSON. Without a ceiling, a redirect
// to something else entirely would be appended to this string until memory ran
// out; returning short of what curl offered aborts the transfer.
constexpr size_t kMaxBodyBytes = 1024 * 1024;

size_t curlAppend(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t bytes = size * nmemb;
    auto* out = static_cast<std::string*>(userdata);
    if (out->size() + bytes > kMaxBodyBytes) return 0; // abort the transfer
    out->append(ptr, bytes);
    return bytes;
}
#endif
} // namespace

bool UpdateChecker::available() {
#if HAVE_CURL
    return true;
#else
    return false;
#endif
}

void UpdateWorker::fetch(bool manual) {
    std::string body;
    std::string error;
#if HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
    } else {
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
        headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
        curl_easy_setopt(curl, CURLOPT_URL, kLatestApi);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "obs-playlist-deck");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        // Hardening, all of it required for a request made from a thread inside
        // someone else's application:
        //  - NOSIGNAL: libcurl otherwise uses SIGALRM/siglongjmp for DNS
        //    timeouts, which is undefined behaviour off the main thread and has
        //    crashed multithreaded programs for as long as curl has existed.
        //  - MAXREDIRS + PROTOCOLS: FOLLOWLOCATION alone will follow a redirect
        //    anywhere, including off HTTPS. Three hops of HTTPS is all a GitHub
        //    API call legitimately needs.
        //  - CONNECTTIMEOUT: TIMEOUT alone lets a black-holed connection hold
        //    the thread for the full ten seconds.
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        // CURLOPT_PROTOCOLS was superseded by the string form in 7.85 and is
        // deprecated (a warning on newer headers); both spellings mean the same
        // restriction.
#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7, 85, 0)
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlAppend);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            error = curl_easy_strerror(rc);
        } else {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            // A rate-limited GitHub answers 403 with a JSON body that has no
            // tag_name, which would otherwise look like "no update".
            if (status != 200) error = "HTTP " + std::to_string(status);
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
#else
    error = "libcurl";
#endif
    emit done(QString::fromStdString(body), QString::fromStdString(error), manual);
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {
    thread_ = new QThread();
    thread_->setObjectName("pld-update-check");
    worker_ = new UpdateWorker();
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &UpdateChecker::requestFetch, worker_, &UpdateWorker::fetch,
            Qt::QueuedConnection);
    connect(worker_, &UpdateWorker::done, this,
            [this](const QString& body, const QString& error, bool manual) {
                busy_ = false;
                emit resultReady(body, error, manual);
            });
    thread_->start();
}

UpdateChecker::~UpdateChecker() {
    shutdown();
    delete thread_;
    thread_ = nullptr;
}

bool UpdateChecker::check(bool manual) {
    // One request at a time. Clicking "Check for updates" five times used to
    // start five threads.
    if (stopped_ || busy_) return false;
    busy_ = true;
    emit requestFetch(manual);
    return true;
}

void UpdateChecker::shutdown() {
    if (stopped_) return;
    stopped_ = true;
    if (thread_ && thread_->isRunning()) {
        thread_->quit();
        // The request itself is capped at ten seconds by CURLOPT_TIMEOUT, but
        // OBS must not wait that long to close: give it a moment, then move on.
        if (!thread_->wait(3000)) {
            thread_->terminate();
            thread_->wait(1000);
        }
    }
}
