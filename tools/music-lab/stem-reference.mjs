export const STEM_NAMES=['Drums','Bass','Vocals','Other'];
export const STEM_COLORS=['#e9b67c','#ba9cdf','#89c9e7','#92cfa6'];
export function validateReference(r,frames,hash){
 if(r?.format!=='music-lab-stems-v1'||r.source?.sampleRate!==16000||r.source?.channels!==2||r.source?.frames!==frames||r.hopSamples!==1600||r.unit!=='0.1 dBFS')throw new Error('Stem reference does not match this microphone timeline');
 if(!/^[a-f0-9]{64}$/.test(r.source.wavSHA256)||hash&&r.source.wavSHA256!==hash)throw new Error('These stems belong to a different recording');
 if(typeof r.model!=='string'||r.model.length>200)throw new Error('Invalid separation model');
 for(const name of ['Mixture',...STEM_NAMES]){
  const v=r.levels?.[name];if(!Array.isArray(v)||v.length!==Math.ceil(frames/1600)||v.some(x=>!Number.isInteger(x)||x< -1200||x>0))throw new Error('Invalid or incomplete stem level curve');
 }
 return r;
}
export function paintReference(c,r,{view,span,playhead,selection,gaps=[]}){
 const W=1100,G=112,H=200,P=W-G,x=t=>G+(t-view)/span*P;
 c.clearRect(0,0,W,H);c.fillStyle='#111b16';c.fillRect(0,0,W,H);c.font='11px system-ui';
 const peak=Math.max(-900,...r.levels.Mixture),floor=peak-450;
 for(let lane=0;lane<4;lane++){
  const top=lane*50+4,bottom=top+42,values=r.levels[STEM_NAMES[lane]];
  c.fillStyle='#1c2922';c.fillRect(G,top,P,42);c.fillStyle=STEM_COLORS[lane];c.fillText(STEM_NAMES[lane]==='Other'?'ACCOMP.':STEM_NAMES[lane].toUpperCase(),12,top+25);
  const bins=new Float32Array(P);
  for(let i=Math.max(0,Math.floor(view*10));i<Math.min(values.length,Math.ceil((view+span)*10));i++){
   const level=Math.max(0,Math.min(1,(values[i]-floor)/450)),left=Math.max(0,Math.floor(x(i*.1)-G)),right=Math.min(P-1,Math.floor(x((i+1)*.1)-G));
   for(let p=left;p<=right;p++)bins[p]=Math.max(bins[p],level);
  }
  c.fillStyle=STEM_COLORS[lane];for(let p=0;p<P;p++)if(bins[p])c.fillRect(G+p,bottom-bins[p]*36,1,bins[p]*36);
 }
 c.save();c.beginPath();c.rect(G,0,P,H);c.clip();
 for(const gap of gaps){if(gap.start>view+span||gap.end<view)continue;c.fillStyle='#d4757577';c.fillRect(x(gap.start),0,Math.max(3,x(gap.end)-x(gap.start)),H);}
 if(selection&&selection[1]>selection[0]){c.fillStyle='#c7eea51b';c.fillRect(x(selection[0]),0,x(selection[1])-x(selection[0]),H);c.strokeStyle='#bce49d';c.strokeRect(x(selection[0]),0,x(selection[1])-x(selection[0]),H);}
 if(playhead>=view&&playhead<=view+span){c.fillStyle='#f5d790';c.fillRect(x(playhead),0,1.5,H);}
 c.restore();
}
