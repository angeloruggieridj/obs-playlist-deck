// SPDX-License-Identifier: MIT
#include "MediaScanner.hpp"
#include "MediaProbe.hpp"

#include <QElapsedTimer>
#include <QFileInfo>

namespace {
// Results are flushed either when this many have piled up or when this long has
// passed, whichever comes first: enough to keep a large drop from rebuilding the
// list once per file, short enough that durations still appear to fill in live.
constexpr int kBatchSize = 24;
constexpr qint64 kBatchIntervalMs = 250;
} // namespace

void ScanWorker::scan(const QStringList& paths, quint64 generation, bool probeDurations) {
    QList<ScanResult> batch;
    QElapsedTimer since;
    since.start();

    auto flush = [&]() {
        if (batch.isEmpty()) return;
        emit batchReady(batch, generation);
        batch.clear();
        since.restart();
    };

    for (const QString& path : paths) {
        // Checked per file, not per batch: abandoning a stale scan promptly is
        // the whole point of the generation counter.
        if (stale(generation)) return;
        ScanResult r;
        r.path = path;
        r.exists = QFileInfo::exists(path);
        // Probing a file that is not there wakes FFmpeg up for nothing.
        if (probeDurations && r.exists) r.durationMs = pld::probeDurationMs(path.toStdString());
        batch.append(r);
        if (batch.size() >= kBatchSize || since.elapsed() >= kBatchIntervalMs) flush();
    }
    flush();
    if (!stale(generation)) emit finished(generation);
}

MediaScanner::MediaScanner(QObject* parent) : QObject(parent) {
    thread_ = new QThread();
    thread_->setObjectName("pld-scanner");
    worker_ = new ScanWorker(&generation_);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &MediaScanner::requestScan, worker_, &ScanWorker::scan, Qt::QueuedConnection);
    connect(worker_, &ScanWorker::batchReady, this,
            [this](const QList<ScanResult>& results, quint64 generation) {
                // A batch that finished just as the playlist changed is dropped
                // here too: the worker's own check races with this one.
                if (generation != generation_.load(std::memory_order_relaxed)) return;
                emit resultsReady(results);
            });
    connect(worker_, &ScanWorker::finished, this, [this](quint64 generation) {
        if (generation != generation_.load(std::memory_order_relaxed)) return;
        emit batchFinished();
    });
    thread_->start();
}

MediaScanner::~MediaScanner() {
    shutdown();
    delete thread_;
    thread_ = nullptr;
}

void MediaScanner::submit(const QStringList& paths, bool probeDurations, bool replacesPlaylist) {
    if (stopped_ || paths.isEmpty()) return;
    if (replacesPlaylist) generation_.fetch_add(1, std::memory_order_relaxed);
    // Batches queued under the current generation run one after another on the
    // worker thread; only a replacement invalidates what is already in flight.
    emit requestScan(paths, generation_.load(std::memory_order_relaxed), probeDurations);
}

void MediaScanner::cancelPending() { generation_.fetch_add(1, std::memory_order_relaxed); }

void MediaScanner::shutdown() {
    if (stopped_) return;
    stopped_ = true;
    cancelPending(); // makes the running batch return at its next file
    if (thread_ && thread_->isRunning()) {
        thread_->quit();
        // A single FFmpeg probe of an unresponsive network file can take a
        // while; wait long enough to collect the thread in practice, and give
        // up rather than hanging OBS's shutdown if it does not come back.
        if (!thread_->wait(5000)) {
            thread_->terminate();
            thread_->wait(1000);
        }
    }
}
