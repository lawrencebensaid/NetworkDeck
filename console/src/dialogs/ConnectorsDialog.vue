<script setup lang="ts">
/*
 * The connectors: the services through which other systems use the dock, each switched on here. Matter
 * (Home Assistant, Apple Home, Google Home) and the REST API with its API keys; an MCP server is planned
 * and listed as such. The list is at the left with each one's state, the chosen one at the right.
 */
import { computed, onBeforeUnmount, ref, watch, type Component } from "vue"
import { Cable, Webhook, X } from "@lucide/vue"
import MatterIcon from "../components/MatterIcon.vue"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Dialog, DialogClose, DialogContent, DialogDescription, DialogTitle } from "@/components/ui/dialog"
import { cn } from "@/lib/utils"
import { Label } from "@/components/ui/label"
import { Switch } from "@/components/ui/switch"
import { toast } from "vue-sonner"
import ApiKeysPanel from "../panels/ApiKeysPanel.vue"
import MatterPanel from "../panels/MatterPanel.vue"
import { api, type Matter } from "../session/api.ts"
import { store } from "../session/store.ts"

const open = defineModel<boolean>("open", { required: true })
const chosen = ref<"matter" | "rest" | "mcp">("matter")

/* Matter's state and the API keys for the list, while the dialog is open (the panel polls its own). */
const matter = ref<Matter | null>(null)
const keys = ref<number | null>(null)
const rest = ref<boolean | null>(null)
let timer = 0
async function load() {
  try {
    matter.value = await api.matter()
    keys.value = (await api.keys()).length
    rest.value = (await api.rest()).enabled
  } catch {
    /* signed out or offline */
  }
}
const base = location.origin

async function setRest(on: boolean) {
  const r = await api.setRest(on)
  if (r.ok) rest.value = on
  else toast.error(r.message)
}
watch(
  open,
  (now) => {
    clearInterval(timer)
    if (now) {
      void load()
      timer = window.setInterval(load, 3000)
    }
  },
  { immediate: true },
)
onBeforeUnmount(() => clearInterval(timer))

interface Integration {
  id: "matter" | "rest" | "mcp"
  name: string
  icon: Component
  state: string
  later?: boolean
}

const integrations = computed<Integration[]>(() => {
  const m = matter.value
  const n = m?.fabrics ?? 0
  const matterState = store.system && !store.system.matter ? "Not on this chip" : !m ? "…" : !m.running ? (m.enabled ? "Did not start" : "Off") : n > 0 ? (n === 1 ? "1 controller" : `${n} controllers`) : m.window_open ? "Ready to pair" : "Not paired"
  return [
    { id: "matter", name: "Matter", icon: MatterIcon, state: matterState },
    {
      id: "rest",
      name: "REST API",
      icon: Webhook,
      state: rest.value === null ? "…" : !rest.value ? "Off" : keys.value === 1 ? "On · 1 API key" : `On · ${keys.value ?? 0} API keys`,
    },
    { id: "mcp", name: "MCP", icon: Cable, state: "Planned", later: true },
  ]
})

const LATER = {
  mcp: {
    title: "MCP",
    text: "Use the deck from any Model Context Protocol client: see what is on the keys, put things on them and hear about presses.",
  },
} as const
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent
      :show-close-button="false"
      data-testid="connectors-dialog"
      class="flex h-[min(600px,calc(100dvh-2rem))] w-[calc(100vw-2rem)] max-w-none flex-col gap-0 overflow-hidden bg-background p-0 sm:max-w-[760px] max-sm:top-0 max-sm:left-0 max-sm:h-dvh max-sm:w-screen max-sm:translate-x-0 max-sm:translate-y-0 max-sm:rounded-none"
    >
      <header class="flex h-12 shrink-0 items-center justify-between gap-3 border-b bg-card px-4">
        <DialogTitle class="text-sm font-semibold">Connectors</DialogTitle>
        <DialogDescription class="sr-only">The services other systems use the dock through: Matter, the REST API, and later MCP.</DialogDescription>
        <DialogClose as-child>
          <Button type="button" variant="ghost" size="icon-sm" aria-label="Close connectors"><X /></Button>
        </DialogClose>
      </header>

      <div class="flex min-h-0 flex-1 flex-col sm:flex-row">
        <ul class="flex shrink-0 gap-1 overflow-x-auto border-b p-2 sm:w-60 sm:flex-col sm:border-r sm:border-b-0" aria-label="Connectors">
          <li v-for="c in integrations" :key="c.id" class="shrink-0">
            <button
              type="button"
              :data-connector="c.id"
              :aria-current="chosen === c.id ? 'true' : undefined"
              :class="
                cn(
                  'flex w-full items-center gap-3 rounded-md px-2.5 py-2 text-left outline-none transition-colors focus-visible:ring-3 focus-visible:ring-ring/50',
                  chosen === c.id ? 'bg-muted' : 'hover:bg-muted/60',
                )
              "
              @click="chosen = c.id"
            >
              <span class="flex size-8 shrink-0 items-center justify-center rounded-full bg-muted text-muted-foreground" :class="chosen === c.id && 'bg-background text-foreground'">
                <component :is="c.icon" class="size-4" />
              </span>
              <span class="min-w-0 flex-1">
                <span class="block text-sm font-medium" :class="c.later && 'text-muted-foreground'">{{ c.name }}</span>
                <span class="block truncate text-xs text-muted-foreground">{{ c.state }}</span>
              </span>
            </button>
          </li>
        </ul>

        <div class="min-h-0 min-w-0 flex-1 overflow-y-auto">
          <div class="max-w-lg px-4 py-5 sm:px-6">
            <template v-if="chosen === 'matter'">
              <h2 class="text-base font-semibold tracking-tight">Matter</h2>
              <p class="mt-0.5 mb-5 text-sm text-muted-foreground">Home Assistant, Apple Home and Google Home: every key is a button there, for automations.</p>
              <p v-if="store.system && !store.system.matter" class="text-sm text-muted-foreground">Matter isn't in the firmware for the {{ store.system.chip }} yet: it needs more memory than this chip has next to the web interface.</p>
              <MatterPanel v-else />
            </template>
            <template v-else-if="chosen === 'rest'">
              <h2 class="text-base font-semibold tracking-tight">REST API</h2>
              <p class="mt-0.5 mb-5 text-sm text-muted-foreground">
                What this page does, for scripts and other tools: the dock's status, the deck and its keys, the log and the settings.
                Every change is still confirmed on the deck.
              </p>
              <div class="flex items-center justify-between gap-4 rounded-lg border p-3">
                <div>
                  <Label for="rest-on">REST API</Label>
                  <p class="text-xs text-muted-foreground">{{ rest ? "On: requests with an API key are answered." : "Off: API keys are refused." }}</p>
                </div>
                <Switch id="rest-on" :model-value="!!rest" :disabled="rest === null" data-testid="rest-toggle" @update:model-value="setRest(Boolean($event))" />
              </div>
              <template v-if="rest">
                <div class="mt-5 flex flex-col gap-2 text-sm">
                  <p>Send an API key with each request:</p>
                  <pre class="overflow-x-auto rounded-md border bg-muted/40 p-3 font-mono text-xs leading-relaxed">curl -H "Authorization: Bearer ndk_…" {{ base }}/api/status</pre>
                  <p class="text-muted-foreground">The endpoints are listed in <code>docs/api.md</code> in the NetworkDeck repository.</p>
                </div>
                <h3 class="mt-6 mb-3 text-sm font-semibold">API keys</h3>
                <ApiKeysPanel @change="load" />
              </template>
            </template>
            <template v-else>
              <div class="flex items-center gap-2">
                <h2 class="text-base font-semibold tracking-tight">{{ LATER[chosen].title }}</h2>
                <Badge variant="secondary">Planned</Badge>
              </div>
              <p class="mt-2 text-sm text-muted-foreground">{{ LATER[chosen].text }}</p>
              <p class="mt-4 text-sm text-muted-foreground">It comes in a later firmware update.</p>
            </template>
          </div>
        </div>
      </div>
    </DialogContent>
  </Dialog>
</template>
