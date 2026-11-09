<script setup lang="ts">
/* Wi-Fi, used when no Ethernet cable is connected. */
import { Lock, RefreshCw } from "@lucide/vue"
import { onMounted, ref } from "vue"
import { toast } from "vue-sonner"

import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Spinner } from "@/components/ui/spinner"
import { api, type WifiNetwork } from "../session/api.ts"
import { store } from "../session/store.ts"

const networks = ref<WifiNetwork[]>([])
const chosen = ref("")
const password = ref("")
const scanning = ref(false)
const busy = ref(false)

async function scan() {
  scanning.value = true
  try {
    networks.value = await api.scan()
  } catch {
    networks.value = []
  }
  scanning.value = false
}

async function join() {
  busy.value = true
  const r = await api.wifi(chosen.value, password.value)
  busy.value = false
  r.ok ? toast.success(r.message) : toast.error(r.message)
}

function bars(rssi: number) {
  return rssi > -60 ? 3 : rssi > -72 ? 2 : 1
}

onMounted(() => store.status?.wifi_available && scan())
</script>

<template>
  <div class="flex flex-col gap-4">
    <p v-if="!store.status?.wifi_available" class="text-sm text-muted-foreground">Wi-Fi is not available on this board.</p>
    <template v-else>
      <p v-if="store.status?.wifi_ssid" class="text-sm text-muted-foreground">Saved: <strong class="text-foreground">{{ store.status.wifi_ssid }}</strong>.</p>
      <div class="flex items-center justify-between">
        <span class="text-sm font-medium">Networks</span>
        <Button variant="ghost" size="sm" :disabled="scanning" @click="scan">
          <Spinner v-if="scanning" class="size-3.5" /><RefreshCw v-else class="size-3.5" /> Scan
        </Button>
      </div>
      <ul class="flex flex-col overflow-hidden rounded-lg border" role="listbox" aria-label="Networks">
        <li v-if="!networks.length" class="p-3 text-sm text-muted-foreground">{{ scanning ? "Scanning…" : "No networks found" }}</li>
        <li
          v-for="n in networks"
          :key="n.ssid"
          role="option"
          :aria-selected="chosen === n.ssid"
          class="flex cursor-pointer items-center gap-2 border-b px-3 py-2 text-sm last:border-b-0 hover:bg-muted active:bg-muted aria-selected:bg-accent"
          @click="chosen = n.ssid"
        >
          <span class="flex h-3 items-end gap-0.5" aria-hidden="true">
            <span v-for="b in 3" :key="b" class="w-1 rounded-sm" :class="b <= bars(n.rssi) ? 'bg-foreground' : 'bg-muted-foreground/30'" :style="{ height: `${b * 4}px` }" />
          </span>
          <span class="min-w-0 flex-1 truncate">{{ n.ssid }}</span>
          <Lock v-if="n.secure" class="size-3.5 text-muted-foreground" />
        </li>
      </ul>
      <div v-if="chosen" class="flex flex-col gap-1.5">
        <Label for="wpw">Password for {{ chosen }}</Label>
        <Input id="wpw" v-model="password" type="password" autocomplete="off" />
      </div>
      <Button :disabled="!chosen || busy" @click="join"><Spinner v-if="busy" class="size-4" /> Join</Button>
      <p v-if="busy && !store.viaHotspot" class="text-xs text-muted-foreground" role="status">Confirm on the Stream Deck: press ✓.</p>
    </template>
  </div>
</template>
