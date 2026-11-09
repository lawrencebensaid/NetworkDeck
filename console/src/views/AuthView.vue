<script setup lang="ts">
/*
 * Sign in, or (first visit) choose the password. Setting the first password has to be confirmed on the
 * Stream Deck itself, so only someone at the dock can claim it; on the setup hotspot that is already true.
 */
import { computed, ref } from "vue"
import { KeyRound } from "@lucide/vue"

import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Spinner } from "@/components/ui/spinner"
import BrandMark from "../components/BrandMark.vue"
import { api } from "../session/api.ts"
import { passkeysSupported, signInWithPasskey } from "../session/passkey.ts"
import { signedIn, store } from "../session/store.ts"

const props = defineProps<{ setup: boolean }>()

const password = ref("")
const repeat = ref("")
const busy = ref(false)
const error = ref("")

const waitingOnDeck = computed(() => busy.value && props.setup && !store.viaHotspot)
const canSubmit = computed(() =>
  props.setup ? password.value.length >= 8 && password.value === repeat.value : password.value.length > 0,
)

/* Passkeys: here when the page is at the dock's name over HTTPS; otherwise a pointer to that address. */
const passkeyHere = computed(() => !props.setup && (store.auth?.passkeys ?? 0) > 0 && store.auth?.passkey_ready && passkeysSupported())
const passkeyElsewhere = computed(() => !props.setup && (store.auth?.passkeys ?? 0) > 0 && !store.auth?.passkey_ready && store.auth?.https_available)
const secureUrl = computed(() => `https://${store.auth?.hostname || location.hostname}.local/`.replace(".local.local", ".local"))

async function passkey() {
  busy.value = true
  error.value = ""
  const r = await signInWithPasskey()
  busy.value = false
  if (r.ok) signedIn()
  else error.value = r.message
}

async function submit() {
  if (!canSubmit.value || busy.value) return
  busy.value = true
  error.value = ""
  const r = props.setup ? await api.setup(password.value) : await api.login(password.value)
  busy.value = false
  if (r.ok) {
    password.value = repeat.value = ""
    signedIn()
  } else {
    error.value = r.message
  }
}
</script>

<template>
  <main class="flex min-h-dvh items-center justify-center bg-background p-4">
    <Card class="w-full max-w-sm">
      <CardHeader class="items-center text-center">
        <BrandMark :size="40" class="mx-auto mb-2" />
        <CardTitle class="text-xl tracking-tight">{{ setup ? "Welcome to NetworkDeck" : "Sign in" }}</CardTitle>
        <CardDescription>
          {{ setup ? "Choose the password for this dock's web interface." : "Enter the dock's password." }}
        </CardDescription>
      </CardHeader>
      <CardContent>
        <form class="flex flex-col gap-3" @submit.prevent="submit">
          <div class="flex flex-col gap-1.5">
            <Label for="pw">Password</Label>
            <Input
              id="pw"
              v-model="password"
              type="password"
              :autocomplete="setup ? 'new-password' : 'current-password'"
              autofocus
              :disabled="busy"
            />
            <p v-if="setup" class="text-xs text-muted-foreground">At least 8 characters.</p>
          </div>
          <div v-if="setup" class="flex flex-col gap-1.5">
            <Label for="pw2">Repeat it</Label>
            <Input id="pw2" v-model="repeat" type="password" autocomplete="new-password" :disabled="busy" />
          </div>
          <div
            v-if="waitingOnDeck"
            class="flex items-center gap-2 rounded-md border border-sky-500/30 bg-sky-500/10 p-3 text-sm"
            role="status"
          >
            <Spinner class="size-4 shrink-0" />
            <span>Press <strong>✓</strong> on your Stream Deck to confirm. You have 30 seconds.</span>
          </div>
          <p v-if="error" class="text-sm text-destructive" role="alert">{{ error }}</p>
          <Button type="submit" :disabled="!canSubmit || busy" class="mt-1">
            <Spinner v-if="busy" class="size-4" />
            {{ setup ? "Set password" : "Sign in" }}
          </Button>
          <template v-if="passkeyHere">
            <div class="my-1 flex items-center gap-3 text-xs text-muted-foreground"><span class="h-px flex-1 bg-border" />or<span class="h-px flex-1 bg-border" /></div>
            <Button type="button" variant="outline" :disabled="busy" data-testid="passkey-sign-in" @click="passkey"><KeyRound /> Sign in with a passkey</Button>
          </template>
          <p v-else-if="passkeyElsewhere" class="text-center text-xs text-muted-foreground">
            Passkeys work at <a class="underline underline-offset-2 hover:text-foreground" :href="secureUrl">{{ secureUrl.replace(/\/$/, "") }}</a>.
          </p>
          <p v-if="setup && !store.viaHotspot" class="text-center text-xs text-muted-foreground">
            The dock asks you to confirm on the deck, so nobody else on your network can set it first.
          </p>
        </form>
      </CardContent>
    </Card>
  </main>
</template>
