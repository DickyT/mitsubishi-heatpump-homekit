/****************************************************************************
 * Kiri Bridge
 * CN105 HomeKit controller for Mitsubishi heat pumps
 * https://kiri.dkt.moe
 * https://github.com/DickyT/kiri-homekit
 *
 * Copyright (c) 2026
 * All Rights Reserved.
 * Licensed under terms of the GPL-3.0 License.
 ****************************************************************************/

(function(){
  const manifest=__WEB_ASSET_MANIFEST__;
  const version=manifest.version||'dev';
  const app=document.getElementById('app');

  function withVersion(path){
    return path+(path.indexOf('?')===-1?'?':'&')+'v='+encodeURIComponent(version);
  }

  async function fetchText(path){
    const response=await fetch(withVersion(path),{cache:'force-cache'});
    if(!response.ok){
      throw new Error(path+' failed with HTTP '+response.status);
    }
    return response.text();
  }

  async function fetchSeries(paths){
    let output='';
    for(const path of paths){
      output+=await fetchText(path);
    }
    return output;
  }

  function runPageScript(source){
    const script=document.createElement('script');
    script.textContent=source+'\n//# sourceURL=/assets/'+document.body.dataset.page+'.bundle.js';
    document.body.appendChild(script);
  }

  function pageTitle(page){
    if(page==='logs') return 'Logs';
    if(page==='admin') return 'Admin';
    return 'Control';
  }

  function navClass(page,name){
    return page===name?' class="active"':'';
  }

  function renderChrome(page){
    return '<nav class="tabs">'+
      '<a href="/" id="t1"'+navClass(page,'root')+'><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 9l9-7 9 7v11a2 2 0 01-2 2H5a2 2 0 01-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>Control</a>'+
      '<a href="/logs" id="t3"'+navClass(page,'logs')+'><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 19h16"/><path d="M4 15h16"/><path d="M4 11h16"/><path d="M4 7h10"/></svg>Logs</a>'+
      '<a href="/admin" id="t5"'+navClass(page,'admin')+'><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 00.33 1.82l.06.06a2 2 0 010 2.83 2 2 0 01-2.83 0l-.06-.06a1.65 1.65 0 00-1.82-.33 1.65 1.65 0 00-1 1.51V21a2 2 0 01-4 0v-.09A1.65 1.65 0 009 19.4a1.65 1.65 0 00-1.82.33l-.06.06a2 2 0 01-2.83-2.83l.06-.06A1.65 1.65 0 004.68 15a1.65 1.65 0 00-1.51-1H3a2 2 0 010-4h.09A1.65 1.65 0 004.6 9a1.65 1.65 0 00-.33-1.82l-.06-.06a2 2 0 012.83-2.83l.06.06A1.65 1.65 0 009 4.68a1.65 1.65 0 001-1.51V3a2 2 0 014 0v.09a1.65 1.65 0 001 1.51 1.65 1.65 0 001.82-.33l.06-.06a2 2 0 012.83 2.83l-.06.06A1.65 1.65 0 0019.4 9a1.65 1.65 0 001.51 1H21a2 2 0 010 4h-.09a1.65 1.65 0 00-1.51 1z"/></svg>Admin</a>'+
      '</nav>'+
      '<div class="brand-strip"><div><strong>Kiri Bridge</strong><span>CN105 HomeKit controller for Mitsubishi heat pumps</span></div><div class="brand-links"><a href="https://kiri.dkt.moe" target="_blank" rel="noopener noreferrer">Website</a></div></div>';
  }

  function renderFooter(){
    return '<footer class="site-footer"><div><strong>Kiri Bridge</strong> © 2026. All Rights Reserved.</div><div><a href="https://kiri.dkt.moe" target="_blank" rel="noopener noreferrer">kiri.dkt.moe</a><span>Firmware <b id="footer-version">--</b></span></div></footer>';
  }

  async function populateFooter(){
    const version=document.getElementById('footer-version');
    const page=document.body.dataset.page||'root';
    try{
      const response=await fetch('/api/status',{cache:'no-store'});
      if(!response.ok) throw new Error('HTTP '+response.status);
      const status=await response.json();
      const device=(status.config&&status.config.device_name)||status.device||'Kiri Bridge';
      document.title=pageTitle(page)+' | '+device+' | Kiri Bridge';
      if(version) version.textContent=status.version||'--';
    }catch(error){
      document.title=pageTitle(page)+' | Kiri Bridge | Kiri Bridge';
      if(version) version.textContent='--';
    }
  }

  async function boot(){
    const page=document.body.dataset.page||'root';
    const config=manifest.pages[page];
    if(!app||!config){
      throw new Error('Unknown WebUI page: '+page);
    }

    app.textContent='Loading styles...';
    const css=await fetchSeries(manifest.css);
    const style=document.createElement('style');
    style.textContent=css;
    document.head.appendChild(style);

    app.textContent='Loading page...';
    app.innerHTML=renderChrome(page)+await fetchSeries(config.body)+renderFooter();

    app.setAttribute('data-ready','1');
    populateFooter();
    runPageScript(await fetchSeries(config.js));
  }

  boot().catch(function(error){
    if(app){
      app.innerHTML='<main><h1>WebUI load failed</h1><pre></pre></main>';
      const pre=app.querySelector('pre');
      if(pre)pre.textContent=String(error&&error.stack?error.stack:error);
    }
  });
})();
