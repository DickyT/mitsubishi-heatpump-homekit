const $=id=>document.getElementById(id);
document.getElementById('t4').classList.add('active');
function norm(v){v=(v||'/').trim();if(!v.startsWith('/'))v='/'+v;if(v.length>1&&v.endsWith('/'))v=v.slice(0,-1);return v||'/';}
function full(v){v=(v||'').trim();if(!v)return '';if(v.startsWith('/'))return norm(v);const d=norm($('dir').value);return d==='/'?'/'+v:d+'/'+v;}
async function loadFiles(){
  const dir=norm($('dir').value);$('dir').value=dir;
  try{
    const r=await fetch('/api/files?dir='+encodeURIComponent(dir));const j=await r.json();
    $('fs-info').textContent=JSON.stringify(j.info||{},null,2);
    const list=$('file-list');list.innerHTML='';
    if(!j.ok){list.textContent=j.error||'读取失败';return;}
    if(j.truncated){
      const note=document.createElement('div');
      note.className='notice';
      note.textContent='只显示前 '+j.returnedItems+' 项，共 '+j.totalItems+' 项。日志太多时请先到“日志”页下载/清理，或输入更具体的目录。';
      list.appendChild(note);
    }
    if(!j.items.length){list.textContent='这个目录是空的。';return;}
    j.items.forEach(item=>{
      const row=document.createElement('div');row.className='listrow';
      const meta=document.createElement('div');meta.className='listmeta';
      meta.textContent=(item.type==='dir'?'[目录] ':'[文件] ')+item.name;
      const small=document.createElement('small');small.textContent=item.type==='file'?item.size+' bytes':'文件夹';meta.appendChild(small);
      const actions=document.createElement('div');actions.className='actions';
      const open=document.createElement('button');open.className='btn-secondary';open.textContent=item.type==='dir'?'打开':'下载';
      open.onclick=()=>{if(item.type==='dir'){$('dir').value=item.name;loadFiles();}else{location.href='/api/files/download?path='+encodeURIComponent(item.name);}};
      const del=document.createElement('button');del.className='btn-danger';del.textContent='删除';del.onclick=()=>deletePath(item.name);
      actions.appendChild(open);actions.appendChild(del);row.appendChild(meta);row.appendChild(actions);list.appendChild(row);
    });
  }catch(e){$('file-list').textContent='读取失败: '+e;}
}
async function post(url){const r=await fetch(url,{method:'POST'});const j=await r.json();$('file-out').textContent=JSON.stringify(j,null,2);loadFiles();}
function deletePath(path){if(confirm('删除 '+path+' ?'))post('/api/files/delete?path='+encodeURIComponent(path));}
function createFile(){const p=full($('new-file').value);if(!p){$('file-out').textContent='请输入文件路径';return;}post('/api/files/create-file?path='+encodeURIComponent(p));}
function createDir(){const p=full($('new-dir').value);if(!p){$('file-out').textContent='请输入文件夹路径';return;}post('/api/files/create-dir?path='+encodeURIComponent(p));}
function upDir(){let d=norm($('dir').value);if(d!=='/'){$('dir').value=d.substring(0,d.lastIndexOf('/'))||'/';loadFiles();}}
async function uploadFile(){
  const f=$('upload-file').files[0];if(!f){$('file-out').textContent='请选择文件';return;}
  const path=full(f.name);$('file-out').textContent='上传中...';
  try{const r=await fetch('/api/files/upload?path='+encodeURIComponent(path),{method:'POST',body:await f.arrayBuffer()});const j=await r.json();$('file-out').textContent=JSON.stringify(j,null,2);loadFiles();}
  catch(e){$('file-out').textContent='上传失败: '+e;}
}
loadFiles();
