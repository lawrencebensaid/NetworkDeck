<script setup lang="ts">
/* The console: top bar, rail, a panel beside the deck when one is open, the deck, the status line. */
import { ref } from "vue"

import DeckStage from "../components/DeckStage.vue"
import SidePanel from "../components/SidePanel.vue"
import SideRail from "../components/SideRail.vue"
import StatusBar from "../components/StatusBar.vue"
import TopBar from "../components/TopBar.vue"
import DiagnosticsPanel from "../panels/DiagnosticsPanel.vue"
import FirmwarePanel from "../panels/FirmwarePanel.vue"
import NetworkPanel from "../panels/NetworkPanel.vue"
import { PANELS, type PanelId } from "../panels/panels.ts"
import SecurityPanel from "../panels/SecurityPanel.vue"
import WifiPanel from "../panels/WifiPanel.vue"

const panel = ref<PanelId | null>(null)
const views = { network: NetworkPanel, wifi: WifiPanel, firmware: FirmwarePanel, security: SecurityPanel, diagnostics: DiagnosticsPanel }

function toggle(id: PanelId) {
  panel.value = panel.value === id ? null : id
}
</script>

<template>
  <div class="flex h-dvh flex-col bg-background text-foreground">
    <TopBar />
    <div class="flex min-h-0 flex-1 flex-col-reverse md:flex-row">
      <SideRail :panel="panel" @panel="toggle" />
      <div class="flex min-h-0 min-w-0 flex-1">
        <SidePanel :open="panel !== null" :title="panel ? PANELS[panel].title : ''" @close="panel = null">
          <component :is="views[panel]" v-if="panel" />
        </SidePanel>
        <DeckStage />
      </div>
    </div>
    <StatusBar />
  </div>
</template>
