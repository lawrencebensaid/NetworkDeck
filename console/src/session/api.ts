/*
 * The dock's API. The session is an HttpOnly cookie the dock sets at sign-in; every change also carries
 * X-NetworkDeck, which a page on another site cannot send. A 401 means the session is gone.
 */
export interface Result {
  ok: boolean;
  message: string;
}

export interface AuthState {
  password_set: boolean;
  signed_in: boolean;
  via_hotspot: boolean;
  https: boolean; /* this page came over HTTPS */
  https_available: boolean; /* the dock serves HTTPS */
  passkey_ready: boolean; /* HTTPS at the dock's name: passkeys work here */
  passkeys: number;
  hostname: string;
}

export interface PasskeyInfo {
  id: string;
  name: string;
}

export interface Status {
  via_hotspot: boolean;
  name: string;
  serial: string;
  firmware: string;
  build: string;
  link: string;
  ip: string;
  wifi_available: boolean;
  wifi_enabled: boolean;
  wifi_ssid: string; /* the saved network */
  wifi_rssi: number; /* dBm while joined, else 0 */
  hostname: string; /* without .local */
  dhcp: boolean;
  static_ip: string;
  static_mask: string;
  static_gateway: string;
  host: boolean;
  host_ip: string; /* the computer running the Stream Deck app, empty when none */
  host_seconds: number; /* connected this long */
  host_name?: string; /* the computer's own name, as it announces itself ("Lawrence's MacBook Pro") */
  host_model?: string; /* its model code (MacBookPro18,1), when announced */
  deck: string;
}

export interface DeckKey {
  v: number;
  color?: string;
}

export interface Deck {
  attached: boolean;
  pid?: number; /* USB product id */
  live?: boolean; /* false: a deck the dock bridges but can't show the keys of */
  finish?: "black" | "white" | ""; /* as chosen for this deck (by serial); empty: not chosen */
  quiet_ms?: number; /* since the Stream Deck app last changed a key */
  model?: string;
  cols?: number;
  rows?: number;
  key_px?: number;
  transform?: string;
  source?: "app" | "dock";
  brightness?: number; /* of the dock's own screens, 0..100 */
  product?: string; /* USB product string */
  serial?: string;
  usb_rev?: string; /* bcdDevice, as 1.00 */
  keys?: DeckKey[];
  pressed?: number[];
}

export interface ApiKeyInfo {
  id: string;
  name: string;
  created: number; /* Unix time, 0 if the dock's clock wasn't set */
  last_used: number; /* Unix time since the dock started, 0 if not used */
}

export interface ApiKeyMade extends Result {
  id?: string;
  name?: string;
  key?: string; /* shown once */
}

export type LogCategory = "system" | "update" | "network" | "connection" | "deck" | "security" | "matter";

export interface LogRecord {
  s: number; /* sequence number */
  b: number; /* start (boot) number */
  t: number; /* Unix time, 0 before the clock was set */
  u: number; /* seconds since that start */
  c: LogCategory;
  l: "info" | "warn" | "error";
  m: string;
}

export interface LogPage {
  capacity: number;
  kept: number;
  boot: number; /* the current start */
  now: number; /* the dock's Unix time, 0 if not set */
  uptime: number; /* seconds since this start */
  records: LogRecord[]; /* newest first */
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

export interface Matter {
  enabled?: boolean; /* set up; it runs from now on */
  running: boolean;
  fabrics?: number; /* controllers it has been added to */
  window_open?: boolean; /* a controller can add it now */
  window_seconds?: number;
  qr?: string; /* MT:… */
  manual?: string; /* 11 digits */
}

export class SignedOut extends Error {}

let onSignedOut: () => void = () => {};
export function whenSignedOut(fn: () => void) {
  onSignedOut = fn;
}

async function get<T>(path: string): Promise<T> {
  const r = await fetch(path, { credentials: "same-origin", cache: "no-store" });
  if (r.status === 401) {
    onSignedOut();
    throw new SignedOut();
  }
  if (!r.ok) throw new Error(`${path}: ${r.status}`);
  return (await r.json()) as T;
}

export async function post(path: string, data: Record<string, string> = {}): Promise<Result> {
  const r = await fetch(path, {
    method: "POST",
    credentials: "same-origin",
    headers: { "X-NetworkDeck": "1", "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(data),
  });
  let res: Result;
  try {
    res = (await r.json()) as Result;
  } catch {
    res = { ok: false, message: `The dock answered ${r.status}` };
  }
  if (r.status === 401 && path !== "/api/login" && path !== "/api/password") onSignedOut();
  return res;
}

export const api = {
  auth: () => get<AuthState>("/api/auth"),
  status: () => get<Status>("/api/status"),
  deck: () => get<Deck>("/api/deck"),
  scan: () => get<WifiNetwork[]>("/api/scan"),
  brightness: (percent: number) => post("/api/deck/brightness", { value: String(percent) }),
  matter: () => get<Matter>("/api/matter"),
  passkeys: () => get<PasskeyInfo[]>("/api/passkeys"),
  passkeyDelete: (id: string) => post("/api/passkey/delete", { id }),
  matterSetup: () => post("/api/matter/setup"),
  matterOpen: () => post("/api/matter/open"),
  matterOff: () => post("/api/matter/off"),
  wifiEnabled: (on: boolean) => post("/api/wifi/enabled", { value: on ? "1" : "0" }),
  wifiForget: () => post("/api/wifi/forget"),
  hostname: (value: string) => post("/api/hostname", { value }),
  finish: (value: "black" | "white") => post("/api/deck/finish", { value }),
  keys: () => get<ApiKeyInfo[]>("/api/keys"),
  keyCreate: (name: string) => post("/api/keys", { name }) as Promise<ApiKeyMade>,
  keyDelete: (id: string) => post("/api/keys/delete", { id }),
  log: (before = 0, limit = 300) => get<LogPage>(`/api/log?before=${before}&limit=${limit}`),
  trace: async () => {
    const r = await fetch("/api/trace", { credentials: "same-origin", cache: "no-store" });
    if (r.status === 401) {
      onSignedOut();
      throw new SignedOut();
    }
    return r.text();
  },
  login: (password: string) => post("/api/login", { password }),
  logout: () => post("/api/logout"),
  setup: (password: string) => post("/api/setup", { password }),
  changePassword: (current: string, password: string) => post("/api/password", { current, password }),
  wifi: (ssid: string, password: string) => post("/api/wifi", { ssid, password }),
  network: (data: Record<string, string>) => post("/api/network", data),
  /* The firmware upload streams the file; progress comes from XHR's upload events. */
  update(file: File, onProgress: (fraction: number) => void): Promise<Result> {
    return new Promise((resolve) => {
      const x = new XMLHttpRequest();
      x.upload.onprogress = (e) => e.lengthComputable && onProgress(e.loaded / e.total);
      x.onload = () => {
        if (x.status === 401) onSignedOut();
        try {
          resolve(JSON.parse(x.responseText) as Result);
        } catch {
          resolve({ ok: false, message: "Upload failed" });
        }
      };
      x.onerror = () => resolve({ ok: false, message: "Upload failed: the dock did not answer" });
      x.open("POST", "/api/update");
      x.setRequestHeader("X-NetworkDeck", "1");
      x.setRequestHeader("Content-Type", "application/octet-stream");
      x.send(file);
    });
  },
};
