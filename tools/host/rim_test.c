/* Physical-disc visibility and interruption contracts for the edge choreography. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "anim.h"
uint32_t esp_random(void) { return 0x12345678u; }
static uint16_t band[466*16];
static eyes_t eyes;
static anim_sm_t sm;
static raster_shape_t shapes[2];
static uint32_t now;
static void start(anim_id_t id)
{
    now=0; eyes_init(&eyes,now); eyes_set_hotspot(&eyes,true);
    anim_init(&sm,&eyes,now); anim_set(&sm,&eyes,id,now);
}
static void advance(uint32_t end)
{
    while (now<end) {
        now+=end-now<16 ? end-now : 16;
        anim_update(&sm,&eyes,now); eyes_update(&eyes,now,shapes);
    }
}
static int visible_pixels(int eye)
{
    int n=0;
    for (int y=0; y<466; y+=16) {
        const int rows=466-y<16 ? 466-y : 16;
        raster_band(band,0,y,466,rows,&shapes[eye],1);
        for (int yy=0; yy<rows; yy++) for (int x=0; x<466; x++) {
            const int dx=2*x+1-466, dy=2*(y+yy)+1-466;
            if (dx*dx+dy*dy<=466*466 && band[yy*466+x]) n++;
        }
    }
    return n;
}
int main(void)
{
    assert(ANIM_CAUTIOUS_PEEK==41 && ANIM_COUNT==51);
    start(ANIM_CAUTIOUS_PEEK);
    advance(1000); assert(!visible_pixels(0) && !visible_pixels(1));
    advance(2000); assert(visible_pixels(0)>200 && !visible_pixels(1));
    advance(3300); assert(visible_pixels(0)>200 && visible_pixels(1)>200);
    advance(4100); assert(!visible_pixels(0) && !visible_pixels(1));
    advance(6000); assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
    start(ANIM_HIDE_RELOCATE);
    advance(1300); assert(!visible_pixels(0) && !visible_pixels(1));
    advance(2500); assert(visible_pixels(0)>100 && visible_pixels(1)>100);
    start(ANIM_JACKPOT_ESCAPE);
    advance(5200); assert(!visible_pixels(0) && !visible_pixels(1));
    advance(5650); assert(!visible_pixels(0) && !visible_pixels(1));
    advance(7300); assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
    start(ANIM_SECRET_OBSERVER);
    advance(2200); assert(visible_pixels(0)>100 && visible_pixels(1)>100);
    eyes_set_attention(&eyes,true,440,233); advance(2450);
    assert(sm.rim_retreat_ms && !visible_pixels(0) && !visible_pixels(1));
    advance(3200); assert(!visible_pixels(0) && !visible_pixels(1));
    eyes_set_attention(&eyes,false,0,0); advance(6200);
    assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
    advance(8100); assert(!sm.rim_retreat_ms && !eyes.idle.blink_interval_scale);
    start(ANIM_SECRET_OBSERVER); eyes_set_face_angle(&eyes,90); advance(2200);
    eyes_set_attention(&eyes,true,233,440); advance(2450);
    assert(sm.rim_retreat_ms && !visible_pixels(0) && !visible_pixels(1));
    start(ANIM_AROUND_BEND); advance(2500);
    const int32_t x=shapes[0].cx, y=shapes[0].cy;
    anim_set(&sm,&eyes,ANIM_NEUTRAL,now);
    eyes_update(&eyes,now,shapes);
    assert(abs(shapes[0].cx-x)<Q16(1) && abs(shapes[0].cy-y)<Q16(1));
    advance(3000); assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
    /* Every action returns from offstage, and a second loop stays live. */
    for (int id=ANIM_CAUTIOUS_PEEK; id<ANIM_COUNT; id++) {
        start((anim_id_t)id);
        advance(7600); assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
        advance(15600); assert(visible_pixels(0)>5000 && visible_pixels(1)>5000);
    }
    puts("PASS: empty-screen holds; staggered peeks; relocation; jackpot return; touch retreat; orbit interruption; all ten loop recoveries");
    return 0;
}
