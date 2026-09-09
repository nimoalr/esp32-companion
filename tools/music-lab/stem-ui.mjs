import {STEM_NAMES,validateReference,paintReference} from './stem-reference.mjs?v=portable4';
import {AUDIO_STEMS,validateFLAC,validateStemAudio} from './stem-audio.mjs?v=portable4';
export function createStems({getWav,onEdit,notice,onAudio,onSeek,onSelect,redraw}){
 const $=id=>document.getElementById(id),canvas=$('stem-timeline');
 let track=null,map=null,reference=null,job=null,captureBusy=false,available=false,embedded=false,solo=null,view=0,span=1,drag=null,generation=0,feedback='';
 const urls=new Map();
 function report(message){feedback=message;notice(message);}
 async function api(path,options){const response=await fetch(path,options);if(!response.headers.get('Content-Type')?.includes('application/json'))throw new Error('Restart the updated Music Lab server after saving your recordings to enable local stem analysis.');const value=await response.json();if(!response.ok)throw new Error(value.error||'Local analysis request failed');return value;}
 function controls(){
  $('stem-analyze').disabled=captureBusy||!!job||!track||!available;
  $('stem-analyze').hidden=embedded&&!job;
  $('stem-analyze').textContent=reference?'Generate stem audio':'Analyze stems';
  $('stem-cancel').hidden=!job;$('stem-cancel').disabled=!!job&&(!job.id||job.attached);
  $('stem-progress').hidden=!job;
  $('stem-status').textContent=job?`${job.target.track}: ${job.stage}`:feedback||(embedded?'Stems included in this session · save the notebook to keep everything together':captureBusy?'Disconnect USB to analyze; your recording and labels remain here.':!available?'Local analysis needs the updated server and setup-ml.sh.':reference?'Level curves saved · generate audio to include it in the notebook':'Separate locally. Stem audio will be included in your saved notebook; no persistent audio cache.');
  $('stem-lanes').hidden=!reference;
  for(const button of document.querySelectorAll('[data-stem]')){button.disabled=button.dataset.stem!=='original'&&!embedded;button.setAttribute('aria-pressed',String((solo||'original')===button.dataset.stem));}
 }
 function choose(name){if(name!=='original'&&!embedded)return;solo=name==='original'?null:name;
  if(solo&&!urls.has(solo))urls.set(solo,URL.createObjectURL(new Blob([track.stemAudio.stems[solo]],{type:'audio/flac'})));
  onAudio(solo?urls.get(solo):null,solo);controls();
 }
 $('stem-solo').onclick=e=>{if(e.target.dataset.stem)choose(e.target.dataset.stem);};
 async function receive(active,state){
  validateReference(state.reference,active.frames,active.id);
  const stems={};
  for(const name of AUDIO_STEMS){
   if(active.cancelled)throw new Error('Analysis cancelled');
   active.stage=`Receiving ${name.toLowerCase()} into the notebook`;controls();
   const response=await fetch(`/api/stems/${active.id}/${name.toLowerCase()}.flac`);
   if(!response.ok)throw new Error('Could not receive stem audio. Your original recording is unchanged; try again.');
   if(Number(response.headers.get('Content-Length'))>128000000)throw new Error('Stem audio exceeds the size limit');
   stems[name]=validateFLAC(new Uint8Array(await response.arrayBuffer()),active.frames);
  }
  if(active.cancelled)throw new Error('Analysis cancelled');
  const audio=validateStemAudio({sourceSHA256:active.id,stems},state.reference);
  active.target.stemReference=state.reference;active.target.stemAudio=audio;active.attached=true;onEdit();
  if(track===active.target){reference=state.reference;embedded=true;redraw();}
  active.stage='Removing temporary server files';controls();
  try{await api(`/api/stems/${active.id}`,{method:'DELETE'});}
  catch(e){report('Stems are in this session. Temporary server cleanup could not be confirmed; abandoned jobs expire after 10 minutes.');}
 }
 async function poll(active){
  while(job===active){
   let state;
   try{state=await api(`/api/stems/${active.id}`);}catch(e){notice(e.message);active.stage='Connection interrupted · retrying local worker';controls();await new Promise(resolve=>setTimeout(resolve,2000));continue;}
   if(job!==active)return;
   if(state.state==='ready'){
    try{await receive(active,state);}catch(e){report(active.cancelled?'Stem analysis cancelled. Your recording and annotations are unchanged.':e.message);try{await api(`/api/stems/${active.id}`,{method:'DELETE'});}catch{}}
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
  try{const input=getWav();active.stage='Sending to local analysis worker';controls();const state=await api('/api/stems',{method:'POST',headers:{'Content-Type':'audio/wav'},body:input});active.id=state.id;await poll(active);}
  catch(e){report(e.message);if(job===active)job=null;controls();}
 };
 $('stem-cancel').onclick=async()=>{if(!job?.id||job.attached)return;const active=job;active.cancelled=true;active.stage='Cancelling analysis…';controls();try{await api(`/api/stems/${active.id}`,{method:'DELETE'});}catch(e){notice(e.message);}};
 const position=e=>{const r=canvas.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width*1100,y:(e.clientY-r.top)/r.height*200};};
 const time=x=>Math.max(0,Math.min(map.duration,view+(x-112)/988*span));
 canvas.onpointerdown=e=>{if(!reference)return;const p=position(e);if(p.x<112){choose(STEM_NAMES[Math.min(3,Math.floor(p.y/50))]);return;}drag={x:p.x,start:time(p.x),moved:false};canvas.setPointerCapture(e.pointerId);};
 canvas.onpointermove=e=>{if(!drag)return;const p=position(e);if(Math.abs(p.x-drag.x)>3)drag.moved=true;if(drag.moved)onSelect(Math.min(drag.start,time(p.x)),Math.max(drag.start,time(p.x)));};
 canvas.onpointerup=e=>{if(!drag)return;if(!drag.moved)onSeek(time(position(e).x));drag=null;};canvas.onpointercancel=()=>{drag=null;};
 return {
  get busy(){return !!job;},setCaptureBusy(value){captureBusy=value;controls();},
  resetSolo(){solo=null;controls();},
  clear(){generation++;for(const url of urls.values())URL.revokeObjectURL(url);urls.clear();track=map=reference=null;embedded=false;solo=null;drag=null;feedback='';controls();},
  async open(t,m){const token=++generation;track=t;map=m;reference=null;embedded=false;solo=null;available=false;feedback='';controls();
   try{
    if(t.stemReference){const bytes=getWav(),hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),x=>x.toString(16).padStart(2,'0')).join('');if(token!==generation)return;
     reference=validateReference(t.stemReference,m.frames*256,hash);
     if(t.stemAudio){validateStemAudio(t.stemAudio,reference);embedded=true;}redraw();controls();
    }
    const capabilities=await api('/api/stems');if(token!==generation)return;available=capabilities.available;
   }catch(e){if(token===generation&&!embedded)report(e.message);}
   if(token===generation){controls();redraw();}
  },
  draw(start,duration,playhead,selection){view=start;span=duration;if(reference)paintReference(canvas.getContext('2d'),reference,{view,span,playhead,selection,gaps:map.gaps});},
 };
}
