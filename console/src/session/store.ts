/*
 * The page's state: where the session is (checking, choose a password, sign in, signed in, the dock
 * not answering), and while signed in the dock's status and the deck, polled.
 */
import { reactive } from "vue";

import { api, whenSignedOut, type Deck, type Status } from "./api.ts";

export type Phase = "checking" | "setup" | "signed-out" | "signed-in" | "unreachable";

export const store = reactive({
  phase: "checking" as Phase,
  viaHotspot: false,
  status: null as Status | null,
  deck: null as Deck | null,
  lastSeen: 0,
});

let timers: number[] = [];

function stopPolling() {
  timers.forEach((t) => clearInterval(t));
  timers = [];
}

async function pollStatus() {
  try {
    store.status = await api.status();
    store.lastSeen = Date.now();
  } catch {
    /* a moment without the dock (it may be changing network) is shown by the status bar */
  }
}

async function pollDeck() {
  try {
    store.deck = await api.deck();
    store.lastSeen = Date.now();
  } catch {
    /* as above */
  }
}

function startPolling() {
  stopPolling();
  void pollStatus();
  void pollDeck();
  timers.push(window.setInterval(pollStatus, 4000));
  timers.push(window.setInterval(pollDeck, 350));
}

whenSignedOut(() => {
  stopPolling();
  store.phase = "signed-out";
});

export async function checkAuth() {
  try {
    const a = await api.auth();
    store.viaHotspot = a.via_hotspot;
    if (!a.password_set) store.phase = "setup";
    else if (!a.signed_in) store.phase = "signed-out";
    else {
      store.phase = "signed-in";
      startPolling();
    }
  } catch {
    store.phase = "unreachable";
    setTimeout(checkAuth, 3000);
  }
}

export async function signOut() {
  await api.logout();
  stopPolling();
  store.status = null;
  store.deck = null;
  store.phase = "signed-out";
}

export function signedIn() {
  store.phase = "signed-in";
  startPolling();
}

export function refreshStatus() {
  return pollStatus();
}
