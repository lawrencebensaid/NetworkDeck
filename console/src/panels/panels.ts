/* The panels the rail opens beside the deck. Settings and integrations are dialogs (see ConsoleView). */
export type PanelId = "deck" | "matter" | "diagnostics";

export const PANELS: Record<PanelId, { title: string }> = {
  deck: { title: "Stream Deck" },
  matter: { title: "Matter" },
  diagnostics: { title: "Diagnostics" },
};

/* Wide enough for a panel beside the deck. */
export const WIDE_QUERY = "(min-width: 768px)";
