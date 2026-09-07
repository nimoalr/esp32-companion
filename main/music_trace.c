#include "music_trace.h"
#include <math.h>
static unsigned quant(float f,unsigned max){return !(f>0)?0:f>=max?max:(unsigned)(f+.5f);}
static void le16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
void music_trace_pack(uint8_t p[24],const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags,unsigned session)
{
    for(int i=0;i<4;i++)p[i]=ms>>(i*8);
    le16(p+4,quant(a->raw_loud,65535));
    le16(p+6,quant(kick*131072.f,65535));
    le16(p+8,quant(mean*131072.f,65535));
    le16(p+10,quant(previous*131072.f,65535));
    le16(p+12,quant(a->bpm*10,65535));
    p[14]=quant(presence*255,255);p[15]=quant(a->bass_ratio*127.5f,255);
    p[16]=quant(a->tempo_conf*255,255);p[17]=quant(a->speech_depth*64,255);
    p[18]=flags;p[19]=quant(a->bass*255,255);p[20]=quant(a->mid*255,255);p[21]=quant(a->high*255,255);
    le16(p+22,session);
}
#ifndef AUDIO_ANALYSIS_HOST
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
static QueueHandle_t queue;
static bool usb_ready;
/* Producer reads one atomic control word: session << 1 | enabled. */
static atomic_uint control, lost, context;
static const char hex[]="0123456789abcdef";
static void writer(void *unused)
{
    (void)unused;
    uint8_t p[24];char line[4+16*48+1];
    int fd=open("/dev/secondary",O_WRONLY|O_NONBLOCK);
    memcpy(line,"MC1:",4);
    for(;;) {
        if(xQueueReceive(queue,p,portMAX_DELAY)!=pdTRUE)continue;
        int n=4;
        do {
            for(int j=0;j<24;j++){line[n++]=hex[p[j]>>4];line[n++]=hex[p[j]&15];}
        }while(n<4+16*48 && xQueueReceive(queue,p,0)==pdTRUE);
        line[n++]='\n';
        /* USB only: avoid copying the feature stream through the 115200 UART
         * and holding the global log lock during a long text packet. */
        if(fd>=0)write(fd,line,n);
        unsigned dropped=atomic_exchange(&lost,0);
        if(dropped)ESP_LOGW("music_trace","MC_LOST:%u",dropped);
        vTaskDelay(pdMS_TO_TICKS(100)); /* batching; never blocks the analyser */
    }
}
esp_err_t music_trace_enable(bool on)
{
    unsigned old=atomic_load(&control);
    if(on==(bool)(old&1))return ESP_OK;
    if(on && !queue) {
        queue=xQueueCreate(128,24);
        if(!queue)return ESP_ERR_NO_MEM;
        if(xTaskCreatePinnedToCore(writer,"music_trace",3072,NULL,1,NULL,0)!=pdPASS) {
            vQueueDelete(queue);queue=NULL;return ESP_ERR_NO_MEM;
        }
    }
    unsigned next=on?((((old>>1)+1)&65535)<<1)|1:(old&~1u);
    atomic_store(&control,next);
    if(on)ESP_LOGI("music_trace","MC_CONFIG:fw=%s gain=%d",esp_app_get_description()->version,CONFIG_EYES_AUDIO_GAIN_DB);
    ESP_LOGI("music_trace","MC_SESSION:%u %s format=1 frame_ms=16 bytes=24",next>>1,on?"start":"stop");
    return ESP_OK;
}
void music_trace_init(void)
{
    usb_serial_jtag_driver_config_t cfg={.tx_buffer_size=2048,.rx_buffer_size=256};
    esp_err_t err=usb_serial_jtag_driver_install(&cfg);
    if(err!=ESP_OK){ESP_LOGW("music_trace","USB control unavailable: %s",esp_err_to_name(err));return;}
    usb_serial_jtag_vfs_use_driver();usb_ready=true;
}
void music_trace_poll(void)
{
    /* Bounded RX ring read; no waits on the render thread. */
    static char command[32];static unsigned used;static bool overflow;
    static uint32_t previous;
    uint32_t now=esp_timer_get_time()/1000;
    if(now-previous<100)return;
    previous=now;
    if(!usb_ready)return;
    char bytes[32];int n=usb_serial_jtag_read_bytes(bytes,sizeof bytes,0);
    for(int i=0;i<n;i++) {
        char c=bytes[i];
        if(c=='\n'||c=='\r') {
            command[used]=0;
            if(!overflow) {
                if(!strcmp(command,"MC_START"))music_trace_enable(true);
                else if(!strcmp(command,"MC_STOP"))music_trace_enable(false);
                else if(!strcmp(command,"MC_PING"))ESP_LOGI("music_trace","MC_STATE:%s",music_trace_active()?"recording":"idle");
            }
            used=0;overflow=false;
        } else if(used<sizeof command-1)command[used++]=c;else overflow=true;
    }
}
void music_trace_context(bool dancing,bool listening){atomic_store(&context,(dancing?64u:0)|(listening?128u:0));}
bool music_trace_active(void){return atomic_load(&control)&1;}
void music_trace_offer(const audio_features_t *a,uint32_t ms,float kick,float mean,float previous,float presence,unsigned flags)
{
    unsigned c=atomic_load(&control);if(!(c&1))return;
    uint8_t p[24];music_trace_pack(p,a,ms,kick,mean,previous,presence,flags|atomic_load(&context),c>>1);
    if(xQueueSend(queue,p,0)!=pdTRUE)atomic_fetch_add(&lost,1);
}
#endif
