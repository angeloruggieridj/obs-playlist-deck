// SPDX-License-Identifier: MIT
#pragma once
#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>

// Performs the HTTP request. Lives on the checker's own thread.
class UpdateWorker : public QObject {
    Q_OBJECT
public slots:
    void fetch(bool manual);

signals:
    void done(const QString& body, const QString& error, bool manual);
};

// Asks GitHub for the latest release tag, off the UI thread.
//
// Extracted from the dock (which had grown seven responsibilities) and, more
// importantly, off `std::thread(...).detach()`: a detached HTTP request has no
// owner, cannot be cancelled, and can still be posting results into a Qt
// application that is being torn down. Here the thread is owned and joined at
// shutdown, and a second request is refused while one is in flight instead of
// piling up one thread per click.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    // `manual` marks a check the user asked for: the outcome is reported in the
    // status line rather than only the log. Returns false when a check is
    // already running.
    bool check(bool manual);
    bool busy() const { return busy_; }
    void shutdown();

    // True when this build can actually reach the network (libcurl present).
    static bool available();

signals:
    // `body` is the raw JSON reply; `error` is empty on success. Delivered on
    // the thread that created the checker.
    void resultReady(const QString& body, const QString& error, bool manual);
    void requestFetch(bool manual); // internal: hands the work to the worker

private:
    QThread* thread_ = nullptr;
    UpdateWorker* worker_ = nullptr;
    bool busy_ = false;
    bool stopped_ = false;
};
