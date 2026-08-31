// SPDX-License-Identifier: MIT
#pragma once
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>

// One file's answer to the two questions the dock asks the filesystem: is it
// still there, and how long is it?
struct ScanResult {
    QString path;
    long long durationMs = -1; // -1: unknown (not probed, or unreadable)
    bool exists = false;
};

// Results cross a thread boundary through a queued connection.
Q_DECLARE_METATYPE(ScanResult)

// Runs on the scanner thread. Do not touch it from the UI thread except through
// queued signals.
class ScanWorker : public QObject {
    Q_OBJECT
public:
    // `cancel` is shared with the owner: the generation counter is bumped when
    // the playlist is replaced, and a batch whose generation is stale is
    // abandoned mid-flight rather than delivering answers about a playlist that
    // no longer exists.
    explicit ScanWorker(std::atomic<quint64>* generation) : generation_(generation) {}

public slots:
    void scan(const QStringList& paths, quint64 generation, bool probeDurations);

signals:
    // Results arrive in batches rather than one signal per file: a 300-file drop
    // used to rebuild the whole list 300 times.
    void batchReady(const QList<ScanResult>& results, quint64 generation);
    void finished(quint64 generation);

private:
    bool stale(quint64 generation) const {
        return generation_ && generation_->load(std::memory_order_relaxed) != generation;
    }
    std::atomic<quint64>* generation_ = nullptr;
};

// Owns the scanner thread and the worker living on it.
//
// This replaces `std::thread(...).detach()`. A detached thread cannot be
// stopped, cannot be waited for, and outlives the objects it posts to: at OBS
// shutdown it could still be calling into a Qt application that was being torn
// down — an intermittent crash on exit that is close to impossible to
// reproduce. The thread is owned now, and shutdown waits for it.
class MediaScanner : public QObject {
    Q_OBJECT
public:
    explicit MediaScanner(QObject* parent = nullptr);
    ~MediaScanner() override;

    // Queues a batch. `replacesPlaylist` says the list these paths belong to has
    // taken the place of the previous one: that cancels whatever was still
    // running, so answers about a playlist the user has already replaced are
    // never applied. Appending does not cancel — the files added a moment ago
    // still deserve their durations.
    void submit(const QStringList& paths, bool probeDurations, bool replacesPlaylist = false);

    // Invalidates every in-flight batch without queueing a new one.
    void cancelPending();

    // Stops the thread and waits for it. Safe to call more than once; called
    // from the dock's shutdown before OBS tears anything down.
    void shutdown();

    quint64 currentGeneration() const { return generation_.load(std::memory_order_relaxed); }

signals:
    void resultsReady(const QList<ScanResult>& results);
    void batchFinished();
    // Internal: hands the request to the worker thread.
    void requestScan(const QStringList& paths, quint64 generation, bool probeDurations);

private:
    QThread* thread_ = nullptr;
    ScanWorker* worker_ = nullptr;
    std::atomic<quint64> generation_{1};
    bool stopped_ = false;
};
