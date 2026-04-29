// Kiri Bridge — minimal hash router.
// Three pages: control / logs / admin. Hash because no server-side routing.

import { signal, effect } from "@preact/signals";

export type PageId = "control" | "logs" | "admin";

const PAGE_BY_PATH: Record<string, PageId> = {
  "/": "control",
  "/logs": "logs",
  "/admin": "admin",
};

const PATH_BY_PAGE: Record<PageId, string> = {
  control: "/",
  logs: "/logs",
  admin: "/admin",
};

function readPage(): PageId {
  return PAGE_BY_PATH[location.pathname] ?? "control";
}

export const currentPage = signal<PageId>(readPage());

export function navigate(page: PageId): void {
  const path = PATH_BY_PAGE[page];
  if (location.pathname !== path) {
    history.pushState({}, "", path);
  }
  currentPage.value = page;
}

window.addEventListener("popstate", () => {
  currentPage.value = readPage();
});
