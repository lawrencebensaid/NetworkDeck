/*
 * Passkeys in the browser: the WebAuthn calls, with the dock's options (base64url in JSON) turned into
 * the ArrayBuffers the browser wants and its answers turned back. Passkeys need a secure page at the
 * dock's name (https://<name>.local with its certificate authority trusted), which the dock says in
 * /api/auth as passkey_ready.
 */
import type { Result } from "./api.ts"

export const passkeysSupported = () => window.isSecureContext && typeof window.PublicKeyCredential !== "undefined"

function toBuf(b64url: string): ArrayBuffer {
  const b64 = b64url.replace(/-/g, "+").replace(/_/g, "/") + "===".slice((b64url.length + 3) % 4)
  const bin = atob(b64)
  const out = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i)
  return out.buffer
}

function toB64url(buf: ArrayBuffer): string {
  const bytes = new Uint8Array(buf)
  let bin = ""
  for (const b of bytes) bin += String.fromCharCode(b)
  return btoa(bin).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "")
}

async function postForm(path: string, data: Record<string, string>): Promise<Response> {
  return fetch(path, {
    method: "POST",
    credentials: "same-origin",
    headers: { "X-NetworkDeck": "1", "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams(data),
  })
}

/* An options request: the options as JSON when it worked, the dock's Result when it did not. */
async function options(path: string, data: Record<string, string>): Promise<Record<string, unknown>> {
  const r = await postForm(path, data)
  const body = (await r.json()) as Record<string, unknown>
  if (!r.ok || "ok" in body) throw new Error((body as unknown as Result).message ?? `The dock answered ${r.status}`)
  return body
}

type Descriptor = { type: "public-key"; id: string }
const descriptors = (list: unknown) => ((list as Descriptor[] | undefined) ?? []).map((d) => ({ type: d.type, id: toBuf(d.id) }))

/* Why a WebAuthn call failed, in words; a dismissed prompt is not an error worth more than a line. */
function why(e: unknown): string {
  if (e instanceof DOMException && e.name === "NotAllowedError") return "Cancelled, or the passkey prompt timed out."
  if (e instanceof DOMException && e.name === "InvalidStateError") return "This passkey is on the dock already."
  return e instanceof Error ? e.message : String(e)
}

export async function addPasskey(password: string, name: string): Promise<Result> {
  try {
    const o = await options("/api/passkey/register/options", { password })
    const user = o.user as { id: string; name: string; displayName: string }
    const cred = (await navigator.credentials.create({
      publicKey: {
        ...(o as object),
        challenge: toBuf(o.challenge as string),
        user: { ...user, id: toBuf(user.id) },
        excludeCredentials: descriptors(o.excludeCredentials),
      } as PublicKeyCredentialCreationOptions,
    })) as PublicKeyCredential | null
    if (!cred) return { ok: false, message: "No passkey was made." }
    const res = cred.response as AuthenticatorAttestationResponse
    const r = await postForm("/api/passkey/register", {
      client: toB64url(res.clientDataJSON),
      attestation: toB64url(res.attestationObject),
      name,
    })
    return (await r.json()) as Result
  } catch (e) {
    return { ok: false, message: why(e) }
  }
}

export async function signInWithPasskey(): Promise<Result> {
  try {
    const o = await options("/api/passkey/login/options", {})
    const cred = (await navigator.credentials.get({
      publicKey: {
        ...(o as object),
        challenge: toBuf(o.challenge as string),
        allowCredentials: descriptors(o.allowCredentials),
      } as PublicKeyCredentialRequestOptions,
    })) as PublicKeyCredential | null
    if (!cred) return { ok: false, message: "No passkey was chosen." }
    const res = cred.response as AuthenticatorAssertionResponse
    const r = await postForm("/api/passkey/login", {
      id: toB64url(cred.rawId),
      client: toB64url(res.clientDataJSON),
      authenticator: toB64url(res.authenticatorData),
      signature: toB64url(res.signature),
    })
    return (await r.json()) as Result
  } catch (e) {
    return { ok: false, message: why(e) }
  }
}

/* A name for a new passkey, from the device it is made on. */
export function suggestedName(): string {
  const ua = navigator.userAgent
  if (/iPhone/.test(ua)) return "iPhone"
  if (/iPad/.test(ua)) return "iPad"
  if (/Macintosh/.test(ua)) return "Mac"
  if (/Android/.test(ua)) return "Android"
  if (/Windows/.test(ua)) return "Windows"
  return "Passkey"
}
