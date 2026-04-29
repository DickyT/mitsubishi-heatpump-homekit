/* Kiri Bridge — minimal bootstrap. Pulls the chunked CSS + JS, then runs the SPA. */

(function(){
  const manifest = __WEB_ASSET_MANIFEST__;
  const version = manifest.version || "dev";
  const app = document.getElementById("app");

  function withVersion(path){
    return path + (path.indexOf("?") === -1 ? "?" : "&") + "v=" + encodeURIComponent(version);
  }
  async function fetchText(path){
    const r = await fetch(withVersion(path), { cache: "force-cache" });
    if (!r.ok) throw new Error(path + " failed with HTTP " + r.status);
    return r.text();
  }
  async function concat(paths){
    let out = "";
    for (const p of paths) out += await fetchText(p);
    return out;
  }

  async function boot(){
    if (!app) throw new Error("Missing #app");
    app.textContent = "Loading…";

    const css = await concat(manifest.css);
    const style = document.createElement("style");
    style.textContent = css;
    document.head.appendChild(style);

    const js = await concat(manifest.js);
    const script = document.createElement("script");
    script.textContent = js + "\n//# sourceURL=/assets/app.bundle.js";
    document.body.appendChild(script);
  }

  boot().catch(function(e){
    if (app) {
      app.innerHTML = "<main><h1>WebUI load failed</h1><pre></pre></main>";
      const pre = app.querySelector("pre");
      if (pre) pre.textContent = String(e && e.stack ? e.stack : e);
    }
  });
})();
