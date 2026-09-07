import {parseLine,decode,pack,unpack,summarize} from './trace.mjs';
const $=id=>document.getElementById(id);
let lastPerf=null;
let deviceConfig=null,stopAck=null,connecting=false,editing=null;
async function send(command){const w=port.writable.getWriter();try{await w.write(new TextEncoder().encode(command+"\n"));}finally{w.releaseLock();}}
let port,reader,reading=false,demoTimer,latest,lastReceived=0,history=[],records=[],run=null,started=0,unsaved=false;
let meta={format:1,created:new Date().toISOString(),demo:false,tracks:[],transportEvents:[]};
const fields=['track','kind','style','volume','setup','notes'];
function notice(s){$('notice').textContent=s;}
function status(s){$('status').textContent=s;}
function controls(){
 const fresh=performance.now()-lastReceived<1500;
 $('start').disabled=!!run||!!editing||!latest||!fresh;$('save-labels').hidden=!editing;
 $('stop').disabled=!run;$('download').disabled=!!run||!records.length;
 $('connect').disabled=connecting||!!port||!!demoTimer;$('disconnect').disabled=!port;
 $('demo').disabled=!!port||!!run||!!demoTimer||records.length>0;
 $('import').disabled=!!run||!!port||!!demoTimer;$('new').disabled=!!run;
 fields.forEach(id=>$(id).disabled=!!run);
 document.querySelectorAll('[data-mark]').forEach(b=>b.disabled=!run);
}
function receive(p){
 latest=decode(p);lastReceived=performance.now();history.push(latest);if(history.length>256)history.shift();
 if(run){records.push(p);unsaved=true;if(records.length>=500000){endRun();notice('Session limit reached (about two hours). Download before starting a new session.');}}
}
function line(s){
 try{for(const p of parseLine(s))receive(p);}catch(e){notice(e.message);meta.transportEvents.push({type:'invalid_packet',at:new Date().toISOString()});}
 const perf=s.match(/I \((\d+)\).*?: (\d+) fps \| raster (\d+) us avg, (\d+) us max \| push (\d+) us/);
 if(perf){const cpu=s.match(/audio cpu (\d+) us/);lastPerf={ms:+perf[1],fps:+perf[2],rasterAvgUs:+perf[3],rasterMaxUs:+perf[4],pushUs:+perf[5],audioUs:cpu?+cpu[1]:null};if(run)(run.performance??=[]).push(lastPerf);}
 const loss=s.match(/MC_LOST:(\d+)/);if(loss){meta.transportEvents.push({type:'device_queue_overflow',frames:+loss[1],deviceMs:latest?.ms});notice(`Device dropped ${loss[1]} frames; marked in the session.`);}
 const cfg=s.match(/MC_CONFIG:(.*?)(?:\x1b\[[0-9;]*m)?$/);if(cfg)deviceConfig=cfg[1];
 if(s.includes('MC_SESSION:')&&s.includes('stop'))stopAck?.();
 if(s.includes('MC_SESSION:'))status(s.replace(/\x1b\[[0-9;]*m/g,'').slice(s.indexOf('MC_SESSION:')));
}
$('connect').onclick=async()=>{
 if(connecting||port)return;connecting=true;controls();
 try{
  if(meta.demo&&records.length)throw new Error('Choose New session before collecting device evidence.');
  if(!navigator.serial)throw new Error('Use Chrome or Edge on localhost for USB capture.');
  const known=await navigator.serial.getPorts();
  port=known.length===1?known[0]:await navigator.serial.requestPort();await port.open({baudRate:115200});
  lastPerf=null;deviceConfig=null;
  // Do not toggle DTR/RTS: opening the page should not reset the companion.
  reading=true;status('USB connected. Starting feature stream…');notice('');controls();
  const decoder=new TextDecoder();let buffer='';
  reader=port.readable.getReader();
  await send("MC_START");
  while(reading){const {value,done}=await reader.read();if(done)break;
   buffer+=decoder.decode(value,{stream:true});let i;
   while((i=buffer.indexOf('\n'))>=0){line(buffer.slice(0,i).trim());buffer=buffer.slice(i+1);}
   if(buffer.length>8192){buffer='';notice('Oversized serial line discarded.');}
  }
 }catch(e){notice(e.message);}
 finally{
  if(run){mark('USB disconnected');endRun();}
  reader?.releaseLock();reader=null;reading=false;connecting=false;
  if(port){try{await port.close();}catch{}port=null;}
  lastReceived=0;status('USB disconnected. Collected runs remain available to download.');controls();
 }
};
$('disconnect').onclick=async()=>{
 try{const ack=new Promise(resolve=>{stopAck=resolve;setTimeout(resolve,1000);});await send('MC_STOP');await ack;}catch(e){notice(e.message);}
 stopAck=null;reading=false;await reader?.cancel();
};
$('start').onclick=()=>{
 if(records.length>=500000)return notice('Download this session and reload before recording more.');
 if(!latest||performance.now()-lastReceived>1500)return notice('Waiting for live device frames.');
 if(!$('track').value.trim())return notice('Give this run a track or reference name.');
 run={};fields.forEach(id=>run[id]=$(id).value.trim());
 run.deviceConfig=deviceConfig;
 run.startFrame=records.length;run.startDeviceMs=latest.ms;run.deviceSession=latest.session;run.markers=[];
 run.startedUTC=new Date().toISOString();started=performance.now();notice('');controls();
};
function mark(label){if(!run)return;run.markers.push({label,frame:records.length,deviceMs:latest?.ms,deviceSession:latest?.session,hostElapsedMs:Math.round(performance.now()-started)});unsaved=true;renderEvents();}
function renderEvents(){const parent=$('events');parent.replaceChildren();for(const e of (run?.markers||[]).slice(-5)){const d=document.createElement('div');d.textContent=`${(e.hostElapsedMs/1000).toFixed(1)}s · ${e.label}`;parent.append(d);}}
$('markers').onclick=e=>{if(e.target.dataset.mark)mark(e.target.dataset.mark);};
function endRun(){
 if(!run)return;run.endFrame=records.length;run.summary=summarize(records.slice(run.startFrame,run.endFrame));
 run.hostDurationMs=Math.round(performance.now()-started);meta.tracks.push(run);run=null;unsaved=true;renderTracks();controls();
}
$('stop').onclick=endRun;
function renderTracks(){
 const el=$('tracks');el.replaceChildren();
 for(const t of meta.tracks){const details=document.createElement('details'),d=document.createElement('summary');const s=t.summary;
 d.textContent=`${t.track} · ${t.kind} · ${t.volume||'level unspecified'} · ${s.seconds.toFixed(1)}s · ${s.beats} beats · ${s.eligiblePercent.toFixed(0)}% rhythm-qualified · ${t.markers.length} markers · ${s.dancePercent.toFixed(0)}% dancing${s.missingMs>0?' · timing gaps: '+s.missingMs+' ms':''}`;details.append(d);
 for(const m of t.markers){const line=document.createElement('div');line.textContent=`${(m.hostElapsedMs/1000).toFixed(1)}s · ${m.label}`;details.append(line);}
 const edit=document.createElement('button');edit.textContent='Edit labels';edit.disabled=!!run;
 edit.onclick=()=>{if(run)return;editing=t;fields.forEach(id=>$(id).value=t[id]||'');controls();$('track').focus();};details.append(edit);
 el.append(details);}
}
$('save-labels').onclick=()=>{if(!editing)return;fields.forEach(id=>editing[id]=$(id).value.trim());editing=null;unsaved=true;renderTracks();controls();};
$('new').onclick=()=>{
 if(unsaved)return notice('Download this session before starting a new one.');
 if(demoTimer){clearInterval(demoTimer);demoTimer=null;latest=null;lastReceived=0;fields.forEach(id=>$(id).value=id==='kind'?'music':'');}
 editing=null;records=[];history=[];meta={format:1,created:new Date().toISOString(),demo:false,tracks:[],transportEvents:[]};
 $('tracks').textContent='No runs yet.';$('events').replaceChildren();$('timer').textContent='00:00';
 notice('');status(port?'USB connected':'No device connected');controls();
};
$('download').onclick=()=>{
 meta.totalFrames=records.length;meta.exported=new Date().toISOString();
 const url=URL.createObjectURL(pack(meta,records)),a=document.createElement('a');a.href=url;
 a.download=`${meta.demo?'DEMO-':''}companion-music-${meta.created.slice(0,19).replaceAll(':','-')}.mcal`;a.click();setTimeout(()=>URL.revokeObjectURL(url),10000);unsaved=false;
};
$('import').onclick=()=>{if(records.length&&unsaved){notice('Download your current session before opening another.');return;}$('file').click();};
$('file').onchange=async()=>{
 try{const f=$('file').files[0];if(!f)return;if(f.size>15000000)throw new Error('Session exceeds 15 MB limit.');
 const data=unpack(await f.arrayBuffer());
 if(!Array.isArray(data.meta.tracks))throw new Error('Missing session notebook');
 for(const t of data.meta.tracks){if(!Number.isInteger(t.startFrame)||!Number.isInteger(t.endFrame)||t.startFrame<0||t.endFrame<t.startFrame||t.endFrame>data.records.length)throw new Error('Invalid run boundaries');t.summary=summarize(data.records.slice(t.startFrame,t.endFrame));t.markers=t.markers||[];}
 editing=null;records=data.records;meta=data.meta;history=records.slice(-256).map(decode);latest=history.at(-1);lastReceived=0;unsaved=false;
 renderTracks();notice(meta.demo?'This is a simulated session, not device evidence.':'Saved session opened.');controls();
 }catch(e){notice(e.message);}
};
$('demo').onclick=()=>{
 meta.demo=true;$('track').value='Simulated 150 BPM groove';$('style').value='Demo';status('SIMULATED SIGNAL — no device evidence');
 let f=0;demoTimer=setInterval(()=>{for(let i=0;i<4;i++){
 const p=new Uint8Array(24),v=new DataView(p.buffer);const phase=f%25,k=15+180*Math.exp(-phase/3);
 v.setUint32(0,1000+f*16,true);v.setUint16(4,300,true);v.setUint16(6,k*4,true);v.setUint16(8,100,true);v.setUint16(10,k*3,true);v.setUint16(12,1500,true);
 p[14]=255;p[15]=51;p[16]=255;p[18]=phase===0?3:0;p[19]=k;p[20]=90;p[21]=45;v.setUint16(22,1,true);receive(p);f++;}},64);controls();
};
setInterval(()=>{
 controls();if(run){const sec=Math.floor((performance.now()-started)/1000);$('timer').textContent=`${String(Math.floor(sec/60)).padStart(2,'0')}:${String(sec%60).padStart(2,'0')}`;}
 $('size').textContent=(records.length*24/1024).toFixed(0);
 if(latest){$('bpm').textContent=latest.bpm?latest.bpm.toFixed(0):'—';$('confidence').textContent=(latest.confidence*100).toFixed(0)+'%';$('level').textContent=latest.rms;
 $('health').textContent=performance.now()-lastReceived>1500?'No live frames. Saved data is retained.':`${latest.flags&16?'CLIPPING · lower playback level':'Signal arriving'} · ${latest.flags&64?'Dancing':latest.flags&128?'Listening':'Idle'}${lastPerf?' · '+lastPerf.fps+' FPS':''}${latest.flags&8?' · own voice flagged':''}`;}
 else{for(const id of ['bpm','confidence','level'])$(id).textContent='—';$('health').textContent='Waiting for feature packets…';}
 const c=$('graph').getContext('2d');c.clearRect(0,0,900,280);const max=Math.max(30,...history.map(a=>a.kick));
 c.strokeStyle='#bbf2a5';c.lineWidth=2;c.beginPath();history.forEach((a,i)=>{const x=i*900/256,y=265-a.kick/max*245;i?c.lineTo(x,y):c.moveTo(x,y);});c.stroke();
 history.forEach((a,i)=>{const x=i*900/256;if(a.flags&2){c.fillStyle='#ffc77c';c.fillRect(x,10,2,255);}else if(a.flags&1){c.fillStyle='#d6e9d6';c.fillRect(x,260,3,3);}});
},100);
window.addEventListener('beforeunload',e=>{if(unsaved||run){e.preventDefault();e.returnValue='';}});
controls();
