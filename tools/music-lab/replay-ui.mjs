import {timeline,wav,pcmStats,intervals,annotation,evaluate} from './replay.mjs';
const fmt=(s,precise=false)=>`${Math.floor(s/60)}:${(s%60).toFixed(precise?3:1).padStart(precise?6:4,'0')}`;
export function createReplay(onEdit,notice){
 const $=id=>document.getElementById(id),audio=$('replay-audio'),canvas=$('replay-timeline');
 const W=1100,H=334,G=112,P=W-G;
 let track=null,records=[],map=null,url=null,stats=null,drag=null,editing=-1,undo=[],peaks=[],dance=[],listen=[];
 let view=0,span=1,selection=false,playingSelection=false,raf=0,lastPaint=0;
 const range=()=>[+$('label-start').value,+$('label-end').value];
 const x=t=>G+(t-view)/span*P;
 const pos=e=>{const r=canvas.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width*W,y:(e.clientY-r.top)/r.height*H};};
 const time=px=>Math.max(0,Math.min(map.duration,view+(px-G)/P*span));
 function setRange(a,b){selection=true;$('label-start').value=Math.max(0,Math.min(a,map.duration)).toFixed(3);$('label-end').value=Math.max(0,Math.min(b,map.duration)).toFixed(3);draw();}
 function snapshot(){undo.push(JSON.stringify(track.annotations||[]));if(undo.length>30)undo.shift();$('label-undo').disabled=false;}
 function changed(){onEdit();$('replay-save-hint').textContent='Unsaved annotations · download session to keep them';}
 function resetEdit(){editing=-1;$('label-save').textContent='Add annotation';$('label-heading').textContent='What should happen here?';$('label-cancel').hidden=true;}
 function expected(value){$('label-expected').value=value;document.querySelectorAll('[data-expected]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.expected===value)));}
 function download(p,name){const u=URL.createObjectURL(new Blob([p],{type:'audio/wav'})),a=document.createElement('a');a.href=u;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(u),10000);}
 function preview(){
  if(!track)return;const current=audio.currentTime||0;audio.pause();if(url)URL.revokeObjectURL(url);
  const gain=$('replay-boost').checked?Math.min(100,26214/Math.max(1,...stats.peak)):1;
  url=URL.createObjectURL(new Blob([wav(records,{map,channel:$('replay-channel').value,gain})],{type:'audio/wav'}));audio.src=url;audio.playbackRate=+$('replay-speed').value;
  audio.onloadedmetadata=()=>{audio.currentTime=Math.min(current,map.duration);draw();};
  $('replay-gain').textContent=`Preview gain ×${gain.toFixed(1)}. WAV export always keeps both original channels and levels.`;
 }
 function clampView(){span=Math.max(Math.min(.25,map.duration),Math.min(map.duration,span));view=Math.max(0,Math.min(map.duration-span,view));}
 function zoom(factor,anchor=audio.currentTime){const relative=Math.max(0,Math.min(1,(anchor-view)/span));span*=factor;clampView();view=anchor-relative*span;clampView();draw();}
 function visible(r){return r.end>view&&r.start<view+span;}
 function block(c,r,y,h,color){if(!visible(r))return;const l=Math.max(G,x(r.start)),right=Math.min(W,x(r.end));c.fillStyle=color;c.fillRect(l,y,Math.max(1,right-l),h);}
 function draw(){if(!map)return;const c=canvas.getContext('2d');c.clearRect(0,0,W,H);c.fillStyle='#101915';c.fillRect(0,0,W,H);c.font='11px system-ui';
  const lanes=[['MIC L',38,104,'#9dcbea'],['MIC R',110,176,'#baace7'],['DEVICE · DANCE',190,220,'#a9d78d'],['DEVICE · LISTEN',229,254,'#dab37a'],['YOUR LABELS',271,312,'#a9d78d']];
  for(const [name,top,bottom,color] of lanes){c.fillStyle='#19241e';c.fillRect(G,top,P,bottom-top);c.fillStyle=color;c.fillText(name,12,(top+bottom)/2+4);}
  const raw=span/8,step=[.025,.05,.1,.25,.5,1,2,5,10,15,30,60,120,300,600,1200].find(v=>v>=raw)||1800;
  for(let t=Math.ceil(view/step)*step;t<view+span;t+=step){const px=x(t);c.fillStyle='#b3c4b9';c.fillText(fmt(t),px+5,19);c.strokeStyle='#ffffff0d';c.beginPath();c.moveTo(px,27);c.lineTo(px,H);c.stroke();}
  const bars=Array.from({length:P},()=>[0,0]);
  for(let j=0;j<map.entries.length;j++){const e=map.entries[j];if(!visible(e)||!e.a.hasAudio)continue;const l=Math.max(0,Math.floor(x(e.start)-G)),r=Math.min(P-1,Math.floor(x(e.end)-G));
   for(let k=l;k<=r;k++){bars[k][0]=Math.max(bars[k][0],peaks[j][0]);bars[k][1]=Math.max(bars[k][1],peaks[j][1]);}
  }
  for(let px=0;px<P;px++)for(let ch=0;ch<2;ch++){const height=bars[px][ch]*28;c.fillStyle=ch?'#ae9ed9':'#8bbada';if(height)c.fillRect(G+px,71+ch*72-height,1,Math.max(1,height*2));}
  for(const r of dance)block(c,r,195,20,'#83b967');for(const r of listen)block(c,r,234,15,'#bb945c');
  (track.annotations||[]).forEach((l,i)=>{block(c,l,277,29,l.expected==='dance'?'#70a85d':l.expected==='no_dance'?'#b97870':'#8875ab');if(visible(l)){c.save();c.beginPath();c.rect(Math.max(G,x(l.start))+3,278,Math.max(0,Math.min(W,x(l.end))-Math.max(G,x(l.start))-6),27);c.clip();c.fillStyle='#f4fff0';c.fillText(`${i+1} · ${l.label||({dance:'Dance',no_dance:'Stay quiet',unsure:'Note'}[l.expected])}`,Math.max(G,x(l.start))+7,295);c.restore();}});
  for(const g of map.gaps){if(g.end===g.start&&g.start>=view&&g.start<=view+span){c.fillStyle='#f69383';c.fillRect(x(g.start),30,3,H-30);}else if(visible(g)){block(c,g,30,H-30,'#c4636338');const left=Math.max(G,x(g.start)),right=Math.min(W,x(g.end));c.save();c.beginPath();c.rect(left,30,right-left,H-30);c.clip();c.strokeStyle='#ee8d8255';for(let xx=left-H;xx<right;xx+=10){c.beginPath();c.moveTo(xx,H);c.lineTo(xx+H,30);c.stroke();}c.restore();}}
  const [a,b]=range();if(selection&&b>a){c.save();c.beginPath();c.rect(G,0,P,H);c.clip();c.fillStyle='#c7eea51b';c.fillRect(x(a),29,x(b)-x(a),H-29);c.strokeStyle='#c2eaa0';c.lineWidth=1.5;c.strokeRect(x(a),29,x(b)-x(a),H-30);for(const t of [a,b]){c.fillStyle='#c2eaa0';c.fillRect(x(t)-4,29,8,18);c.fillRect(x(t)-2,H-14,4,12);}c.restore();}
  if(audio.currentTime>=view&&audio.currentTime<=view+span){c.fillStyle='#f5d790';const px=x(audio.currentTime);c.fillRect(px,25,1.5,H-25);c.beginPath();c.moveTo(px-5,25);c.lineTo(px+5,25);c.lineTo(px,32);c.fill();}
  $('replay-time').textContent=`${fmt(audio.currentTime,true)} / ${fmt(map.duration)}`;
  $('replay-zoom-text').textContent=span>=map.duration-.001?'Full track':`${fmt(view)} – ${fmt(view+span)} · ${(map.duration/span).toFixed(1)}× zoom`;
  $('replay-pan').disabled=span>=map.duration-.001;$('replay-pan').value=map.duration>span?view/(map.duration-span)*1000:0;
  $('selection-info').textContent=selection&&b>a?`${fmt(a,true)} → ${fmt(b,true)} · ${(b-a).toFixed(3)}s selected`:'Click to seek · drag on a waveform to select a passage';
  $('replay-selection').disabled=!selection||b<=a;$('zoom-selection').disabled=!selection||b<=a;$('label-save').disabled=!selection||b<=a;
 }
 function editLabel(i){const l=track.annotations[i];editing=i;setRange(l.start,l.end);expected(l.expected);$('label-comment').value=l.label;$('label-save').textContent='Save annotation';$('label-heading').textContent=`Edit annotation ${i+1}`;$('label-cancel').hidden=false;audio.currentTime=l.start;if(l.start<view||l.end>view+span){view=l.start;span=Math.max(2,l.end-l.start)*1.2;clampView();}draw();}
 function labels(){const root=$('range-labels');root.replaceChildren();$('label-count').textContent=(track.annotations||[]).length;
  if(!track.annotations?.length){const empty=document.createElement('div');empty.className='empty-labels';empty.textContent='Hear something worth noting? Drag across that passage, choose what he should do, and add an annotation. You can refine it afterward.';root.append(empty);}
  (track.annotations||[]).forEach((l,i)=>{const row=document.createElement('div');row.className='annotation-card';row.dataset.kind=l.expected;
   const head=document.createElement('div');head.className='row';const title=document.createElement('strong');title.textContent=`${i+1} · ${{dance:'Should dance',no_dance:'No dancing',unsure:'Note'}[l.expected]}`;head.append(title);row.append(head);
   const timing=document.createElement('div');timing.className='label-time';timing.textContent=`${fmt(l.start,true)} → ${fmt(l.end,true)}`;row.append(timing);
   if(l.label){const text=document.createElement('p');text.textContent=l.label;row.append(text);}
   const buttons=document.createElement('div');buttons.className='row';
   const play=document.createElement('button');play.textContent='▶ Listen';play.onclick=()=>{setRange(l.start,l.end);playRange();};buttons.append(play);
   const edit=document.createElement('button');edit.textContent='Edit';edit.onclick=()=>editLabel(i);buttons.append(edit);
   const remove=document.createElement('button');remove.textContent='Delete';remove.onclick=()=>{snapshot();track.annotations.splice(i,1);resetEdit();changed();labels();draw();};buttons.append(remove);row.append(buttons);root.append(row);
  });
  const e=evaluate(map,track.annotations||[]);
  $('label-score').textContent=`Human-labelled ${e.labelled.toFixed(2)}s · correctly dancing ${e.tp.toFixed(2)}s · missed dance ${e.fn.toFixed(2)}s · false dance ${e.fp.toFixed(2)}s · correctly quiet ${e.tn.toFixed(2)}s · conflicting labels excluded ${e.conflict.toFixed(2)}s. Unlabelled ranges and missing audio are excluded.`;
 }
 function play(){audio.play().catch(e=>notice(e.message));}
 function playRange(){if(!selection)return;const [a,b]=range();if(b<=a)return;playingSelection=true;audio.currentTime=a;play();}
 function tick(now){if(!track)return;if(playingSelection){const [a,b]=range();if(audio.currentTime>=b){if($('replay-loop').checked)audio.currentTime=a;else{audio.pause();audio.currentTime=b;playingSelection=false;}}}
  if(now-lastPaint>32){if(!audio.paused&&(audio.currentTime>view+span||audio.currentTime<view)){view=audio.currentTime;clampView();}draw();lastPaint=now;}
  if(!audio.paused)raf=requestAnimationFrame(tick);
 }
 audio.onplay=()=>{$('replay-play').textContent='❚❚ Pause';cancelAnimationFrame(raf);raf=requestAnimationFrame(tick);};
 audio.onpause=()=>{$('replay-play').textContent='▶ Play';cancelAnimationFrame(raf);draw();};audio.ontimeupdate=()=>{if(audio.paused)draw();};
 audio.onended=()=>{if(playingSelection&&$('replay-loop').checked){audio.currentTime=range()[0];play();}};
 $('replay-play').onclick=()=>{playingSelection=false;if(audio.paused)play();else audio.pause();};
 $('replay-back').onclick=()=>{audio.currentTime=Math.max(0,audio.currentTime-5);draw();};$('replay-selection').onclick=playRange;
 $('replay-speed').onchange=()=>{audio.playbackRate=+$('replay-speed').value;};
 $('label-save').onclick=()=>{try{if(!track||!selection)return;const l=annotation(...range(),$('label-expected').value,$('label-comment').value,map.duration);snapshot();
  track.annotations??=[];if(editing<0)track.annotations.push(l);else track.annotations[editing]=l;
  resetEdit();$('label-comment').value='';changed();labels();draw();$('label-feedback').textContent='Annotation saved to this session';
 }catch(e){notice(e.message);}};
 $('label-undo').onclick=()=>{if(!track||!undo.length)return;track.annotations=JSON.parse(undo.pop());$('label-undo').disabled=!undo.length;resetEdit();changed();labels();draw();};
 $('label-cancel').onclick=()=>{resetEdit();$('label-comment').value='';};
 $('expected-buttons').onclick=e=>{if(e.target.dataset.expected)expected(e.target.dataset.expected);};
 $('label-in').onclick=()=>setRange(audio.currentTime,Math.max(audio.currentTime+.016,range()[1]));$('label-out').onclick=()=>setRange(Math.min(range()[0],audio.currentTime),audio.currentTime);
 for(const id of ['label-start','label-end'])$(id).oninput=()=>{selection=true;draw();};
 $('selection-clear').onclick=()=>{selection=false;resetEdit();playingSelection=false;draw();};
 $('zoom-in').onclick=()=>zoom(.5);$('zoom-out').onclick=()=>zoom(2);$('zoom-fit').onclick=()=>{view=0;span=map.duration;draw();};
 $('zoom-selection').onclick=()=>{const [a,b]=range();if(b>a){view=a;span=b-a;clampView();draw();}};
 $('replay-pan').oninput=()=>{view=+$('replay-pan').value/1000*(map.duration-span);draw();};
 $('replay-channel').onchange=()=>{try{preview();}catch(e){notice(e.message);}};$('replay-boost').onchange=$('replay-channel').onchange;
 $('replay-wav').onclick=()=>{try{download(wav(records,{map}),(track.track||'microphones').replace(/[^\w .-]/g,'_')+'.wav');}catch(e){notice(e.message);}};
 canvas.onpointerdown=e=>{if(!map)return;const p=pos(e);if(p.x<G)return;canvas.focus();const t=time(p.x),[a,b]=range();
  if(p.y>=271&&p.y<312){const i=(track.annotations||[]).findIndex(l=>t>=l.start&&t<=l.end);if(i>=0){editLabel(i);return;}}
  drag={start:t,x:p.x,a,b,mode:selection&&Math.abs(p.x-x(a))<10?'left':selection&&Math.abs(p.x-x(b))<10?'right':'new',moved:false};canvas.setPointerCapture(e.pointerId);
 };
 canvas.onpointermove=e=>{if(!drag||!map)return;const p=pos(e),t=time(p.x);if(Math.abs(p.x-drag.x)>3)drag.moved=true;if(!drag.moved)return;
  if(drag.mode==='left')setRange(Math.min(t,drag.b-.001),drag.b);else if(drag.mode==='right')setRange(drag.a,Math.max(t,drag.a+.001));else setRange(Math.min(t,drag.start),Math.max(t,drag.start));
 };
 canvas.onpointerup=e=>{if(!drag)return;if(!drag.moved){audio.currentTime=time(pos(e).x);playingSelection=false;}drag=null;draw();};canvas.onpointercancel=()=>{drag=null;};
 canvas.addEventListener('wheel',e=>{if(!map||!e.ctrlKey)return;e.preventDefault();zoom(e.deltaY>0?1.2:1/1.2,time(pos(e).x));},{passive:false});
 $('replay-section').onkeydown=e=>{if(!track||/INPUT|TEXTAREA|SELECT|BUTTON/.test(e.target.tagName))return;
  if(e.code==='Space'){e.preventDefault();$('replay-play').click();}else if(e.key.toLowerCase()==='i')$('label-in').click();else if(e.key.toLowerCase()==='o')$('label-out').click();else if(e.key==='+'||e.key==='='){e.preventDefault();zoom(.5);}else if(e.key==='-'){e.preventDefault();zoom(2);}else if(e.key==='ArrowRight'||e.key==='ArrowLeft'){e.preventDefault();audio.currentTime=Math.max(0,Math.min(map.duration,audio.currentTime+(e.key==='ArrowRight'?1:-1)*(e.shiftKey?5:1)));draw();}
 };
 function clear(){audio.pause();cancelAnimationFrame(raf);audio.removeAttribute('src');audio.load();if(url)URL.revokeObjectURL(url);url=null;track=null;map=null;records=[];peaks=[];undo=[];playingSelection=false;$('replay-section').hidden=true;}
 return {clear,open(t,p){try{
  clear();track=t;records=p;map=timeline(records);stats=pcmStats(records);if(!stats.samples)throw new Error('This old run has features only. Record a new stereo run for microphone replay.');
  const peak=Math.max(1,...stats.peak);peaks=records.map((p,j)=>{const out=[0,0];if(!map.entries[j].a.hasAudio)return out;const v=new DataView(p.buffer,p.byteOffset,p.byteLength);for(let i=0;i<256;i++)for(let ch=0;ch<2;ch++)out[ch]=Math.max(out[ch],Math.abs(v.getInt16(68+i*4+ch*2,true))/peak);return out;});
  dance=intervals(map);listen=intervals(map,128);view=0;span=map.duration;selection=false;resetEdit();$('label-start').value=0;$('label-end').value=0;$('label-comment').value='';$('label-feedback').textContent='';$('label-undo').disabled=true;
  $('replay-title').textContent=t.track;$('replay-section').hidden=false;$('replay-save-hint').textContent='Download session to keep annotations';
  $('replay-stats').textContent=`Stereo 16 kHz · L/R RMS ${stats.rms.map(v=>v.toFixed(1)).join(' / ')} · peaks ${stats.peak.join(' / ')} · clipped samples ${stats.clipped.join(' / ')} · correlation ${stats.correlation?.toFixed(3)??'—'} · ${map.gaps.length} gap/boundary marker(s).`;
  $('replay-detected').textContent='Recorded algorithm would dance: '+(dance.length?dance.map(r=>`${fmt(r.start)}–${fmt(r.end)}`).join(' · '):'none');
  preview();labels();draw();$('replay-section').scrollIntoView({behavior:'smooth'});$('replay-section').focus({preventScroll:true});
 }catch(e){clear();notice(e.message);}}};
}
