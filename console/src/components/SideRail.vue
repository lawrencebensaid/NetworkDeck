<script setup lang="ts">
/*
 * The rail: a slim column of icon buttons down the left, from under the top bar to the bottom. At the
 * top the panels beside the deck: the Stream Deck, the Network Deck (the dock's hardware) and Matter. At
 * the bottom the dialogs: the log, the settings, the connectors (Matter, the REST API; MCP later) and the
 * account.
 * On a phone it runs along the bottom.
 */
import { CircleUser, Cpu, KeyRound, LogOut, Plug, ScrollText, Settings } from "@lucide/vue"

import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import { PANELS, type PanelId } from "../panels/panels.ts"
import { signOut, store } from "../session/store.ts"
import DeckIcon from "./DeckIcon.vue"
import MatterIcon from "./MatterIcon.vue"
import RailButton from "./RailButton.vue"
import TipWrap from "./TipWrap.vue"

defineProps<{ panel: PanelId | null }>()
defineEmits<{ panel: [id: PanelId]; settings: [section?: string]; connectors: []; logs: [] }>()

const icons = { deck: DeckIcon, dock: Cpu, matter: MatterIcon }
const order: PanelId[] = ["deck", "dock", "matter"]
</script>

<template>
  <nav
    aria-label="Panels"
    class="chrome flex shrink-0 items-center gap-1 border-t px-2 md:w-[52px] md:flex-col md:border-t-0 md:border-r md:px-0 md:py-2"
    data-testid="rail"
  >
    <TipWrap v-for="id in order" :key="id" :tip="PANELS[id].title" side="right">
      <RailButton
        :aria-label="PANELS[id].title"
        :aria-pressed="panel === id"
        :data-testid="`rail-${id}`"
        @mousedown.prevent
        @click="$emit('panel', id)"
      >
        <component :is="icons[id]" />
      </RailButton>
    </TipWrap>

    <div class="min-h-0 flex-1" />
    <div class="mx-1 h-6 w-px bg-border md:mx-auto md:my-1 md:h-px md:w-6" aria-hidden="true" />

    <TipWrap tip="Logs" side="right">
      <RailButton aria-label="Logs" data-testid="logs-button" @mousedown.prevent @click="$emit('logs')">
        <ScrollText />
      </RailButton>
    </TipWrap>
    <TipWrap tip="Settings" side="right">
      <RailButton aria-label="Settings" data-testid="settings-button" @click="$emit('settings')">
        <Settings />
      </RailButton>
    </TipWrap>
    <TipWrap tip="Connectors" side="right">
      <RailButton aria-label="Connectors" data-testid="connectors-button" @click="$emit('connectors')">
        <Plug />
      </RailButton>
    </TipWrap>
    <TipWrap tip="Account" side="right">
      <DropdownMenu>
        <DropdownMenuTrigger as-child>
          <RailButton aria-label="Account" data-testid="account-button">
            <CircleUser />
          </RailButton>
        </DropdownMenuTrigger>
        <DropdownMenuContent side="right" align="end" :side-offset="8" class="w-56">
          <DropdownMenuLabel class="flex flex-col gap-0.5 font-normal">
            <span class="text-sm font-medium text-foreground">Administrator</span>
            <span class="text-xs text-muted-foreground">{{ store.viaHotspot ? "Signed in over the hotspot" : "Signed in with the dock's password" }}</span>
          </DropdownMenuLabel>
          <DropdownMenuSeparator />
          <DropdownMenuItem @select="$emit('settings', 'users')"><KeyRound /> Change password</DropdownMenuItem>
          <DropdownMenuItem data-testid="sign-out" @select="signOut"><LogOut /> Sign out</DropdownMenuItem>
        </DropdownMenuContent>
      </DropdownMenu>
    </TipWrap>
  </nav>
</template>
