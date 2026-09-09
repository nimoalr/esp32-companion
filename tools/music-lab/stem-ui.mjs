import {STEM_NAMES,validateReference,paintReference} from './stem-reference.mjs';
export function createStems({getWav,onEdit,notice,onAudio,onSeek,onSelect,redraw}){
 const $=id=>document.getElementById(id),canvas=$('stem-timeline');
 let track=null,map=null,reference=null,job=null,captureBusy=false,available=false,cached=false,solo=null,view=0,span=1,drag=null,generation=0,feedback='';
 function report(message){feedback=message;notice(message);}
 async function api(path,options){const response=await fetch(path,options);if(!response.headers.get('Content-Type')?.includes('application/json'))throw new Error('Restart the updated Music Lab server after saving your recordings to enable local stem analysis.');const value=await response.json();if(!response.ok)throw new Error(value.error||'Local analysis request failed');return value;}
 function controls(){
  $('stem-analyze').disabled=captureBusy||!!job||!track||!available;
  $('stem-analyze').hidden=!!reference&&cached&&!job;
  $('stem-analyze').textContent=reference?'Restore stem audio':'Analyze stems';
  $('stem-forget').hidden=!cached||!!job;
  $('stem-cancel').hidden=!job;$('stem-cancel').disabled=!!job&&!job.id;
  $('stem-progress').hidden=!job;
  $('stem-status').textContent=job?`${job.target.track}: ${job.stage}`:captureBusy?'Disconnect USB to analyze; your recording and labels remain here.':feedback||(!available?'Local analysis needs the updated server and setup-ml.sh.':reference?(cached?'Stems ready · choose a source to listen':'Level curves saved · analyze again to recover cached audio'):'Separate this recording locally on the Mac. First use downloads model weights.');
  $('stem-lanes').hidden=!reference;
  for(const button of document.querySelectorAll('[data-stem]')){button.disabled=button.dataset.stem!=='original'&&!cached;button.setAttribute('aria-pressed',String((solo||'original')===button.dataset.stem));}
 }
 function choose(name){if(name!=='original'&&!cached)return;solo=name==='original'?null:name;onAudio(solo?`/api/stems/${reference.source.wavSHA256}/${solo.toLowerCase()}.wav`:null,solo);controls();}
 $('stem-solo').onclick=e=>{if(e.target.dataset.stem)choose(e.target.dataset.stem);};
 async function poll(active){
  while(job===active){
   let state;
   try{state=await api(`/api/stems/${active.id}`);}catch(e){notice(e.message);active.stage='Connection interrupted · retrying local worker';controls();await new Promise(resolve=>setTimeout(resolve,2000));continue;}
   if(job!==active)return;
   if(state.state==='ready'){
    try{validateReference(state.reference,active.frames,active.id);active.target.stemReference=state.reference;onEdit();
     if(track===active.target){reference=state.reference;cached=true;redraw();}}
    catch(e){report(e.message);}
    job=null;controls();return;
   }
   if(state.state!=='running'){report(active.cancelled?'Stem analysis cancelled. Your recording and annotations are unchanged.':state.error||'Analysis was interrupted. Try again when ready.');job=null;controls();return;}
   active.stage=active.cancelled?'Cancelling analysis…':state.stage||'Separating audio';$('stem-progress').value=state.progress||0;controls();
   await new Promise(resolve=>setTimeout(resolve,1000));
  }
 }
 $('stem-analyze').onclick=async()=>{
  if(job||captureBusy||!track)return;
  const active={target:track,frames:map.frames*256,stage:'Preparing microphone audio',id:null};feedback='';job=active;controls();
  try{
   const input=getWav();active.stage='Sending to local analysis worker';controls();
   const state=await api('/api/stems',{method:'POST',headers:{'Content-Type':'audio/wav'},body:input});
   active.id=state.id;await poll(active);
  }catch(e){report(e.message);if(job===active)job=null;controls();}
 };
 $('stem-cancel').onclick=async()=>{if(!job?.id)return;const active=job;try{await api(`/api/stems/${active.id}`,{method:'DELETE'});}catch(e){notice(e.message);return;}if(job===active){active.cancelled=true;active.stage='Cancelling analysis…';controls();}};
 $('stem-forget').onclick=async()=>{if(!reference||job)return;try{await api(`/api/stems/${reference.source.wavSHA256}`,{method:'DELETE'});choose('original');cached=false;controls();notice('Cached stem audio removed. The level curves and your labels are still saved in this session.');}catch(e){notice(e.message);}};
 const position=e=>{const r=canvas.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width*1100,y:(e.clientY-r.top)/r.height*200};};
 const time=x=>Math.max(0,Math.min(map.duration,view+(x-112)/988*span));
 canvas.onpointerdown=e=>{if(!reference)return;const p=position(e);if(p.x<112){choose(STEM_NAMES[Math.min(3,Math.floor(p.y/50))]);return;}drag={x:p.x,start:time(p.x),moved:false};canvas.setPointerCapture(e.pointerId);};
 canvas.onpointermove=e=>{if(!drag)return;const p=position(e);if(Math.abs(p.x-drag.x)>3)drag.moved=true;if(drag.moved)onSelect(Math.min(drag.start,time(p.x)),Math.max(drag.start,time(p.x)));};
 canvas.onpointerup=e=>{if(!drag)return;if(!drag.moved)onSeek(time(position(e).x));drag=null;};canvas.onpointercancel=()=>{drag=null;};
 return {
  get busy(){return !!job;},
  setCaptureBusy(value){captureBusy=value;controls();},
  resetSolo(){solo=null;cached=false;controls();},
  clear(){generation++;track=map=reference=null;cached=false;solo=null;drag=null;feedback='';controls();},
  async open(t,m){const token=++generation;track=t;map=m;reference=null;cached=false;solo=null;available=false;feedback='';controls();
   try{
    if(t.stemReference){const bytes=getWav();const hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');if(token!==generation)return;
     reference=validateReference(t.stemReference,m.frames*256,hash);redraw();
    }
    const capabilities=await api('/api/stems');if(token!==generation)return;available=capabilities.available;
    if(reference){const state=await api(`/api/stems/${reference.source.wavSHA256}`);if(token!==generation)return;cached=state.state==='ready';}
   }catch(e){if(token===generation)report(e.message);}
   if(token===generation){controls();redraw();}
  },
  draw(start,duration,playhead,selection){view=start;span=duration;if(reference)paintReference(canvas.getContext('2d'),reference,{view,span,playhead,selection,gaps:map.gaps});},
 };
}
