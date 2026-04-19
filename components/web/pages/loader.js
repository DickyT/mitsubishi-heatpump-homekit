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

  async function boot(){
    const page=document.body.dataset.page||'root';
    const config=manifest.pages[page];
    if(!app||!config){
      throw new Error('Unknown WebUI page: '+page);
    }

    app.textContent='加载样式...';
    const css=await fetchSeries(manifest.css);
    const style=document.createElement('style');
    style.textContent=css;
    document.head.appendChild(style);

    app.textContent='加载页面...';
    app.innerHTML=await fetchSeries(config.body);

    app.setAttribute('data-ready','1');
    runPageScript(await fetchSeries(config.js));
  }

  boot().catch(function(error){
    if(app){
      app.innerHTML='<main><h1>WebUI 加载失败</h1><pre></pre></main>';
      const pre=app.querySelector('pre');
      if(pre)pre.textContent=String(error&&error.stack?error.stack:error);
    }
  });
})();
