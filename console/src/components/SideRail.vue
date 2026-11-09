<script setup lang="ts">
/* The rail: the panels at the top, signing out at the bottom. On a phone it runs along the bottom. */
import { Activity, Download, KeyRound, LogOut, Network, Wifi } from "@lucide/vue"

import { PANELS, type PanelId } from "../panels/panels.ts"
import { signOut } from "../session/store.ts"
import RailButton from "./RailButton.vue"
import TipWrap from "./TipWrap.vue"

defineProps<{ panel: PanelId | null }>()
defineEmits<{ panel: [id: PanelId] }>()

const icons = { network: Network, wifi: Wifi, firmware: Download, security: KeyRound, diagnostics: Activity }
const order: PanelId[] = ["network", "wifi", "firmware", "security", "diagnostics"]
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
    <TipWrap tip="Sign out" side="right">
      <RailButton aria-label="Sign out" data-testid="sign-out" @click="signOut">
        <LogOut />
      </RailButton>
    </TipWrap>
  </nav>
</template>
