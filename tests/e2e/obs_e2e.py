#!/usr/bin/env python3
"""End-to-end checks of the deck inside a real, running OBS.

The unit tests drive the playback engine against a fake media source, so they
cannot say what OBS really does. This drives the real plugin through
obs-websocket (the plugin's own vendor requests plus OBS's standard ones) and
looks at what comes out:

  skip-play-next   PlayNext: every clip stays current for (nearly) its whole
                   length, so no item is skipped.
  skip-load-next   "Load next (paused)": the staged item stays staged and is not
                   replaced by the one after it.
  last-frame       After the last item of the list the program still shows the
                   final frame instead of black.

It is meant to be run twice, against the released plugin and against a new
build: a check that passes on both proves nothing, and one that fails on the old
one and passes on the new one is the evidence that a fix works.

Needs: pip install websocket-client pillow
"""
import argparse
import base64
import io
import json
import sys
import time
import uuid

import websocket  # websocket-client
from PIL import Image

VENDOR = "obs-playlist-deck"
MODE_PLAY_NEXT = 0
MODE_LOAD_NEXT = 2

# Length of the test clips; set from --clip-seconds. A clip that really played to
# its end is current for about this long. One that was skipped is current for a
# moment, so anything over MIN_DWELL counts as played.
CLIP_SECONDS = 3.0
MIN_DWELL = CLIP_SECONDS * 0.6


class Obs:
    def __init__(self, url):
        self.ws = websocket.create_connection(url, timeout=15)
        hello = json.loads(self.ws.recv())
        if hello["op"] != 0:
            raise RuntimeError(f"unexpected hello: {hello}")
        if "authentication" in hello["d"]:
            raise RuntimeError("the test OBS must run without a websocket password")
        self.ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1}}))
        identified = json.loads(self.ws.recv())
        if identified["op"] != 2:
            raise RuntimeError(f"identify failed: {identified}")

    def request(self, request_type, data=None):
        rid = uuid.uuid4().hex
        self.ws.send(json.dumps({"op": 6, "d": {
            "requestType": request_type, "requestId": rid, "requestData": data or {}}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg["op"] == 7 and msg["d"]["requestId"] == rid:
                status = msg["d"]["requestStatus"]
                if not status["result"]:
                    raise RuntimeError(f"{request_type} failed: {status}")
                return msg["d"].get("responseData") or {}

    def vendor(self, request_type, data=None):
        out = self.request("CallVendorRequest", {
            "vendorName": VENDOR, "requestType": request_type, "requestData": data or {}})
        return out.get("responseData") or {}

    def status(self):
        return self.vendor("GetStatus")

    def program_scene(self, name):
        self.request("SetCurrentProgramScene", {"sceneName": name})

    def screenshot(self, source):
        out = self.request("GetSourceScreenshot", {
            "sourceName": source, "imageFormat": "png",
            "imageWidth": 160, "imageHeight": 90})
        data = out["imageData"].split(",", 1)[1]
        return Image.open(io.BytesIO(base64.b64decode(data))).convert("RGB")


def brightest_channel(image):
    """The largest per-channel mean: ~0 for a black frame."""
    pixels = image.resize((16, 9)).getdata()
    n = len(pixels)
    return max(sum(p[c] for p in pixels) / n for c in range(3))


class Tracker:
    """Samples the deck's current index and records how long each one lasted."""

    def __init__(self, obs):
        self.obs = obs
        self.last = None
        self.since = None
        self.visits = []  # (index, seconds), in order

    def sample(self):
        index = self.obs.status().get("currentIndex", -1)
        now = time.monotonic()
        if index != self.last:
            if self.last is not None and self.last >= 0:
                self.visits.append((self.last, now - self.since))
            self.last, self.since = index, now
        return index

    def close(self):
        if self.last is not None and self.last >= 0:
            self.visits.append((self.last, time.monotonic() - self.since))

    def wait(self, seconds, until=None):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            index = self.sample()
            if until is not None and until(index):
                return True
            time.sleep(0.05)
        return until is None

    def sequence(self):
        return [i for i, _ in self.visits]


def reset(obs, clips, mode):
    obs.program_scene("Deck")
    obs.vendor("Clear")
    time.sleep(0.5)
    obs.vendor("SetMode", {"mode": mode})
    obs.vendor("AddPaths", {"paths": [{"value": c} for c in clips]})
    deadline = time.monotonic() + 15
    while obs.status().get("count") != len(clips):
        if time.monotonic() > deadline:
            raise RuntimeError("the playlist did not fill")
        time.sleep(0.2)
    # AddPaths does not guarantee an order while it probes; the test needs one.
    items = obs.vendor("GetItems").get("items", [])
    got = [i["path"] for i in items]
    if got != clips:
        raise RuntimeError(f"playlist order is {got}, expected {clips}")


def check_skip_play_next(obs, clips):
    reset(obs, clips, MODE_PLAY_NEXT)
    t = Tracker(obs)
    obs.vendor("PlayIndex", {"index": 0})
    deadline = time.monotonic() + CLIP_SECONDS * len(clips) + 10
    reached_last = False
    while time.monotonic() < deadline:
        index = t.sample()
        if index == len(clips) - 1:
            reached_last = True
        # The last clip is over once the deck reports nothing playing.
        if reached_last and not obs.status().get("playing"):
            break
        time.sleep(0.05)
    t.close()
    seq = t.sequence()
    detail = f"visited {seq}, dwell " + ", ".join(f"{i}:{s:.1f}s" for i, s in t.visits)
    short = [i for i, s in t.visits[:-1] if s < MIN_DWELL]
    ok = seq == list(range(len(clips))) and not short
    return ok, detail


def check_skip_load_next(obs, clips):
    reset(obs, clips, MODE_LOAD_NEXT)
    t = Tracker(obs)
    obs.vendor("PlayIndex", {"index": 0})
    problems = []
    for step in range(1, len(clips)):
        # The clip ends with the source on air: the next one is only queued.
        t.wait(CLIP_SECONDS + 1.5)
        obs.program_scene("Other")  # off air: the queued clip is loaded, paused
        if not t.wait(5, until=lambda i, s=step: i == s):
            problems.append(f"item {step} was never staged")
            break
        # Held staged: a stray end must not replace it with the one after it.
        t.wait(3)
        if t.last != step:
            problems.append(f"staged item {step} was replaced by {t.last}")
            break
        obs.program_scene("Deck")
        t.wait(1.5)
        if not obs.status().get("playing"):
            obs.vendor("PlayPause")
    t.wait(CLIP_SECONDS + 1.5)
    t.close()
    seq = t.sequence()
    detail = f"visited {seq}, dwell " + ", ".join(f"{i}:{s:.1f}s" for i, s in t.visits)
    ok = not problems and seq == list(range(len(clips)))
    if problems:
        detail += "; " + "; ".join(problems)
    return ok, detail


def check_last_frame(obs, clips):
    reset(obs, clips, MODE_PLAY_NEXT)
    obs.vendor("PlayIndex", {"index": len(clips) - 1})
    # Let it play to the end and the deck declare itself finished.
    time.sleep(CLIP_SECONDS + 3)
    shot = obs.screenshot("Deck")
    level = brightest_channel(shot)
    return level > 40, f"program brightness {level:.0f}/255 after the last clip (black is ~0)"


CHECKS = [
    ("skip-play-next", check_skip_play_next),
    ("skip-load-next", check_skip_load_next),
    ("last-frame", check_last_frame),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="ws://127.0.0.1:4455")
    ap.add_argument("--clips", nargs=3, required=True, help="three media files, in order")
    ap.add_argument("--label", default="plugin")
    ap.add_argument("--clip-seconds", type=float, default=3.0,
                    help="length of the three clips (they must really be this long)")
    ap.add_argument("--json", help="write the results here")
    args = ap.parse_args()
    global CLIP_SECONDS, MIN_DWELL
    CLIP_SECONDS = args.clip_seconds
    MIN_DWELL = CLIP_SECONDS * 0.6

    obs = Obs(args.url)
    version = obs.status().get("pluginVersion", "?")
    print(f"== {args.label}: plugin {version}")
    results = {}
    for name, fn in CHECKS:
        try:
            ok, detail = fn(obs, args.clips)
        except Exception as exc:  # a check that cannot run is a failure, loudly
            ok, detail = False, f"error: {exc}"
        results[name] = {"ok": ok, "detail": detail}
        print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"label": args.label, "pluginVersion": version, "results": results}, f, indent=2)
    return 0 if all(r["ok"] for r in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
