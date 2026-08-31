// Playlist Deck — Stream Deck companion plugin.
//
// Drives the OBS "Playlist Deck" plugin through obs-websocket v5 using OBS's
// vendor request mechanism (CallVendorRequest -> vendorName "obs-playlist-deck").
// Buildless: plain browser JS run inside the Stream Deck plugin WebView.

const ACTION_PREFIX = "com.angeloruggieridj.playlist-deck.";
const VENDOR = "obs-playlist-deck";

// Maps a Stream Deck action UUID to a vendor request (and optional data builder).
const ACTION_REQUESTS = {
  [ACTION_PREFIX + "next"]: () => ["Next", {}],
  [ACTION_PREFIX + "previous"]: () => ["Previous", {}],
  [ACTION_PREFIX + "playpause"]: () => ["PlayPause", {}],
  [ACTION_PREFIX + "stop"]: () => ["Stop", {}],
  [ACTION_PREFIX + "mute"]: () => ["ToggleMute", {}],
  [ACTION_PREFIX + "panic"]: () => ["Panic", {}],
  [ACTION_PREFIX + "playitem"]: (settings) => [
    "PlayIndex",
    { index: parseInt(settings && settings.index, 10) || 0 },
  ],
};

let sd = null; // Stream Deck websocket
let sdUUID = null;
let globalSettings = { host: "127.0.0.1", port: 4455, password: "" };

// Contexts of the visible keys, so their titles can follow what the deck is
// doing and show plainly when OBS is not reachable.
const visibleKeys = new Map(); // context -> { action, settings }

// ---------------- obs-websocket v5 client ----------------
const obs = {
  ws: null,
  ready: false,
  nextId: 1,
  pending: new Map(), // requestId -> resolve
  reconnectDelay: 1000,
  reconnectTimer: null,
  heartbeatTimer: null,
  lastStatus: null,

  isConfigChanged(s) {
    return (
      !this.ws ||
      this._host !== s.host ||
      this._port !== s.port ||
      this._password !== s.password
    );
  },

  connect(s) {
    this._host = s.host;
    this._port = s.port;
    this._password = s.password;
    this.ready = false;
    this.clearTimers();
    try {
      if (this.ws) {
        this.ws.onclose = null; // this close is deliberate; do not schedule a retry for it
        this.ws.close();
      }
    } catch (e) {}
    try {
      this.ws = new WebSocket(`ws://${s.host}:${s.port}`);
    } catch (e) {
      this.scheduleReconnect();
      return;
    }
    this.ws.onmessage = (ev) => {
      let msg;
      try {
        msg = JSON.parse(ev.data);
      } catch (e) {
        return; // a frame that is not JSON is not ours to act on
      }
      this.onMessage(msg);
    };
    this.ws.onclose = () => this.onDisconnected();
    this.ws.onerror = () => this.onDisconnected();
  },

  onDisconnected() {
    const wasReady = this.ready;
    this.ready = false;
    this.lastStatus = null;
    // Anything pressed while OBS was away is dropped, not stored. Queued
    // presses used to be replayed all at once on the next successful connect —
    // seven Next actions firing in a row, live, minutes after the fact.
    this.pending.clear();
    this.clearTimers();
    if (wasReady) refreshAllKeys();
    this.scheduleReconnect();
  },

  clearTimers() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  },

  scheduleReconnect() {
    if (this.reconnectTimer) return;
    const delay = this.reconnectDelay;
    // Exponential backoff up to half a minute: OBS may simply not be running
    // yet, and hammering the port for hours helps nobody.
    this.reconnectDelay = Math.min(this.reconnectDelay * 2, 30000);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect(globalSettings);
    }, delay);
  },

  async onMessage(msg) {
    if (msg.op === 0) {
      // Hello -> Identify
      const identify = { rpcVersion: 1 };
      const auth = msg.d && msg.d.authentication;
      if (auth) {
        identify.authentication = await makeAuth(
          this._password,
          auth.salt,
          auth.challenge
        );
      }
      this.send({ op: 1, d: identify });
    } else if (msg.op === 2) {
      // Identified
      this.ready = true;
      this.reconnectDelay = 1000; // a successful connection resets the backoff
      this.startHeartbeat();
      this.poll();
    } else if (msg.op === 7) {
      // RequestResponse: the vendor reply rides inside responseData.
      const d = msg.d || {};
      const resolve = this.pending.get(d.requestId);
      if (resolve) {
        this.pending.delete(d.requestId);
        resolve((d.responseData && d.responseData.responseData) || null);
      }
    }
  },

  startHeartbeat() {
    this.clearTimers();
    // A websocket that has silently gone away looks identical to an idle one
    // until something is sent. Asking for status every few seconds both proves
    // the link is alive and keeps the key titles current.
    this.heartbeatTimer = setInterval(() => this.poll(), 3000);
  },

  poll() {
    if (!this.ready) return;
    this.callVendor("GetStatus", {}).then((status) => {
      if (!status) return;
      this.lastStatus = status;
      refreshAllKeys();
    });
  },

  send(obj) {
    try {
      this.ws.send(JSON.stringify(obj));
    } catch (e) {
      this.onDisconnected();
    }
  },

  // Returns a promise for the vendor response, or null when OBS is not
  // connected: a press that cannot be delivered now is not delivered later.
  callVendor(requestType, requestData) {
    if (!this.ready) return Promise.resolve(null);
    const requestId = "pd-" + this.nextId++;
    return new Promise((resolve) => {
      this.pending.set(requestId, resolve);
      // Never leave a promise hanging on a reply that will not come.
      setTimeout(() => {
        if (this.pending.has(requestId)) {
          this.pending.delete(requestId);
          resolve(null);
        }
      }, 5000);
      this.send({
        op: 6,
        d: {
          requestType: "CallVendorRequest",
          requestId,
          requestData: {
            vendorName: VENDOR,
            requestType,
            requestData: requestData || {},
          },
        },
      });
    });
  },
};

function ensureObs() {
  if (obs.isConfigChanged(globalSettings)) obs.connect(globalSettings);
}

// ---------------- key feedback ----------------
// Without this the keys say nothing at all: an operator could not tell a broken
// connection from a deck that simply had nothing to play.
function setTitle(context, title) {
  if (!sd) return;
  sd.send(
    JSON.stringify({
      event: "setTitle",
      context,
      payload: { title: title || "", target: 0 },
    })
  );
}

function titleFor(action, settings) {
  if (!obs.ready) return "offline";
  const s = obs.lastStatus;
  if (!s || s.ok === false) return "";
  const short = (text) => (text || "").slice(0, 18);
  if (action.endsWith(".playpause")) return short(s.currentTitle);
  if (action.endsWith(".next")) return short(s.upNextTitle);
  // The mute key is the one place the state is not obvious from the artwork.
  if (action.endsWith(".mute")) return s.muted ? "muted" : "sound on";
  // The playlist a key acts on is worth knowing when several are in play.
  if (action.endsWith(".panic")) return s.playlistName || "";
  if (action.endsWith(".playitem")) {
    const index = parseInt(settings && settings.index, 10) || 0;
    return index === s.currentIndex ? "> " + (index + 1) : String(index + 1);
  }
  return "";
}

function refreshAllKeys() {
  visibleKeys.forEach((info, context) => {
    setTitle(context, titleFor(info.action, info.settings));
  });
}

// ---------------- crypto (obs-websocket v5 auth) ----------------
async function sha256b64(str) {
  const buf = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(str));
  let bin = "";
  new Uint8Array(buf).forEach((b) => (bin += String.fromCharCode(b)));
  return btoa(bin);
}
async function makeAuth(password, salt, challenge) {
  const secret = await sha256b64((password || "") + salt);
  return await sha256b64(secret + challenge);
}

// ---------------- Stream Deck SDK entry point ----------------
function connectElgatoStreamDeckSocket(inPort, inUUID, inRegisterEvent, inInfo) {
  sdUUID = inUUID;
  sd = new WebSocket("ws://127.0.0.1:" + inPort);

  sd.onopen = () => {
    sd.send(JSON.stringify({ event: inRegisterEvent, uuid: inUUID }));
    sd.send(JSON.stringify({ event: "getGlobalSettings", context: inUUID }));
  };

  sd.onerror = () => {
    // Nothing to recover here — the Stream Deck app owns this socket — but an
    // unhandled error event should not take the plugin down with it.
  };

  sd.onmessage = (ev) => {
    let msg;
    try {
      msg = JSON.parse(ev.data);
    } catch (e) {
      return;
    }
    const settings = (msg.payload && msg.payload.settings) || {};

    if (msg.event === "didReceiveGlobalSettings") {
      globalSettings = {
        host: settings.host || "127.0.0.1",
        port: parseInt(settings.port, 10) || 4455,
        password: settings.password || "",
      };
      obs.reconnectDelay = 1000; // new settings deserve an immediate attempt
      ensureObs();
    } else if (msg.event === "willAppear") {
      visibleKeys.set(msg.context, { action: msg.action, settings });
      setTitle(msg.context, titleFor(msg.action, settings));
      ensureObs();
    } else if (msg.event === "willDisappear") {
      visibleKeys.delete(msg.context);
    } else if (msg.event === "didReceiveSettings") {
      const known = visibleKeys.get(msg.context);
      if (known) {
        known.settings = settings;
        setTitle(msg.context, titleFor(msg.action, settings));
      }
    } else if (msg.event === "keyUp") {
      const builder = ACTION_REQUESTS[msg.action];
      if (!builder) return;
      ensureObs();
      if (!obs.ready) {
        // Say so on the key instead of pretending the press worked.
        sd.send(JSON.stringify({ event: "showAlert", context: msg.context }));
        setTitle(msg.context, "offline");
        return;
      }
      const [reqType, reqData] = builder(settings);
      obs.callVendor(reqType, reqData).then(() => obs.poll());
    }
  };
}
