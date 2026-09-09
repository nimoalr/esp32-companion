// Shared portable limits. Reserve the full metadata allowance while capturing,
// so finishing a run (summaries/markers) still leaves an exportable notebook.
export const MAX_SESSION_BYTES=1000000000;
export const MAX_METADATA_BYTES=16000000;
export const MAX_SESSION_FRAMES=500000;
export const MAX_RUN_BYTES=128000000;
export function captureCapacity({frames,recordBytes,nextBytes,stemBytes=0,runFrames=0}){
 const width=Math.max(recordBytes,nextBytes);
 const sessionFrames=Math.max(0,Math.min(MAX_SESSION_FRAMES-frames,
  Math.floor((MAX_SESSION_BYTES-MAX_METADATA_BYTES-24-stemBytes)/width)-frames));
 const runRemaining=Math.max(0,Math.floor(MAX_RUN_BYTES/nextBytes)-runFrames);
 return {remaining:Math.min(sessionFrames,runRemaining),sessionFrames,runRemaining,
  reason:sessionFrames<=0?'session':runRemaining<=0?'run':null};
}
