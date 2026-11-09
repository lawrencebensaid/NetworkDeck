<script setup lang="ts">
/*
 * Wi-Fi: on or off, the network the dock keeps, and choosing another. The dock uses Wi-Fi only while no
 * Ethernet cable is connected; with Wi-Fi off the radio stays off, the setup hotspot too. Choosing a
 * network opens its password field in its row; a hidden network is typed under "Other network".
 * Joining, forgetting and turning Wi-Fi off are confirmed on the deck.
 */
import { computed, nextTick, onMounted, ref } from "vue"
import { Check, Lock, RefreshCw } from "@lucide/vue"
import { toast } from "vue-sonner"

import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Spinner } from "@/components/ui/spinner"
import { Switch } from "@/components/ui/switch"
import { cn } from "@/lib/utils"
import { api, type WifiNetwork } from "../session/api.ts"
import { refreshStatus, store } from "../session/store.ts"

const s = computed(() => store.status)
const networks = ref<WifiNetwork[]>([])
const phase = ref<"idle" | "scanning" | "done">("idle")
const chosen = ref<string | null>(null) /* a network's name, or "" for Other network */
const otherName = ref("")
const password = ref("")
const busy = ref<"" | "join" | "toggle" | "forget">("")

/* Four bars, the way phones draw them. */
const bars = (rssi: number) => (rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1)
const quality = (rssi: number) => ["", "Weak", "Fair", "Good", "Excellent"][bars(rssi)]

/* What the saved network is doing now. */
const savedState = computed(() => {
  const st = s.value
  if (!st?.wifi_ssid) return ""
  if (st.link === "Wi-Fi") return `Connected · ${quality(st.wifi_rssi)} signal`
  if (st.link === "Ethernet") return "Standing by: Ethernet is connected"
  if (st.link === "Hotspot") return "Not joined: the dock runs its setup hotspot"
  return "Joining…"
})

async function scan() {
  phase.value = "scanning"
  try {
    networks.value = await api.scan()
  } catch {
    networks.value = []
  }
  phase.value = "done"
}

function choose(name: string | null) {
  chosen.value = chosen.value === name ? null : name
  password.value = ""
  if (chosen.value !== null) void nextTick(() => document.getElementById(chosen.value === "" ? "wifi-other" : "wifi-pw")?.focus())
}

async function join() {
  const name = chosen.value === "" ? otherName.value.trim() : chosen.value
  if (!name) return
  busy.value = "join"
  const r = await api.wifi(name, password.value)
  busy.value = ""
  if (r.ok) {
    toast.success(r.message)
    chosen.value = null
    password.value = ""
  } else toast.error(r.message)
  void refreshStatus()
}

async function toggle(on: boolean) {
  busy.value = "toggle"
  const r = await api.wifiEnabled(on)
  busy.value = ""
  r.ok ? toast.success(r.message) : toast.error(r.message)
  await refreshStatus()
  if (on && store.status?.wifi_enabled) void scan()
}

async function forget() {
  busy.value = "forget"
  const r = await api.wifiForget()
  busy.value = ""
  r.ok ? toast.success(r.message) : toast.error(r.message)
  void refreshStatus()
}

const confirming = computed(() => busy.value !== "" && busy.value !== "toggle" && !store.viaHotspot)

onMounted(() => s.value?.wifi_available && s.value.wifi_enabled && scan())
</script>

<template>
  <p v-if="!s?.wifi_available" class="text-sm text-muted-foreground">Wi-Fi is not available on this board.</p>
  <div v-else class="flex flex-col gap-5">
    <div class="flex items-center justify-between gap-4 rounded-lg border bg-card px-4 py-3">
      <div class="min-w-0">
        <Label for="wifi-on" class="text-sm font-medium">Wi-Fi</Label>
        <p class="text-xs text-muted-foreground">
          {{ s.wifi_enabled ? "Used when no Ethernet cable is connected." : "Off: the dock uses Ethernet only, with no setup hotspot." }}
        </p>
      </div>
      <Spinner v-if="busy === 'toggle'" class="size-4" />
      <Switch id="wifi-on" :model-value="s.wifi_enabled" :disabled="busy !== ''" data-testid="wifi-toggle" @update:model-value="toggle(Boolean($event))" />
    </div>
    <p v-if="busy === 'toggle' && s.wifi_enabled && !store.viaHotspot" class="-mt-3 text-xs text-muted-foreground" role="status">Confirm on the Stream Deck: press ✓.</p>

    <template v-if="s.wifi_enabled">
      <section class="flex flex-col gap-2" aria-label="Saved network">
        <h3 class="text-xs font-semibold tracking-[0.08em] text-muted-foreground uppercase">Saved network</h3>
        <div v-if="s.wifi_ssid" class="flex items-center gap-3 rounded-lg border px-4 py-3" data-testid="wifi-saved">
          <Check class="size-4 shrink-0 text-status-on" />
          <div class="min-w-0 flex-1">
            <div class="truncate text-sm font-medium">{{ s.wifi_ssid }}</div>
            <div class="truncate text-xs text-muted-foreground">{{ savedState }}</div>
          </div>
          <Button variant="ghost" size="sm" :disabled="busy !== ''" @click="forget"><Spinner v-if="busy === 'forget'" class="size-3.5" /> Forget</Button>
        </div>
        <p v-else class="rounded-lg border border-dashed px-4 py-3 text-sm text-muted-foreground">None yet. Choose a network below.</p>
      </section>

      <section class="flex flex-col gap-2" aria-label="Choose a network">
        <div class="flex items-center justify-between">
          <h3 class="text-xs font-semibold tracking-[0.08em] text-muted-foreground uppercase">Choose a network</h3>
          <Button variant="ghost" size="xs" :disabled="phase === 'scanning'" @click="scan">
            <Spinner v-if="phase === 'scanning'" class="size-3" /><RefreshCw v-else /> {{ phase === "scanning" ? "Looking…" : "Scan again" }}
          </Button>
        </div>
        <ul class="flex flex-col overflow-hidden rounded-lg border" aria-label="Networks in range">
          <li v-if="phase === 'scanning' && !networks.length" class="flex items-center gap-2 px-4 py-3 text-sm text-muted-foreground" aria-live="polite">
            <Spinner class="size-3.5" /> Looking for networks. This takes about ten seconds.
          </li>
          <li v-else-if="phase === 'done' && !networks.length" class="px-4 py-3 text-sm text-muted-foreground">No networks in range.</li>
          <li v-for="n in networks" :key="n.ssid" class="border-b last:border-b-0">
            <button
              type="button"
              :aria-expanded="chosen === n.ssid"
              :class="cn('flex w-full items-center gap-3 px-4 py-2.5 text-left text-sm outline-none hover:bg-muted/60 focus-visible:bg-muted', chosen === n.ssid && 'bg-muted/60')"
              :data-network="n.ssid"
              @click="choose(n.ssid)"
            >
              <span class="flex h-3 items-end gap-px" role="img" :aria-label="`${quality(n.rssi)} signal`" :title="`${quality(n.rssi)} signal`">
                <span v-for="b in 4" :key="b" :class="cn('w-[3px] rounded-[1px]', b <= bars(n.rssi) ? 'bg-foreground' : 'bg-muted-foreground/30')" :style="{ height: `${b * 25}%` }" />
              </span>
              <span class="min-w-0 flex-1 truncate">{{ n.ssid }}</span>
              <span v-if="n.ssid === s.wifi_ssid" class="text-xs text-muted-foreground">Saved</span>
              <Lock v-if="n.secure" class="size-3.5 shrink-0 text-muted-foreground" aria-label="Has a password" />
            </button>
            <form v-if="chosen === n.ssid" class="flex flex-col gap-2 px-4 pt-1 pb-3" @submit.prevent="join">
              <Input v-if="n.secure" id="wifi-pw" v-model="password" type="password" autocomplete="off" :placeholder="`Password for ${n.ssid}`" />
              <div class="flex justify-end gap-2">
                <Button type="button" variant="ghost" size="sm" @click="chosen = null">Cancel</Button>
                <Button type="submit" size="sm" :disabled="busy !== '' || (n.secure && password.length < 8)"><Spinner v-if="busy === 'join'" class="size-3.5" /> Join</Button>
              </div>
            </form>
          </li>
          <li>
            <button
              type="button"
              :aria-expanded="chosen === ''"
              :class="cn('flex w-full items-center px-4 py-2.5 text-left text-sm text-muted-foreground outline-none hover:bg-muted/60 hover:text-foreground focus-visible:bg-muted', chosen === '' && 'bg-muted/60 text-foreground')"
              data-network="(other)"
              @click="choose('')"
            >
              Other network…
            </button>
            <form v-if="chosen === ''" class="flex flex-col gap-2 px-4 pt-1 pb-3" @submit.prevent="join">
              <Input id="wifi-other" v-model="otherName" autocomplete="off" placeholder="Network name" />
              <Input v-model="password" type="password" autocomplete="off" placeholder="Password (empty for an open network)" />
              <div class="flex justify-end gap-2">
                <Button type="button" variant="ghost" size="sm" @click="chosen = null">Cancel</Button>
                <Button type="submit" size="sm" :disabled="busy !== '' || !otherName.trim()"><Spinner v-if="busy === 'join'" class="size-3.5" /> Join</Button>
              </div>
            </form>
          </li>
        </ul>
      </section>
      <p v-if="confirming" class="text-xs text-muted-foreground" role="status">Confirm on the Stream Deck: press ✓.</p>
    </template>
  </div>
</template>
