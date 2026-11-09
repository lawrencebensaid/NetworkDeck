<script setup lang="ts">
/*
 * The settings window: the dock's system options, network, Wi-Fi, users, this page's appearance and the
 * firmware, one at a time, and
 * what it says of itself. Every change is confirmed on the deck (the sections say so where it applies).
 */
import { computed, type Component } from "vue"
import { X } from "@lucide/vue"

import { Button } from "@/components/ui/button"
import { Dialog, DialogClose, DialogContent, DialogDescription, DialogTitle } from "@/components/ui/dialog"
import { cn } from "@/lib/utils"
import AboutPanel from "../panels/AboutPanel.vue"
import AppearancePanel from "../panels/AppearancePanel.vue"
import FirmwarePanel from "../panels/FirmwarePanel.vue"
import NetworkPanel from "../panels/NetworkPanel.vue"
import SystemPanel from "../panels/SystemPanel.vue"
import UsersPanel from "../panels/UsersPanel.vue"
import WifiPanel from "../panels/WifiPanel.vue"
import { store } from "../session/store.ts"

const open = defineModel<boolean>("open", { required: true })
const section = defineModel<string>("section", { default: "system" })

interface Section {
  id: string
  title: string
  blurb: string
  view: Component
}

const sections = computed<Section[]>(() => [
  { id: "system", title: "System", blurb: "The dock's hardware options: the status LED, pins, restarting.", view: SystemPanel },
  { id: "network", title: "Network", blurb: "How the dock gets its address on your network.", view: NetworkPanel },
  ...(store.status?.wifi_available
    ? [{ id: "wifi", title: "Wi-Fi", blurb: "The dock's wireless network.", view: WifiPanel }]
    : []),
  { id: "users", title: "Users", blurb: "Who signs in to this web interface, and how: a password or passkeys.", view: UsersPanel },
  { id: "appearance", title: "Appearance", blurb: "How this page looks in this browser.", view: AppearancePanel },
  { id: "firmware", title: "Firmware", blurb: "What runs on the dock, and installing an update.", view: FirmwarePanel },
])
const extra: Section[] = [{ id: "about", title: "About", blurb: "What the dock says of itself.", view: AboutPanel }]

const active = computed(() => [...sections.value, ...extra].find((s) => s.id === section.value) ?? sections.value[0]!)

const navClass = (id: string) =>
  cn(
    "flex shrink-0 items-center rounded-md px-2.5 py-1.5 text-left text-sm whitespace-nowrap transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
    active.value.id === id ? "bg-muted font-medium text-foreground" : "text-muted-foreground hover:bg-muted/60 hover:text-foreground",
  )
</script>

<template>
  <Dialog v-model:open="open">
    <DialogContent
      :show-close-button="false"
      data-testid="settings-dialog"
      class="flex h-[min(640px,calc(100dvh-2rem))] w-[calc(100vw-2rem)] max-w-none flex-col gap-0 overflow-hidden bg-background p-0 sm:max-w-[880px] max-sm:top-0 max-sm:left-0 max-sm:h-dvh max-sm:w-screen max-sm:translate-x-0 max-sm:translate-y-0 max-sm:rounded-none"
    >
      <header class="flex h-12 shrink-0 items-center justify-between gap-3 border-b bg-card px-4">
        <DialogTitle class="text-sm font-semibold">Settings</DialogTitle>
        <DialogDescription class="sr-only">The dock's network, Wi-Fi, firmware and users.</DialogDescription>
        <DialogClose as-child>
          <Button type="button" variant="ghost" size="icon-sm" aria-label="Close settings"><X /></Button>
        </DialogClose>
      </header>

      <div class="flex min-h-0 flex-1 flex-col sm:flex-row">
        <nav aria-label="Settings sections" class="flex shrink-0 gap-1 overflow-x-auto border-b p-2 sm:w-48 sm:flex-col sm:overflow-y-auto sm:border-r sm:border-b-0 sm:p-3">
          <button v-for="s in sections" :key="s.id" type="button" :data-nav="s.id" :aria-current="active.id === s.id ? 'page' : undefined" :class="navClass(s.id)" @click="section = s.id">
            {{ s.title }}
          </button>
          <div class="mx-2 my-1 hidden border-t sm:block" role="separator" />
          <button v-for="s in extra" :key="s.id" type="button" :data-nav="s.id" :aria-current="active.id === s.id ? 'page' : undefined" :class="navClass(s.id)" @click="section = s.id">
            {{ s.title }}
          </button>
        </nav>

        <div class="min-h-0 min-w-0 flex-1 overflow-y-auto">
          <div class="max-w-xl px-4 py-5 sm:px-6">
            <h2 class="text-base font-semibold tracking-tight">{{ active.title }}</h2>
            <p class="mt-0.5 mb-5 text-sm text-muted-foreground">{{ active.blurb }}</p>
            <component :is="active.view" :key="active.id" />
          </div>
        </div>
      </div>
    </DialogContent>
  </Dialog>
</template>
