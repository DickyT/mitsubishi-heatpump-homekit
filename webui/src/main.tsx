// Kiri Bridge — SPA bootstrap. Mounts <App/> into #app and starts polling.

import { render } from "preact";
import type { JSX } from "preact";
import { useEffect } from "preact/hooks";
import "./styles.css";
import { deviceName, initPolling } from "./store";
import { currentPage } from "./router";
import { AppTopBar, Tabs, SiteFooter } from "./layout";
import { ControlPage } from "./pages/Control";
import { LogsPage } from "./pages/Logs";
import { AdminPage } from "./pages/Admin";

const PAGE_TITLES = {
  control: "Control",
  logs: "Logs",
  admin: "Admin",
} as const;

function App(): JSX.Element {
  const page = currentPage.value;
  const pageTitle = PAGE_TITLES[page];
  const name = deviceName.value;

  useEffect(() => {
    document.title = `${pageTitle} | ${name} | Kiri Bridge`;
  }, [pageTitle, name]);

  return (
    <>
      <AppTopBar />
      <Tabs />
      {page === "control" && <ControlPage />}
      {page === "logs" && <LogsPage />}
      {page === "admin" && <AdminPage />}
      <SiteFooter />
    </>
  );
}

const root = document.getElementById("app");
if (root) {
  root.textContent = "";
  render(<App />, root);
  initPolling();
}
