/* The panels the rail opens beside the deck. Settings and connectors are dialogs (see ConsoleView). */
export type PanelId = "deck" | "matter";

export const PANELS: Record<PanelId, { title: string }> = {
  deck: { title: "Stream Deck" },
  matter: { title: "Matter" },
};

/* Wide enough for a panel beside the deck. */
export const WIDE_QUERY = "(min-width: 768px)";
