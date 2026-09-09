import {parseLine,decode,pack,unpack,summarize,summarizeCorpus,BAND_EDGES_HZ} from './trace.mjs';
import {createReplay} from './replay-ui.mjs';
const $=id=>document.getElementById(id);
let lastPerf=null,collectedBytes=0;
let deviceConfig=null,stopAck=null,connecting=false,editing=null;
let heartbeat=null,writeChain=Promise.resolve(),deviceLost=0;
function send(command){const target=port;writeChain=writeChain.catch(()=>{}).then(async()=>{if(!target?.writable)throw new Error("USB is disconnected");const w=target.writable.getWriter();try{await w.write(new TextEncoder().encode(command+"\n"));}finally{w.releaseLock();}});return writeChain;}
let port,reader,reading=false,demoTimer,latest,lastReceived=0,history=[],historyPackets=[],records=[],run=null,started=0,unsaved=false;
let meta={format:2,created:new Date().toISOString(),demo:false,tracks:[],transportEvents:[]};
const fields=['track','kind','style','volume','setup','notes'];
function notice(s){$('notice').textContent=s;}
const replay=createReplay(()=>{unsaved=true;},notice);
function status(s){$('status').textContent=s;}
function controls(){
 const fresh=performance.now()-lastReceived<1500,analysing=replay.isAnalyzing();
 replay.setCaptureBusy(!!port||!!run||connecting);
 $('start').disabled=!!run||!!editing||!latest||!fresh;$('save-labels').hidden=!editing;
 $('stop').disabled=!run;$('download').disabled=!!run||!records.length;
 $('capture-stereo').disabled=connecting||!!port;
 $('connect').disabled=analysing||connecting||!!port||!!demoTimer;$('disconnect').disabled=!port;
 $('demo').disabled=!!port||!!run||!!demoTimer||records.length>0;
 $('import').disabled=analysing||!!run||!!port||!!demoTimer;$('new').disabled=analysing||!!run;
 fields.forEach(id=>$(id).disabled=!!run);
 document.querySelectorAll('[data-mark]').forEach(b=>b.disabled=!run);
}
function receive(p){
 latest=decode(p);lastReceived=performance.now();history.push(latest);historyPackets.push(p);if(history.length>256){history.shift();historyPackets.shift();}
 if(run){records.push(p);collectedBytes+=p.length;unsaved=true;if((records.length>=500000||collectedBytes>=128000000)){endRun();notice('Session size limit reached. Download before starting a new session.');}}
}
function line(s){
 try{for(const p of parseLine(s))receive(p);}catch(e){notice(e.message);meta.transportEvents.push({type:'invalid_packet',at:new Date().toISOString()});}
 const perf=s.match(/I \((\d+)\).*?: (\d+) fps \| raster (\d+) us avg, (\d+) us max \| push (\d+) us/);
 if(perf){const cpu=s.match(/audio cpu (\d+) us/);lastPerf={ms:+perf[1],fps:+perf[2],rasterAvgUs:+perf[3],rasterMaxUs:+perf[4],pushUs:+perf[5],audioUs:cpu?+cpu[1]:null};if(run)(run.performance??=[]).push(lastPerf);}
 const loss=s.match(/MC_LOST:(\d+)/);if(loss){meta.transportEvents.push({type:'device_queue_overflow',frames:+loss[1],deviceMs:latest?.ms});notice(`Device dropped ${loss[1]} frames; marked in the session.`);}
 const cfg=s.match(/MC_CONFIG:(.*?)(?:\x1b\[[0-9;]*m)?$/);if(cfg)deviceConfig=cfg[1];
 const state=s.match(/MC_STATE:(recording|idle).*?frames=(\d+) lost=(\d+)/);
 if(state){deviceLost=+state[3];if(run)(run.captureHealth??=[]).push({deviceMs:latest?.ms,frames:+state[2],lost:deviceLost,offerMaxUs:Number(s.match(/offer_max_us=(\d+)/)?.[1])||null,writerStack:Number(s.match(/writer_stack=(\d+)/)?.[1])||null});}
 if(s.includes('MC_SESSION:'))meta.transportEvents.push({type:'device_session',message:s.replace(/\x1b\[[0-9;]*m/g,''),at:new Date().toISOString(),frame:records.length});
 if(s.includes('MC_SESSION:')&&s.includes('stop')){stopAck?.();if(run){mark('Device capture stopped');endRun();}}
 if(s.includes('MC_SESSION:'))status(s.replace(/\x1b\[[0-9;]*m/g,'').slice(s.indexOf('MC_SESSION:')));
}
$('connect').onclick=async()=>{
 if(connecting||port||replay.isAnalyzing())return;connecting=true;controls();
 try{
  if(meta.demo&&records.length)throw new Error('Choose New session before collecting device evidence.');
  if(!navigator.serial)throw new Error('Use Chrome or Edge on localhost for USB capture.');
  const known=await navigator.serial.getPorts();
  port=known.length===1?known[0]:await navigator.serial.requestPort();await port.open({baudRate:115200});
  lastPerf=null;deviceConfig=null;deviceLost=0;
  // Do not toggle DTR/RTS: opening the page should not reset the companion.
  reading=true;status('USB connected. Starting feature stream…');notice('');controls();
  const decoder=new TextDecoder();let buffer='';
  reader=port.readable.getReader();
  await send($('capture-stereo').checked?"MC_START_PCM":"MC_START");
  heartbeat=setInterval(()=>{if(reading)send('MC_PING').catch(e=>notice(e.message));},2000);
  while(reading){const {value,done}=await reader.read();if(done)break;
   buffer+=decoder.decode(value,{stream:true});let i;
   while((i=buffer.indexOf('\n'))>=0){line(buffer.slice(0,i).trim());buffer=buffer.slice(i+1);}
   if(buffer.length>8192){buffer='';notice('Oversized serial line discarded.');}
  }
 }catch(e){notice(e.message);}
 finally{
  clearInterval(heartbeat);heartbeat=null;
  if(run){mark('USB disconnected');endRun();}
  meta.transportEvents.push({type:'usb_disconnected',at:new Date().toISOString(),frame:records.length});
  reader?.releaseLock();reader=null;reading=false;connecting=false;
  if(port){try{await port.close();}catch{}port=null;}
  lastReceived=0;status('USB disconnected. Saved runs remain available. An unexpected disconnect leaves capture mode on; reconnect to resume, or tap the screen / hold PWR for 2 seconds to exit.');controls();
 }
};
$('disconnect').onclick=async()=>{
 try{const ack=new Promise(resolve=>{stopAck=resolve;setTimeout(resolve,1000);});await send('MC_STOP');await ack;}catch(e){notice(e.message);}
 stopAck=null;reading=false;await reader?.cancel();
};
$('start').onclick=()=>{
 if((records.length>=500000||collectedBytes>=128000000))return notice('Download this session and reload before recording more.');
 if(!latest||performance.now()-lastReceived>1500)return notice('Waiting for live device frames.');
 if(!$('track').value.trim())return notice('Give this run a track or reference name.');
 replay.clear();run={};fields.forEach(id=>run[id]=$(id).value.trim());
 run.deviceConfig=deviceConfig;run.captureMode=latest.power?'exclusive':'legacy';
 run.startFrame=records.length;run.startDeviceMs=latest.ms;run.deviceSession=latest.session;run.markers=[];
 run.startedUTC=new Date().toISOString();started=performance.now();notice('');controls();
};
function mark(label){if(!run)return;run.markers.push({label,frame:records.length,deviceMs:latest?.ms,deviceSession:latest?.session,hostElapsedMs:Math.round(performance.now()-started)});unsaved=true;renderEvents();}
function renderEvents(){const parent=$('events');parent.replaceChildren();for(const e of (run?.markers||[]).slice(-5)){const d=document.createElement('div');d.textContent=`${(e.hostElapsedMs/1000).toFixed(1)}s · ${e.label}`;parent.append(d);}}
$('markers').onclick=e=>{if(e.target.dataset.mark)mark(e.target.dataset.mark);};
function endRun(){
 if(!run)return;run.endFrame=records.length;run.summary=summarize(records.slice(run.startFrame,run.endFrame));
 run.hostDurationMs=Math.round(performance.now()-started);run.summary.unobservedHostMs=Math.max(0,run.hostDurationMs-run.summary.sampledSeconds*1000);meta.tracks.push(run);run=null;unsaved=true;renderTracks();controls();
}
$('stop').onclick=endRun;
$('replay-save-session').onclick=()=>$('download').click();
function renderTracks(){
 const el=$('tracks');el.replaceChildren();
 for(const t of meta.tracks){const details=document.createElement('details'),d=document.createElement('summary');const s=t.summary;
 d.textContent=`${t.track} · ${t.kind} · ${t.volume||'level unspecified'} · ${s.seconds.toFixed(1)}s · ${s.beats} beats · ${s.eligiblePercent.toFixed(0)}% rhythm-qualified · ${t.markers.length} markers · ${s.dancePercent.toFixed(0)}% ${t.captureMode==='exclusive'?'would dance':'dancing'}${s.missingMs>0?' · timing gaps: '+s.missingMs+' ms':''}`;details.append(d);
 for(const m of t.markers){const line=document.createElement('div');line.textContent=`${(m.hostElapsedMs/1000).toFixed(1)}s · ${m.label}`;details.append(line);}
 const edit=document.createElement('button');edit.textContent='Edit labels';edit.disabled=!!run;
 edit.onclick=()=>{if(run)return;editing=t;fields.forEach(id=>$(id).value=t[id]||'');controls();$('track').focus();};details.append(edit);
 const listen=document.createElement('button');listen.textContent='Replay & label';listen.disabled=!!run||!s.audioFrames;if(!s.audioFrames)listen.textContent='Features only · no audio';listen.onclick=()=>{if(!run)replay.open(t,records.slice(t.startFrame,t.endFrame));};details.append(listen);
 if(s.spectrum){const p=document.createElement('p');p.textContent='Frequency energy: '+s.spectrum.energyPercent.map((v,i)=>`${BAND_EDGES_HZ[i]}–${BAND_EDGES_HZ[i+1]} Hz ${v.toFixed(1)}%`).join(' · ');details.append(p);}
 const health=document.createElement('p');health.textContent=`${s.featureHz?.toFixed(2)??'multiple segments'} audio frames/s · ${s.segments} segment(s) · ${s.droppedFrames??'unknown'} sequence gaps · ${Math.max(0,t.hostDurationMs/1000-s.sampledSeconds).toFixed(1)}s host time without samples (includes boundaries)`;details.append(health);
 el.append(details);}
 renderSpectrum();
}
$('save-labels').onclick=()=>{if(!editing)return;fields.forEach(id=>editing[id]=$(id).value.trim());editing=null;unsaved=true;renderTracks();controls();};
$('new').onclick=()=>{
 if(unsaved)return notice('Download this session before starting a new one.');
 if(demoTimer){clearInterval(demoTimer);demoTimer=null;latest=null;lastReceived=0;fields.forEach(id=>$(id).value=id==='kind'?'music':'');}
 replay.clear();editing=null;collectedBytes=0;records=[];history=[];historyPackets=[];meta={format:2,created:new Date().toISOString(),demo:false,tracks:[],transportEvents:[]};
 $('tracks').textContent='No runs yet.';renderSpectrum();$('events').replaceChildren();$('timer').textContent='00:00';
 notice('');status(port?'USB connected':'No device connected');controls();
};
$('download').onclick=()=>{
 $('replay-save-hint').textContent='Session downloaded · includes current annotations';
 meta.spectrumSummary=summarizeCorpus(meta.demo?[]:meta.tracks);meta.totalFrames=records.length;meta.exported=new Date().toISOString();
 const url=URL.createObjectURL(pack(meta,records)),a=document.createElement('a');a.href=url;
 a.download=`${meta.demo?'DEMO-':''}companion-music-${meta.created.slice(0,19).replaceAll(':','-')}.mcal`;a.click();setTimeout(()=>URL.revokeObjectURL(url),10000);unsaved=false;
};
$('import').onclick=()=>{if(records.length&&unsaved){notice('Download your current session before opening another.');return;}$('file').click();};
$('file').onchange=async()=>{
 try{const f=$('file').files[0];if(!f)return;if(f.size>256000000)throw new Error('Session exceeds 256 MB limit.');
 const data=unpack(await f.arrayBuffer());
 if(!Array.isArray(data.meta.tracks))throw new Error('Missing session notebook');
 for(const t of data.meta.tracks){if(!Number.isInteger(t.startFrame)||!Number.isInteger(t.endFrame)||t.startFrame<0||t.endFrame<t.startFrame||t.endFrame>data.records.length)throw new Error('Invalid run boundaries');t.summary=summarize(data.records.slice(t.startFrame,t.endFrame));t.markers=t.markers||[];}
 replay.clear();editing=null;records=data.records;collectedBytes=records.reduce((n,p)=>n+p.length,0);meta=data.meta;historyPackets=records.slice(-256);history=historyPackets.map(decode);latest=history.at(-1);lastReceived=0;unsaved=false;
 renderTracks();notice(meta.demo?'This is a simulated session, not device evidence.':'Saved session opened.');controls();
 }catch(e){notice(e.message);}
};
$('demo').onclick=()=>{
 meta.demo=true;$('track').value='Simulated 150 BPM groove';$('style').value='Demo';status('SIMULATED SIGNAL — no device evidence');
 let f=0;demoTimer=setInterval(()=>{for(let i=0;i<4;i++){
 const p=new Uint8Array(1092),v=new DataView(p.buffer);const phase=f%25,k=15+180*Math.exp(-phase/3);
 v.setUint32(0,1000+f*16,true);v.setUint16(4,300,true);v.setUint16(6,k*4,true);v.setUint16(8,100,true);v.setUint16(10,k*3,true);v.setUint16(12,1500,true);
 p[14]=255;p[15]=51;p[16]=255;p[18]=(phase===0?3:0)|((f%125)<75?64:0);p[19]=k;p[20]=90;p[21]=45;v.setUint16(22,1,true);v.setUint32(24,f,true);p.set([80,67,77,49],64);
 for(let j=0;j<256;j++){const t=(f*256+j)/16000;v.setInt16(68+j*4,Math.round(2000*Math.sin(2*Math.PI*440*t)),true);v.setInt16(70+j*4,Math.round(1000*Math.sin(2*Math.PI*660*t)),true);}receive(p);f++;}},64);controls();
};
setInterval(()=>{
 controls();if(run){const sec=Math.floor((performance.now()-started)/1000);$('timer').textContent=`${String(Math.floor(sec/60)).padStart(2,'0')}:${String(sec%60).padStart(2,'0')}`;}
 $('size').textContent=(collectedBytes/1024).toFixed(0);
 if(latest){$('bpm').textContent=latest.bpm?latest.bpm.toFixed(0):'—';$('confidence').textContent=(latest.confidence*100).toFixed(0)+'%';$('level').textContent=latest.rms;
 const recent=summarize(historyPackets);
 $('health').textContent=performance.now()-lastReceived>1500?'No live frames. Saved data is retained.':`${latest.flags&16?'CLIPPING · lower playback level':'Signal arriving'} · ${recent.featureHz?.toFixed(1)??'—'} audio frames/s · ${latest.hasAudio?'Stereo PCM · ':''}${latest.power?'Renderer paused · ':''}${latest.flags&64?'Would dance':latest.flags&128?'Speech response':'No dance admission'} · ${deviceLost} device drops${latest.cpuUs!==null?' · analysis '+latest.cpuUs+' µs':''}${latest.flags&8?' · own voice flagged':''}`;
 if(latest.power)drawSpectrum(latest.power);}
 else{for(const id of ['bpm','confidence','level'])$(id).textContent='—';$('health').textContent='Waiting for feature packets…';}
 const c=$('graph').getContext('2d');c.clearRect(0,0,900,280);const max=Math.max(30,...history.map(a=>a.kick));
 c.strokeStyle='#bbf2a5';c.lineWidth=2;c.beginPath();history.forEach((a,i)=>{const x=i*900/256,y=265-a.kick/max*245;i?c.lineTo(x,y):c.moveTo(x,y);});c.stroke();
 history.forEach((a,i)=>{const x=i*900/256;if(a.flags&2){c.fillStyle='#ffc77c';c.fillRect(x,10,2,255);}else if(a.flags&1){c.fillStyle='#d6e9d6';c.fillRect(x,260,3,3);}});
},100);
window.addEventListener('beforeunload',e=>{if(unsaved||run){e.preventDefault();e.returnValue='';}});
controls();

function drawSpectrum(power){
 const c=$('spectrum-live').getContext('2d'),sum=power.reduce((a,b)=>a+b,0);c.clearRect(0,0,900,180);
 power.forEach((v,i)=>{const h=sum?160*v/sum:0;c.fillStyle='#b2cafa';c.fillRect(i*900/16+3,170-h,900/16-6,h);});
}
function renderSpectrum(){
 const parent=$('spectrum-summary');parent.replaceChildren();
 const groups=[['All references',meta.tracks],...['music','speech','quiet','noise'].map(k=>[k,meta.tracks.filter(t=>t.kind===k)])];
 const table=document.createElement('table'),head=document.createElement('tr');
 for(const name of ['References',...BAND_EDGES_HZ.slice(0,-1).map((v,i)=>`${v}–${BAND_EDGES_HZ[i+1]} Hz`)]){const th=document.createElement('th');th.textContent=name;head.append(th);}table.append(head);
 for(const [name,tracks] of groups){const s=summarizeCorpus(meta.demo?[]:tracks);if(!s.tracks)continue;
  for(const [label,values] of [['equal track',s.equalTrackPercent],['pooled energy',s.energyPercent]]){
   const row=document.createElement('tr'),title=document.createElement('td');title.textContent=`${name} (${s.tracks}) · ${label}`;row.append(title);
   for(const v of values){const td=document.createElement('td');td.textContent=v.toFixed(1)+'%';td.style.background=`rgba(150,210,165,${Math.min(.65,v/40)})`;row.append(td);}table.append(row);
  }
 }
 if(table.rows.length===1){parent.textContent='New firmware captures 16 frequency bands. Earlier files contain only gain-adjusted bass/mid/high, so a reliable frequency split cannot be reconstructed from them.';return;}
 parent.append(table);
}
renderSpectrum();
