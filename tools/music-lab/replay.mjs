/* Sample-clock replay: sequence gaps become silence, never compressed time.
 * PCM is untouched in exports. Labels use seconds from this track's first frame. */
import {decode} from './trace.mjs?v=portable4';
export function timeline(records){
 const entries=[],gaps=[];let cursor=0,prev=null;
 for(let i=0;i<records.length;i++){
  const a=decode(records[i]);let missing=0;
  if(prev&&a.session===prev.session&&a.sequence!==null&&prev.sequence!==null){
   const step=(a.sequence-prev.sequence)>>>0;
   if(step===0||step>=0x80000000)throw new Error('Non-monotonic audio sequence');
   missing=step-1;
  }else if(prev){gaps.push({start:cursor*.016,end:cursor*.016,reason:'Session boundary: elapsed missing time unknown'});}
  if(missing){gaps.push({start:cursor*.016,end:(cursor+missing)*.016,reason:'Missing USB frames'});cursor+=missing;}
  if(cursor>250000)throw new Error('Replay timeline exceeds one-hour limit; split this recording');
  const entry={index:i,start:cursor*.016,end:(cursor+1)*.016,frame:cursor,a};entries.push(entry);
  if(!a.hasAudio)gaps.push({start:entry.start,end:entry.end,reason:'No microphone audio'});
  cursor++;prev=a;
 }
 return {entries,gaps,frames:cursor,duration:cursor*.016};
}
export function pcmStats(records){
 let samples=0,peak=[0,0],sum=[0,0],cross=0,clipped=[0,0];
 for(const p of records){if(!decode(p).hasAudio)continue;const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
  for(let i=0;i<256;i++){const l=v.getInt16(68+i*4,true),r=v.getInt16(70+i*4,true);samples++;cross+=l*r;
   [l,r].forEach((x,c)=>{peak[c]=Math.max(peak[c],Math.abs(x));sum[c]+=x*x;clipped[c]+=Math.abs(x)>=32760;});}
 }
 return {samples,peak,rms:sum.map(x=>samples?Math.sqrt(x/samples):0),clipped,correlation:sum[0]&&sum[1]?cross/Math.sqrt(sum[0]*sum[1]):null};
}
export function wav(records,{channel='stereo',gain=1,map=timeline(records)}={}){
 if(!map.entries.some(e=>e.a.hasAudio))throw new Error('This recording contains features only; microphone replay needs a new stereo capture.');
 const channels=channel==='stereo'?2:1,size=map.frames*256*channels*2;
 if(size>256000000)throw new Error('WAV exceeds 256 MB replay limit');
 const p=new Uint8Array(44+size),v=new DataView(p.buffer);
 const tag=(at,s)=>p.set(new TextEncoder().encode(s),at);
 tag(0,'RIFF');v.setUint32(4,36+size,true);tag(8,'WAVE');tag(12,'fmt ');v.setUint32(16,16,true);
 v.setUint16(20,1,true);v.setUint16(22,channels,true);v.setUint32(24,16000,true);v.setUint32(28,16000*channels*2,true);
 v.setUint16(32,channels*2,true);v.setUint16(34,16,true);tag(36,'data');v.setUint32(40,size,true);
 for(const e of map.entries){if(!e.a.hasAudio)continue;const b=records[e.index],src=new DataView(b.buffer,b.byteOffset,b.byteLength);
  for(let i=0;i<256;i++)for(let c=0;c<channels;c++){
   const input=channels===2?c:channel==='right'?1:0;
   const sample=Math.max(-32768,Math.min(32767,Math.round(src.getInt16(68+i*4+input*2,true)*gain)));
   v.setInt16(44+((e.frame*256+i)*channels+c)*2,sample,true);
  }
 }
 return p;
}
export function intervals(map,flag=64){
 const out=[];for(const e of map.entries){if(!(e.a.flags&flag))continue;const prev=out.at(-1);
  if(prev&&Math.abs(prev.end-e.start)<.00001)prev.end=e.end;else out.push({start:e.start,end:e.end});
 }return out;
}
export const DANCE_STYLES=Object.freeze({none:'No dance',slow:'Slow sway',groove:'Groove',energetic:'Energetic'});
export const AUDIBLE_CONTENT=Object.freeze({speech:'Speech only',speech_music:'Speech + background music',music:'Music (including singing)',other:'Other sound'});
export function annotation(start,end,expected,label,duration,{danceStyle='',audibleContent=''}={}){
 if(!Number.isFinite(start)||!Number.isFinite(end)||start<0||end<=start||end>duration+.001)throw new Error('Choose a valid start/end range within the recording.');
 if(!['dance','no_dance','unsure'].includes(expected))throw new Error('Invalid expected response');
 if(danceStyle&&!Object.hasOwn(DANCE_STYLES,danceStyle))throw new Error('Choose a valid dance movement.');
 if(audibleContent&&!Object.hasOwn(AUDIBLE_CONTENT,audibleContent))throw new Error('Choose a valid audible content label.');
 if(danceStyle&&expected!==(danceStyle==='none'?'no_dance':'dance'))throw new Error('Dance movement must agree with the expected response.');
 if(!label.trim()&&expected==='unsure'&&!audibleContent)throw new Error('Add a comment, audible content or an expected response.');
 return {start,end:Math.min(end,duration),expected,label:label.trim(),source:'human',...(danceStyle?{danceStyle}:{}),...(audibleContent?{audibleContent}:{})};
}
/* Only explicit, non-conflicting human labels are ground truth. Split frame
 * intersections at label edges; unlabelled/missing audio is not a negative. */
export function evaluate(map,labels){
 const result={tp:0,fp:0,fn:0,tn:0,conflict:0,labelled:0};
 for(const e of map.entries){if(!e.a.hasAudio)continue;
  const active=labels.filter(l=>l.expected!=='unsure'&&l.start<e.end&&l.end>e.start);
  const edges=[e.start,e.end,...active.flatMap(l=>[Math.max(e.start,l.start),Math.min(e.end,l.end)])].sort((a,b)=>a-b);
  for(let i=1;i<edges.length;i++){const start=edges[i-1],end=edges[i];if(end===start)continue;
   const expected=new Set(active.filter(l=>l.start<end&&l.end>start).map(l=>l.expected));
   if(expected.size>1){result.conflict+=end-start;continue;}if(!expected.size)continue;
   const positive=expected.has('dance'),detected=!!(e.a.flags&64);result[positive?(detected?'tp':'fn'):(detected?'fp':'tn')]+=end-start;result.labelled+=end-start;
  }
 }
 return result;
}
