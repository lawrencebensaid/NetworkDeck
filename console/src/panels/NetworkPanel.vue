<script setup lang="ts">
/* The hostname, and addressing: DHCP or a static address (confirmed on the deck). */
import { ref, watch } from "vue"
import { toast } from "vue-sonner"

import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { RadioGroup, RadioGroupItem } from "@/components/ui/radio-group"
import { Spinner } from "@/components/ui/spinner"
import { api } from "../session/api.ts"
import { refreshStatus, store } from "../session/store.ts"

const mode = ref<"dhcp" | "static">("dhcp")
const ip = ref("")
const mask = ref("")
const gateway = ref("")
const busy = ref(false)
const host = ref("")
const hostBusy = ref(false)
let filled = false

watch(
  () => store.status,
  (s) => {
    if (!s || filled) return
    filled = true // fill once; later refreshes must not overwrite what is being typed
    mode.value = s.dhcp ? "dhcp" : "static"
    ip.value = s.static_ip
    mask.value = s.static_mask
    gateway.value = s.static_gateway
    host.value = s.hostname
  },
  { immediate: true },
)

async function saveHost() {
  hostBusy.value = true
  const r = await api.hostname(host.value.trim())
  hostBusy.value = false
  r.ok ? toast.success(r.message) : toast.error(r.message)
  await refreshStatus()
  if (store.status) host.value = store.status.hostname
}

async function save() {
  busy.value = true
  const r = await api.network({ mode: mode.value, ip: ip.value, mask: mask.value, gateway: gateway.value })
  busy.value = false
  r.ok ? toast.success(r.message) : toast.error(r.message)
  void refreshStatus()
}
</script>

<template>
  <div class="flex flex-col gap-5">
    <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
      <dt class="text-muted-foreground">Connection</dt>
      <dd>{{ store.status?.link ?? "–" }}</dd>
      <dt class="text-muted-foreground">IP address</dt>
      <dd class="font-mono">{{ store.status?.ip || "–" }}</dd>
    </dl>
    <form class="flex flex-col gap-1.5" @submit.prevent="saveHost">
      <Label for="host">Hostname</Label>
      <div class="flex gap-2">
        <div class="flex min-w-0 flex-1 items-center rounded-md border border-input bg-transparent pr-3 focus-within:border-ring focus-within:ring-3 focus-within:ring-ring/50 dark:bg-input/30">
          <input id="host" v-model="host" class="h-9 min-w-0 flex-1 bg-transparent px-3 font-mono text-sm outline-none" autocomplete="off" spellcheck="false" autocapitalize="off" data-testid="hostname" />
          <span class="font-mono text-sm text-muted-foreground">.local</span>
        </div>
        <Button type="submit" variant="outline" :disabled="hostBusy || host.trim() === store.status?.hostname"><Spinner v-if="hostBusy" class="size-4" /> Rename</Button>
      </div>
      <p class="text-xs text-muted-foreground">Open the dock at this name. Your router learns it at the next DHCP lease. Leave it empty for the default.</p>
    </form>
    <div class="border-t" />
    <RadioGroup v-model="mode" class="gap-2">
      <Label class="flex items-center gap-2 font-normal"><RadioGroupItem value="dhcp" /> Automatic (DHCP)</Label>
      <Label class="flex items-center gap-2 font-normal"><RadioGroupItem value="static" /> Static address</Label>
    </RadioGroup>
    <div v-if="mode === 'static'" class="flex flex-col gap-3">
      <div class="flex flex-col gap-1.5"><Label for="ip">IP address</Label><Input id="ip" v-model="ip" inputmode="decimal" placeholder="192.168.1.50" /></div>
      <div class="flex flex-col gap-1.5"><Label for="mask">Netmask</Label><Input id="mask" v-model="mask" inputmode="decimal" placeholder="255.255.255.0" /></div>
      <div class="flex flex-col gap-1.5"><Label for="gw">Gateway</Label><Input id="gw" v-model="gateway" inputmode="decimal" placeholder="192.168.1.1" /></div>
    </div>
    <div class="flex flex-col gap-2">
      <Button :disabled="busy" @click="save"><Spinner v-if="busy" class="size-4" /> Save</Button>
      <p v-if="busy && !store.viaHotspot" class="text-xs text-muted-foreground" role="status">Confirm on the Stream Deck: press ✓.</p>
      <p v-else class="text-xs text-muted-foreground">The dock asks you to confirm on the deck. The Stream Deck app reconnects by itself.</p>
    </div>
  </div>
</template>
