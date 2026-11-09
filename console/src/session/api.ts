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
  wifi_ssid: string;
  dhcp: boolean;
  static_ip: string;
  static_mask: string;
  static_gateway: string;
  host: boolean;
  deck: string;
}

export interface DeckKey {
  v: number;
  color?: string;
}

export interface Deck {
  attached: boolean;
  model?: string;
  cols?: number;
  rows?: number;
  key_px?: number;
  transform?: string;
  source?: "app" | "dock";
  keys?: DeckKey[];
  pressed?: number[];
}

export interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
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
