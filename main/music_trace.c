#include "music_trace.h"
#include <math.h>
#include <string.h>
static unsigned quant(float f,unsigned max){return !(f>0)?0:f>=max?max:(unsigned)(f+.5f);}
static void le16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void le32(uint8_t *p,uint32_t n){for(int i=0;i<4;i++)p[i]=n>>(8*i);}
/* Positive bfloat16 power: ~0.4% relative quantization, full float exponent
 * range. No log/sqrt on the capture path; the host performs aggregation. */
static unsigned power16(float f){uint32_t n;if(!(f>0))return 0;memcpy(&n,&f,4);if(n>=0x7f7f8000)return 0x7f7f;n+=0x7fff+((n>>16)&1);return n>>16;}
void music_trace_pack(uint8_t p[64],const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags,unsigned session,
                      const float power[16],uint32_t sequence,uint32_t cpu_us)
{
    le32(p,ms);le16(p+4,quant(a->raw_loud,65535));
    le16(p+6,quant(kick*131072.f,65535));le16(p+8,quant(mean*131072.f,65535));le16(p+10,quant(previous*131072.f,65535));
    le16(p+12,quant(a->bpm*10,65535));p[14]=quant(presence*255,255);p[15]=quant(a->bass_ratio*127.5f,255);
    p[16]=quant(a->tempo_conf*255,255);p[17]=quant(a->speech_depth*64,255);p[18]=flags;
    p[19]=quant(a->bass*255,255);p[20]=quant(a->mid*255,255);p[21]=quant(a->high*255,255);le16(p+22,session);
    le32(p+24,sequence);le16(p+28,cpu_us>65534?65534:cpu_us);le16(p+30,a->peak);
    for(int i=0;i<16;i++)le16(p+32+2*i,power16(power[i]));
}
static const char hex[]="0123456789abcdef";
size_t music_trace_line(char *out,const uint8_t *p,unsigned count)
{
    if(!count||count>MUSIC_TRACE_BATCH)return 0;
    memcpy(out,"MC2:",4);size_t n=4;uint32_t crc=~0u;
    for(unsigned i=0;i<count*MUSIC_TRACE_BYTES;i++){
        out[n++]=hex[p[i]>>4];out[n++]=hex[p[i]&15];crc^=p[i];
        for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1));
    }
    crc=~crc;out[n++]=':';for(int i=7;i>=0;i--)out[n++]=hex[(crc>>(4*i))&15];out[n++]='\n';return n;
}
#ifndef AUDIO_ANALYSIS_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
static QueueHandle_t queue;
static bool usb_ready,benchmark_pending;
static atomic_uint control,lost,context,sequence,status_requested;
bool music_trace_take_benchmark(void){bool b=benchmark_pending;benchmark_pending=false;return b;}
/* Sole owner of capture protocol output. Preserve partial writes across retries;
 * an unplugged/paused host cannot block the analyser or grow memory usage. */
static void writer(void *unused)
{
    (void)unused;
    uint8_t records[MUSIC_TRACE_BATCH*MUSIC_TRACE_BYTES];char line[MUSIC_TRACE_LINE_BYTES];
    size_t size=0,offset=0;unsigned seen=~0u;uint32_t status_ms=0;
    for(;;){
        unsigned c=atomic_load(&control);
        if(c!=seen){
            /* A cancelled partial packet is terminated before the status line,
             * allowing host resynchronization with an explicit CRC failure. */
            offset=0;
            size=snprintf(line,sizeof line,"\nMC_CONFIG:fw=%s gain=%d rate=62.5 spectrum=raw_fft_power_bfloat16\nMC_SESSION:%u %s format=2 frame_ms=16 bytes=64 renderer=%s\n",
                esp_app_get_description()->version,CONFIG_EYES_AUDIO_GAIN_DB,c>>1,c&1?"start":"stop",c&1?"paused":"running");
            seen=c;status_ms=esp_timer_get_time()/1000;
        }
        if(offset<size){
            int n=usb_serial_jtag_write_bytes(line+offset,size-offset,0);
            if(n>0)offset+=(size_t)n;
            if(offset<size){vTaskDelay(pdMS_TO_TICKS(10));continue;}
            size=offset=0;
        }
        unsigned n=0;uint8_t p[MUSIC_TRACE_BYTES];
        while(n<MUSIC_TRACE_BATCH && xQueueReceive(queue,p,0)==pdTRUE){
            if((c&1) && (unsigned)(p[22]|p[23]<<8)==(c>>1))memcpy(records+(n++)*MUSIC_TRACE_BYTES,p,MUSIC_TRACE_BYTES);
        }
        if(n){size=music_trace_line(line,records,n);continue;}
        uint32_t now=esp_timer_get_time()/1000;
        unsigned requested=atomic_exchange(&status_requested,0);
        if(requested || ((c&1)&&now-status_ms>=1000)){
            status_ms=now;size=0;
            if(requested&2)size=snprintf(line,sizeof line,"MC_CONFIG:fw=%s gain=%d rate=62.5 spectrum=raw_fft_power_bfloat16\n",esp_app_get_description()->version,CONFIG_EYES_AUDIO_GAIN_DB);
            size+=snprintf(line+size,sizeof line-size,"MC_STATE:%s session=%u frames=%u lost=%u renderer=%s\n",c&1?"recording":"idle",c>>1,
                atomic_load(&sequence),atomic_load(&lost),c&1?"paused":"running");continue;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t music_trace_enable(bool on)
{
    unsigned old=atomic_load(&control);
    if(on==(bool)(old&1)){atomic_fetch_or(&status_requested,2);return ESP_OK;}
    if(on&&!usb_ready)return ESP_ERR_INVALID_STATE;
    if(on&&!queue){
        queue=xQueueCreate(128,MUSIC_TRACE_BYTES);if(!queue)return ESP_ERR_NO_MEM;
        if(xTaskCreatePinnedToCore(writer,"music_trace",4096,NULL,3,NULL,1)!=pdPASS){vQueueDelete(queue);queue=NULL;return ESP_ERR_NO_MEM;}
    }
    if(on){atomic_store(&sequence,0);atomic_store(&lost,0);}
    atomic_store(&control,on?((((old>>1)+1)&65535)<<1)|1:(old&~1u));
    return ESP_OK;
}
void music_trace_init(void)
{
    usb_serial_jtag_driver_config_t cfg={.tx_buffer_size=4096,.rx_buffer_size=256};
    esp_err_t err=usb_serial_jtag_driver_install(&cfg);
    if(err!=ESP_OK){ESP_LOGW("music_trace","USB control unavailable: %s",esp_err_to_name(err));return;}
    usb_serial_jtag_vfs_use_driver();usb_ready=true;
}
void music_trace_poll(void)
{
    static char command[32];static unsigned used;static bool overflow;
    if(!usb_ready)return;
    char bytes[32];int n=usb_serial_jtag_read_bytes(bytes,sizeof bytes,0);
    for(int i=0;i<n;i++){
        char c=bytes[i];
        if(c=='\n'||c=='\r'){
            command[used]=0;
            if(!overflow){
                if(!strcmp(command,"MC_START"))music_trace_enable(true);
                else if(!strcmp(command,"MC_BENCH")&&!music_trace_active())benchmark_pending=true;
                else if(!strcmp(command,"MC_STOP"))music_trace_enable(false);
                else if(!strcmp(command,"MC_PING"))atomic_fetch_or(&status_requested,1);
            }
            used=0;overflow=false;
        }else if(used<sizeof command-1)command[used++]=c;else overflow=true;
    }
}
void music_trace_context(bool dancing,bool listening){atomic_store(&context,(dancing?64u:0)|(listening?128u:0));}
bool music_trace_active(void){return atomic_load(&control)&1;}
void music_trace_offer(const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags,const float power[16],uint32_t cpu_us)
{
    unsigned c=atomic_load(&control);if(!(c&1))return;
    uint8_t p[MUSIC_TRACE_BYTES];music_trace_pack(p,a,ms,kick,mean,previous,presence,flags|atomic_load(&context),c>>1,power,atomic_fetch_add(&sequence,1),cpu_us);
    if(xQueueSend(queue,p,0)!=pdTRUE)atomic_fetch_add(&lost,1);
}
#endif
