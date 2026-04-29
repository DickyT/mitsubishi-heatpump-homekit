// Kiri Bridge — SPA bootstrap. Mounts <App/> into #app and starts polling.

import { render } from "preact";
import type { JSX } from "preact";
import "./styles.css";
import { startPolling } from "./store";
import { currentPage } from "./router";
import { Tabs, SiteFooter } from "./layout";
import { ControlPage } from "./pages/Control";
import { LogsPage } from "./pages/Logs";
import { AdminPage } from "./pages/Admin";

function App(): JSX.Element {
  const page = currentPage.value;
  return (
    <>
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
  render(<App />, root);
  startPolling(5000);
}
