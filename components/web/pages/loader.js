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
    app.innerHTML=await fetchSeries(config.body);

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
