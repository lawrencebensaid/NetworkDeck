<script setup lang="ts">
/*
 * Users: the dock's one account, its password and its passkeys. A passkey signs in without the
 * password (Touch ID, Face ID, a phone or a security key). Passkeys need the page at the dock's name
 * over HTTPS with the dock's certificate authority trusted; until then this says how to get there.
 * Adding one asks for the password again, so a borrowed session cannot add its own way back in.
 */
import { computed, onMounted, ref } from "vue"
import { Download, ExternalLink, KeyRound, Trash2 } from "@lucide/vue"
import { toast } from "vue-sonner"

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog"
import { Button } from "@/components/ui/button"
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle } from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Spinner } from "@/components/ui/spinner"
import { api, type PasskeyInfo } from "../session/api.ts"
import { addPasskey, passkeysSupported, suggestedName } from "../session/passkey.ts"
import { refreshAuth, store } from "../session/store.ts"

/* ── password ── */
const current = ref("")
const next = ref("")
const repeat = ref("")
const pwBusy = ref(false)
const pwOk = computed(() => current.value && next.value.length >= 8 && next.value === repeat.value)

async function changePassword() {
  pwBusy.value = true
  const r = await api.changePassword(current.value, next.value)
  pwBusy.value = false
  if (r.ok) {
    current.value = next.value = repeat.value = ""
    toast.success(r.message)
  } else toast.error(r.message)
}

/* ── passkeys ── */
const keys = ref<PasskeyInfo[]>([])
const ready = computed(() => !!store.auth?.passkey_ready && passkeysSupported())
const host = computed(() => `${store.auth?.hostname || store.status?.hostname || "networkdeck"}.local`)
const secureUrl = computed(() => `https://${host.value}/`)

async function loadKeys() {
  try {
    keys.value = await api.passkeys()
  } catch {
    /* signed out */
  }
}
onMounted(() => {
  void loadKeys()
  void refreshAuth()
})

const adding = ref(false)
const addName = ref("")
const addPassword = ref("")
const addBusy = ref(false)

function startAdd() {
  addName.value = suggestedName()
  addPassword.value = ""
  adding.value = true
}

async function add() {
  addBusy.value = true
  const r = await addPasskey(addPassword.value, addName.value.trim())
  addBusy.value = false
  if (r.ok) {
    adding.value = false
    toast.success(r.message)
    await loadKeys()
    void refreshAuth()
  } else toast.error(r.message)
}

async function remove(k: PasskeyInfo) {
  const r = await api.passkeyDelete(k.id)
  r.ok ? toast.success(r.message) : toast.error(r.message)
  await loadKeys()
  void refreshAuth()
}
</script>

<template>
  <div class="flex flex-col gap-6">
    <div class="flex items-center gap-3">
      <span class="flex size-10 items-center justify-center rounded-full bg-muted text-sm font-semibold">AD</span>
      <div>
        <div class="text-sm font-medium">admin</div>
        <div class="text-xs text-muted-foreground">The dock's account. Signs in with its password or a passkey.</div>
      </div>
    </div>

    <section class="flex flex-col gap-3" aria-labelledby="pk-h">
      <h3 id="pk-h" class="text-xs font-semibold tracking-[0.08em] text-muted-foreground uppercase">Passkeys</h3>
      <ul v-if="keys.length" class="flex flex-col overflow-hidden rounded-lg border" data-testid="passkeys">
        <li v-for="k in keys" :key="k.id" class="flex items-center gap-3 border-b px-4 py-2.5 last:border-b-0">
          <KeyRound class="size-4 shrink-0 text-muted-foreground" />
          <span class="min-w-0 flex-1 truncate text-sm">{{ k.name }}</span>
          <AlertDialog>
            <AlertDialogTrigger as-child>
              <Button variant="ghost" size="icon-sm" :aria-label="`Remove ${k.name}`"><Trash2 /></Button>
            </AlertDialogTrigger>
            <AlertDialogContent>
              <AlertDialogHeader>
                <AlertDialogTitle>Remove “{{ k.name }}”?</AlertDialogTitle>
                <AlertDialogDescription>It can no longer sign in to the dock. Remove it from your password manager too.</AlertDialogDescription>
              </AlertDialogHeader>
              <AlertDialogFooter>
                <AlertDialogCancel>Cancel</AlertDialogCancel>
                <AlertDialogAction @click="remove(k)">Remove</AlertDialogAction>
              </AlertDialogFooter>
            </AlertDialogContent>
          </AlertDialog>
        </li>
      </ul>
      <p v-else class="text-sm text-muted-foreground">Sign in with Touch ID, Face ID, your phone or a security key instead of the password.</p>

      <Button v-if="ready" variant="outline" class="self-start" data-testid="passkey-add" @click="startAdd"><KeyRound /> Add a passkey</Button>
      <div v-else class="flex flex-col gap-3 rounded-lg border bg-muted/30 p-4 text-sm" data-testid="passkey-steps">
        <p v-if="!store.auth?.https_available" class="text-muted-foreground">Passkeys need HTTPS, which is not running on this dock. The serial log says why.</p>
        <template v-else>
          <p class="text-muted-foreground">Passkeys need the dock's secure address. Two steps, once:</p>
          <ol class="flex flex-col gap-3">
            <li class="flex gap-3">
              <span class="flex size-5 shrink-0 items-center justify-center rounded-full bg-muted text-xs font-semibold">1</span>
              <div class="flex flex-col gap-1.5">
                <span>Trust the dock's certificate authority on this computer.</span>
                <span class="text-xs text-muted-foreground">On a Mac: open the file, then in Keychain Access set it to Always Trust. It can only vouch for this dock.</span>
                <Button as-child variant="outline" size="sm" class="self-start"><a href="/ca.pem" download><Download /> Download certificate</a></Button>
              </div>
            </li>
            <li class="flex gap-3">
              <span class="flex size-5 shrink-0 items-center justify-center rounded-full bg-muted text-xs font-semibold">2</span>
              <div class="flex flex-col gap-1.5">
                <span>Open the dock at its secure address and sign in there.</span>
                <Button as-child variant="outline" size="sm" class="self-start"><a :href="secureUrl"><ExternalLink /> {{ secureUrl.replace(/\/$/, "") }}</a></Button>
              </div>
            </li>
          </ol>
        </template>
      </div>
    </section>

    <section class="flex flex-col gap-3 border-t pt-5" aria-labelledby="pw-h">
      <h3 id="pw-h" class="text-xs font-semibold tracking-[0.08em] text-muted-foreground uppercase">Password</h3>
      <form class="flex flex-col gap-3" @submit.prevent="changePassword">
        <div class="flex flex-col gap-1.5"><Label for="cur">Current password</Label><Input id="cur" v-model="current" type="password" autocomplete="current-password" /></div>
        <div class="flex flex-col gap-1.5"><Label for="new">New password</Label><Input id="new" v-model="next" type="password" autocomplete="new-password" /><p class="text-xs text-muted-foreground">At least 8 characters.</p></div>
        <div class="flex flex-col gap-1.5"><Label for="rep">Repeat it</Label><Input id="rep" v-model="repeat" type="password" autocomplete="new-password" /></div>
        <Button type="submit" :disabled="!pwOk || pwBusy" class="self-start"><Spinner v-if="pwBusy" class="size-4" /> Change password</Button>
      </form>
      <p class="text-xs text-muted-foreground">Forgot it? A factory reset on the deck (hold the top-left and top-right keys for 10 seconds) clears it, along with the passkeys, the network settings and Matter.</p>
    </section>

    <Dialog v-model:open="adding">
      <DialogContent class="sm:max-w-sm">
        <DialogHeader>
          <DialogTitle>Add a passkey</DialogTitle>
          <DialogDescription>Your browser or phone makes the passkey and asks you to confirm with Touch ID, Face ID or the device's PIN.</DialogDescription>
        </DialogHeader>
        <form id="add-pk" class="flex flex-col gap-3" @submit.prevent="add">
          <div class="flex flex-col gap-1.5"><Label for="pk-name">Name</Label><Input id="pk-name" v-model="addName" maxlength="64" /></div>
          <div class="flex flex-col gap-1.5">
            <Label for="pk-pw">The dock's password</Label>
            <Input id="pk-pw" v-model="addPassword" type="password" autocomplete="current-password" />
          </div>
        </form>
        <DialogFooter>
          <Button type="button" variant="ghost" @click="adding = false">Cancel</Button>
          <Button type="submit" form="add-pk" :disabled="addBusy || !addPassword || !addName.trim()"><Spinner v-if="addBusy" class="size-4" /> Continue</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  </div>
</template>
