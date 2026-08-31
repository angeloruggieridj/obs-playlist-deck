// SPDX-License-Identifier: MIT
#include "PlaybackEngine.hpp"

namespace pld {

PlaybackEngine::PlaybackEngine(Playlist& playlist, IMediaTransport& transport, std::mt19937& rng)
    : playlist_(playlist), transport_(transport), rng_(rng) {}

void PlaybackEngine::setMode(EndMode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    // The bag belongs to the mode that drew it.
    shuffle_.invalidate();
}

int PlaybackEngine::drawShuffle() {
    return shuffle_.next(playlist_.size(), playlist_.currentIndex(), rng_);
}

PlaybackResult PlaybackEngine::startAt(int index) {
    if (!playlist_.setCurrent(index)) return {};
    const PlaylistItem* item = playlist_.current();
    if (!item) return {};
    if (!transport_.bound()) return {PlaybackOutcome::NoSource, index};
    awaitingStart_ = true;
    if (!transport_.playFile(item->path)) {
        awaitingStart_ = false;
        return {PlaybackOutcome::Failed, index};
    }
    return {PlaybackOutcome::Started, index};
}

PlaybackResult PlaybackEngine::stageAt(int index) {
    if (!playlist_.setCurrent(index)) return {};
    const PlaylistItem* item = playlist_.current();
    if (!item) return {};
    if (!transport_.bound()) return {PlaybackOutcome::NoSource, index};
    awaitingStart_ = true;
    if (!transport_.stageFile(item->path)) {
        awaitingStart_ = false;
        return {PlaybackOutcome::Failed, index};
    }
    return {PlaybackOutcome::Staged, index};
}

PlaybackResult PlaybackEngine::play(int index) {
    // An explicit play cancels a staged clip: the operator has just said what
    // they want on air, and it is not the one that was queued.
    pendingStage_ = false;
    pendingStageRow_ = -1;
    return startAt(index);
}

PlaybackResult PlaybackEngine::next() {
    const int candidate = (mode_ == EndMode::Shuffle) ? drawShuffle() : -1;
    const auto decision =
        decideOnNext(mode_, playlist_.size(), playlist_.currentIndex(), candidate);
    if (decision.action != EndAction::Play) return {};
    return play(decision.index);
}

PlaybackResult PlaybackEngine::prev() {
    const auto decision = decideOnPrev(mode_, playlist_.size(), playlist_.currentIndex());
    if (decision.action != EndAction::Play) return {};
    return play(decision.index);
}

PlaybackResult PlaybackEngine::mediaEnded() {
    // Regression, 1.3.1: with "Load next (paused)", one clip ending advanced the
    // playlist by two. Handing the source a new file - obs_source_update plus a
    // restart, and for a staged clip a pause on top - makes it report that the
    // *outgoing* media ended, on top of the end that got us here. That second
    // end arrived while the deck was already off air, so it was acted on
    // immediately: item 1 was staged and then item 2 replaced it before anyone
    // saw item 1.
    //
    // An end can only belong to a clip that has started. Until the source says
    // so, one is not ours; the flag is cleared either way, so a source that
    // never reports a start costs at most one missed advance rather than
    // wedging the deck.
    if (awaitingStart_) {
        awaitingStart_ = false;
        return {};
    }
    const int candidate = (mode_ == EndMode::Shuffle) ? drawShuffle() : -1;
    const auto decision =
        decideOnEnd(mode_, playlist_.size(), playlist_.currentIndex(), candidate);

    switch (decision.action) {
    case EndAction::Play:
        return startAt(decision.index);
    case EndAction::StageNext: {
        pendingStage_ = true;
        pendingStageRow_ = decision.index;
        // If the source is already off air there is nothing to wait for; the
        // clip is loaded now rather than held for an event that has passed.
        ProgramPresence presence;
        presence.bound = transport_.bound();
        presence.inProgram = presence.bound && transport_.inProgram();
        if (shouldStageNow(pendingStage_, presence)) return stageNow();
        return {PlaybackOutcome::StagePending, decision.index};
    }
    case EndAction::Stop:
        transport_.stop();
        return {PlaybackOutcome::Stopped, -1};
    case EndAction::Nothing:
        break;
    }
    return {};
}

PlaybackResult PlaybackEngine::programLayoutChanged() {
    if (!pendingStage_) return {};
    ProgramPresence presence;
    presence.bound = transport_.bound();
    presence.inProgram = presence.bound && transport_.inProgram();
    if (!shouldStageNow(pendingStage_, presence)) return {};
    return stageNow();
}

PlaybackResult PlaybackEngine::stageNow() {
    if (!pendingStage_) return {};
    const int row = pendingStageRow_;
    pendingStage_ = false;
    pendingStageRow_ = -1;
    if (row < 0 || row >= playlist_.size()) return {};
    return stageAt(row);
}

bool PlaybackEngine::stageIsWaiting() const {
    ProgramPresence presence;
    presence.bound = transport_.bound();
    presence.inProgram = presence.bound && transport_.inProgram();
    return shouldWarnPendingStage(pendingStage_, presence);
}

int PlaybackEngine::upNextIndex() const {
    if (pendingStage_ && pendingStageRow_ >= 0 && pendingStageRow_ < playlist_.size())
        return pendingStageRow_;
    if (mode_ == EndMode::Shuffle) {
        const int peeked = shuffle_.peek();
        return (peeked >= 0 && peeked < playlist_.size()) ? peeked : -1;
    }
    const auto decision = decideOnEnd(mode_, playlist_.size(), playlist_.currentIndex(), -1);
    if (decision.action == EndAction::Play || decision.action == EndAction::StageNext)
        return (decision.index >= 0 && decision.index < playlist_.size()) ? decision.index : -1;
    return -1;
}

void PlaybackEngine::playlistChanged() {
    shuffle_.invalidate();
    // A staged row that no longer exists would load an arbitrary clip later.
    if (pendingStageRow_ >= playlist_.size()) {
        pendingStage_ = false;
        pendingStageRow_ = -1;
    }
}

} // namespace pld
