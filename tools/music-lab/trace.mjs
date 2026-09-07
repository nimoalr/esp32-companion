export const RECORD_BYTES=24;
export function decode(p){
 const v=new DataView(p.buffer,p.byteOffset,p.byteLength);
 return {ms:v.getUint32(0,true),rms:v.getUint16(4,true),kick:v.getUint16(6,true)/4,
 mean:v.getUint16(8,true)/4,previous:v.getUint16(10,true)/4,bpm:v.getUint16(12,true)/10,
 presence:p[14]/255,ratio:p[15]/127.5,confidence:p[16]/255,depth:p[17]/64,
 flags:p[18],bass:p[19]/255,mid:p[20]/255,high:p[21]/255,session:v.getUint16(22,true)};
}
export function parseLine(line){
 const match=line.match(/MC1:([0-9a-f]+)(?:\x1b\[[0-9;]*m)?\s*$/i);
 if(!match)return [];
 const s=match[1];if(s.length%48)throw new Error('Incomplete trace packet');
 const out=[];
 for(let i=0;i<s.length;i+=48){const p=new Uint8Array(24);for(let j=0;j<24;j++)p[j]=parseInt(s.slice(i+j*2,i+j*2+2),16);out.push(p);}
 return out;
}
export function pack(meta,records){
 const json=new TextEncoder().encode(JSON.stringify(meta));
 const header=new Uint8Array(12);header.set(new TextEncoder().encode('MCALv001'));
 new DataView(header.buffer).setUint32(8,json.length,true);
 return new Blob([header,json,...records],{type:'application/octet-stream'});
}
export function unpack(buffer){
 const p=new Uint8Array(buffer),v=new DataView(buffer);
 if(p.length<12||new TextDecoder().decode(p.slice(0,8))!=='MCALv001')throw new Error('Not a Music Lab session');
 const n=v.getUint32(8,true);if(n>p.length-12 || (p.length-12-n)%24)throw new Error('Incomplete session');
 const meta=JSON.parse(new TextDecoder().decode(p.slice(12,12+n)));
 const records=[];for(let i=12+n;i<p.length;i+=24)records.push(p.slice(i,i+24));
 return {meta,records};
}
export function summarize(records){
 let danced=0,listened=0,beats=0,candidates=0,clipped=0,eligible=0,rush=0,missingMs=0,segments=0,prev=null;
 let min=Infinity,max=0,sum=0,ms=0;
 for(const p of records){const a=decode(p);danced+=!!(a.flags&64);listened+=!!(a.flags&128);beats+=!!(a.flags&2);candidates+=!!(a.flags&1);clipped+=!!(a.flags&16);rush+=!!(a.flags&32);
  eligible+=a.bpm>=85&&a.bpm<=185&&a.confidence>=.745&&a.ratio>=.08;
  min=Math.min(min,a.rms);max=Math.max(max,a.rms);sum+=a.rms;
  if(prev && a.session===prev.session && a.ms>prev.ms){const dt=a.ms-prev.ms;ms+=dt;if(dt>40)missingMs+=dt-16;}
  else segments++;
  prev=a;
 }
 const n=records.length;return {frames:n,bytes:n*24,seconds:ms/1000,beats,candidates,rush,clippedFrames:clipped,
  dancePercent:n?100*danced/n:0,listenPercent:n?100*listened/n:0,eligiblePercent:n?100*eligible/n:0,rmsMin:n?min:0,rmsMean:n?sum/n:0,rmsMax:max,missingMs,segments};
}
