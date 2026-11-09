<script setup lang="ts">
/*
 * The integrations: how other systems use the dock. Matter (Home Assistant, Apple Home, Google Home)
 * now; a REST API and an MCP server for AI assistants are planned and listed as such. The list is at
 * the left with each one's state, the chosen one at the right.
 */
import { computed, onBeforeUnmount, ref, watch, type Component } from "vue"
import { Bot, House, Webhook, X } from "@lucide/vue"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Dialog, DialogClose, DialogContent, DialogDescription, DialogTitle } from "@/components/ui/dialog"
import { cn } from "@/lib/utils"
import MatterPanel from "../panels/MatterPanel.vue"
import { api, type Matter } from "../session/api.ts"

const open = defineModel<boolean>("open", { required: true })
const chosen = ref<"matter" | "rest" | "mcp">("matter")

/* Matter's state for the list, while the dialog is open (the panel polls its own). */
const matter = ref<Matter | null>(null)
let timer = 0
async function load() {
  try {
    matter.value = await api.matter()
  } catch {
    /* signed out or offline */
  }
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
  const matterState = !m ? "…" : !m.running ? (m.enabled ? "Did not start" : "Off") : n > 0 ? (n === 1 ? "1 controller" : `${n} controllers`) : m.window_open ? "Ready to pair" : "Not paired"
  return [
    { id: "matter", name: "Matter", icon: House, state: matterState },
    { id: "rest", name: "REST API", icon: Webhook, state: "Planned", later: true },
    { id: "mcp", name: "MCP", icon: Bot, state: "Planned", later: true },
  ]
})

const LATER = {
  rest: {
    title: "REST API",
    text: "Read the deck and its keys, draw on them and change the dock's settings from scripts and other tools, with API keys in place of the password.",
  },
  mcp: {
    title: "MCP",
    text: "Let an AI assistant use the deck through the Model Context Protocol: see what is on the keys, put things on them and hear about presses.",
  },
} as const
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent
      :show-close-button="false"
      data-testid="integrations-dialog"
      class="flex h-[min(600px,calc(100dvh-2rem))] w-[calc(100vw-2rem)] max-w-none flex-col gap-0 overflow-hidden bg-background p-0 sm:max-w-[760px] max-sm:top-0 max-sm:left-0 max-sm:h-dvh max-sm:w-screen max-sm:translate-x-0 max-sm:translate-y-0 max-sm:rounded-none"
    >
      <header class="flex h-12 shrink-0 items-center justify-between gap-3 border-b bg-card px-4">
        <DialogTitle class="text-sm font-semibold">Integrations</DialogTitle>
        <DialogDescription class="sr-only">How other systems use the dock: Matter, and later a REST API and MCP.</DialogDescription>
        <DialogClose as-child>
          <Button type="button" variant="ghost" size="icon-sm" aria-label="Close integrations"><X /></Button>
        </DialogClose>
      </header>

      <div class="flex min-h-0 flex-1 flex-col sm:flex-row">
        <ul class="flex shrink-0 gap-1 overflow-x-auto border-b p-2 sm:w-60 sm:flex-col sm:border-r sm:border-b-0" aria-label="Integrations">
          <li v-for="c in integrations" :key="c.id" class="shrink-0">
            <button
              type="button"
              :data-integration="c.id"
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
              <MatterPanel />
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
