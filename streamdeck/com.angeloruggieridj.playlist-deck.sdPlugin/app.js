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
  [ACTION_PREFIX + "playitem"]: (settings) => [
    "PlayIndex",
    { index: parseInt(settings && settings.index, 10) || 0 },
  ],
};

let sd = null; // Stream Deck websocket
let sdUUID = null;
let globalSettings = { host: "127.0.0.1", port: 4455, password: "" };

// ---------------- obs-websocket v5 client ----------------
const obs = {
  ws: null,
  ready: false,
  nextId: 1,
  queue: [],

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
    try {
      if (this.ws) this.ws.close();
    } catch (e) {}
    this.ws = new WebSocket(`ws://${s.host}:${s.port}`);
    this.ws.onmessage = (ev) => this.onMessage(JSON.parse(ev.data));
    this.ws.onclose = () => {
      this.ready = false;
    };
    this.ws.onerror = () => {
      this.ready = false;
    };
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
      const q = this.queue;
      this.queue = [];
      q.forEach((fn) => fn());
    }
    // op 7 (RequestResponse) is ignored; we don't need the payload here.
  },

  send(obj) {
    this.ws.send(JSON.stringify(obj));
  },

  callVendor(requestType, requestData) {
    const doSend = () => {
      this.send({
        op: 6,
        d: {
          requestType: "CallVendorRequest",
          requestId: "pd-" + this.nextId++,
          requestData: {
            vendorName: VENDOR,
            requestType,
            requestData: requestData || {},
          },
        },
      });
    };
    if (this.ready) doSend();
    else this.queue.push(doSend);
  },
};

function ensureObs() {
  if (obs.isConfigChanged(globalSettings)) obs.connect(globalSettings);
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

  sd.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.event === "didReceiveGlobalSettings") {
      const s = (msg.payload && msg.payload.settings) || {};
      globalSettings = {
        host: s.host || "127.0.0.1",
        port: parseInt(s.port, 10) || 4455,
        password: s.password || "",
      };
      ensureObs();
    } else if (msg.event === "keyUp") {
      const builder = ACTION_REQUESTS[msg.action];
      if (!builder) return;
      ensureObs();
      const [reqType, reqData] = builder(
        (msg.payload && msg.payload.settings) || {}
      );
      obs.callVendor(reqType, reqData);
    }
  };
}
