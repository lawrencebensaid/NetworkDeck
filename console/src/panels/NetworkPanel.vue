<script setup lang="ts">
/* Addressing: DHCP or a static address. The change is confirmed on the deck. */
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
  },
  { immediate: true },
)

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
