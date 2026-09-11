/* Render the real C face, with preview-only captions, to PPM and raw RGB video.
 * Run from tools/host after build.sh expression_preview. No display/ESP-IDF. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "anim.h"
#include "accessories.h"

uint32_t esp_random(void) { return 0x12345678u; }
#define W 466
#define CELL 233
#define FPS 20
typedef struct { eyes_t eyes; anim_sm_t anim; accessories_t acc; raster_shape_t shapes[2]; } scene_t;
static scene_t scenes[10];
static uint16_t fb[W * W], band[W * 32];
static unsigned char canvas[CELL * 6 * (CELL+24) * ((ANIM_COUNT+5)/6) * 3];

static FILE *open_output(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    return f;
}

static void init(scene_t *s, anim_id_t id)
{
    memset(s, 0, sizeof *s);
    eyes_init(&s->eyes, 0);
    eyes_set_hotspot(&s->eyes, true);
    anim_init(&s->anim, &s->eyes, 0);
    anim_set(&s->anim, &s->eyes, id, 0);
    acc_init(&s->acc, 138, 328, 233);
    acc_set_knocked_out(&s->acc, id == ANIM_KNOCKED_OUT, 0);
    acc_set_zz(&s->acc, id == ANIM_SLEEPING, 0);
    /* Fixed random seed; idle still runs in the moving previews. */
}

static void tick(scene_t *s, uint32_t t)
{
    if (s->anim.id == ANIM_DANCE) {
        const float phase = (float)(t % 500) / 500.f;
        audio_features_t af = { .active = true, .loud = 0.65f, .bass = 0.3f + 0.5f * expf(-phase * 5.f),
                                .regularity = 0.9f, .beat_count = t / 500 + 1, .last_beat_ms = t - t % 500 };
        anim_set_audio(&s->anim, &af);
    }
    anim_update(&s->anim, &s->eyes, t);
    eyes_update(&s->eyes, t, s->shapes);
    acc_rect_t dirty[ACC_MAX_DIRTY];
    acc_update(&s->acc, t, dirty);
}

/* Only the before panel uses the old 30 px / 14 px fixed-orange crosses. */
static void legacy_crosses(const gfx_band_t *b)
{
    for (int e = 0; e < 2; e++) {
        const int x = e ? 328 : 138;
        gfx_line(b, x - 30, 203, x + 30, 263, 14, gfx_rgb(255, 140, 0));
        gfx_line(b, x - 30, 263, x + 30, 203, 14, gfx_rgb(255, 140, 0));
    }
}

static void draw(scene_t *s, uint32_t t, const char *caption, bool old_ko)
{
    raster_shape_t shapes[2];
    memcpy(shapes, s->shapes, sizeof shapes);
    if (old_ko) shapes[0].visible = shapes[1].visible = false;
    for (int y = 0; y < W; y += 32) {
        const int rows = W - y < 32 ? W - y : 32;
        raster_band(band, 0, y, W, rows, shapes, 2);
        const gfx_band_t gb = { band, 0, y, W, rows };
        acc_paint(&s->acc, &gb, t);
        if (old_ko) legacy_crosses(&gb);
        /* The real AMOLED is a disc, not a square framebuffer. Reproduce its
         * physical crop so offstage entrances are honest in host previews. */
        for (int yy = 0; yy < rows; yy++) for (int x = 0; x < W; x++) {
            const int dx = 2*x+1-W, dy = 2*(y+yy)+1-W;
            if (dx*dx+dy*dy > W*W) band[yy*W+x] = 0;
        }
        /* Captions and subtle disc boundary are host annotations, not new on-device UI. */
        gfx_ring(&gb, 233, 233, 232, 1, 0, 360, gfx_rgb(35, 39, 45));
        const int x = (W - gfx_text_width(&font_spleen_16x32, caption)) / 2;
        gfx_text(&gb, &font_spleen_16x32, x, 382, caption, gfx_rgb(190, 196, 205), GFX_TRANSPARENT);
        memcpy(fb + y * W, band, (size_t)W * rows * 2);
    }
}

static void put_cell_ex(int index, int cols, int row_height)
{
    const int ox = index % cols * CELL, oy = index / cols * row_height;
    for (int y = 0; y < CELL; y++) for (int x = 0; x < CELL; x++) {
        int rgb[3] = {0};
        for (int yy = 0; yy < 2; yy++) for (int xx = 0; xx < 2; xx++) {
            const uint16_t s = fb[(2*y+yy)*W+2*x+xx], c = (uint16_t)(s >> 8 | s << 8);
            rgb[0] += ((c >> 11) & 31) * 255 / 31;
            rgb[1] += ((c >> 5) & 63) * 255 / 63;
            rgb[2] += (c & 31) * 255 / 31;
        }
        for (int c = 0; c < 3; c++) canvas[((oy+y)*cols*CELL+ox+x)*3+c] = (unsigned char)(rgb[c]/4);
    }
}

static void put_cell(int index, int cols) { put_cell_ex(index,cols,CELL); }

/* Rim scenes need the bottom of the circle for acting, so captions sit below it. */
static void rim_cell(int index, int cols, const char *caption)
{
    put_cell_ex(index,cols,CELL+24);
    memset(fb,0,W*48*sizeof *fb);
    const gfx_band_t label = {fb,0,0,W,48};
    gfx_text(&label,&font_spleen_16x32,(W-gfx_text_width(&font_spleen_16x32,caption))/2,
             8,caption,gfx_rgb(190,196,205),GFX_TRANSPARENT);
    const int ox = index%cols*CELL, oy = index/cols*(CELL+24)+CELL;
    for (int y=0; y<24; y++) for (int x=0; x<CELL; x++) {
        const uint16_t a=fb[(2*y)*W+2*x], c=(uint16_t)(a>>8|a<<8);
        unsigned char *p=&canvas[((oy+y)*cols*CELL+ox+x)*3];
        p[0]=((c>>11)&31)*255/31; p[1]=((c>>5)&63)*255/63; p[2]=(c&31)*255/31;
    }
}

static void ppm_sized(const char *path, int width, int height)
{
    FILE *f = open_output(path);
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(canvas, 3, (size_t)width * height, f);
    fclose(f);
}

static void ppm(const char *path, int cols, int rows) { ppm_sized(path,cols*CELL,rows*CELL); }

int main(void)
{
    mkdir("out/character", 0755);
    memset(canvas, 0, sizeof canvas);
    for (int id = 0; id < ANIM_COUNT; id++) {
        scene_t *s = &scenes[0];
        init(s, (anim_id_t)id);
        s->eyes.idle.next_blink_ms = s->eyes.idle.next_dart_ms = 100000;
        uint32_t sample = id == ANIM_WINK ? 680 : id == ANIM_MISCHIEVOUS ? 1760 : id == ANIM_DOUBLE_TAKE ? 960 : 1400;
        if (id == ANIM_HEARTBREAK) sample = 2200;
        if (id == ANIM_HIGH_ROLLER) sample = 4500;
        if (id == ANIM_LOADING) sample = 1850;
        if (id == ANIM_SNEEZE) sample = 1290;
        if (id >= ANIM_CAUTIOUS_PEEK) {
            static const uint32_t rim_sample[] = {3100,2450,2100,1160,2100,3400,2400,3100,2300,4850};
            sample = rim_sample[id-ANIM_CAUTIOUS_PEEK];
        }
        for (uint32_t t = 0; t <= sample; t += 10) tick(s, t);
        draw(s, sample, "", false);
        rim_cell(id, 6, anim_name((anim_id_t)id));
    }
    ppm_sized("out/character/catalog.ppm", 6*CELL, (CELL+24)*((ANIM_COUNT + 5)/6));

    static const anim_id_t ids[] = { ANIM_SMUG, ANIM_SUSPICIOUS, ANIM_DETERMINED, ANIM_PLEADING,
                                    ANIM_MISCHIEVOUS, ANIM_EMBARRASSED, ANIM_RELIEVED, ANIM_DOUBLE_TAKE };
    for (int i = 0; i < 8; i++) init(&scenes[i], ids[i]);
    FILE *video = open_output("out/character/new.rgb");
    for (uint32_t frame = 0; frame < 6*60; frame++) {
        const uint32_t t = frame*1000/60;
        for (int i = 0; i < 8; i++) {
            tick(&scenes[i], t);
            if (frame % (60/FPS) == 0) {
                draw(&scenes[i], t, anim_name(ids[i]), false);
                put_cell(i, 4);
            }
        }
        if (frame % (60/FPS) == 0) fwrite(canvas, 3, 4 * 2 * CELL * CELL, video);
        if (t == 1750) ppm("out/character/new.ppm", 4, 2);
    }
    fclose(video);

    init(&scenes[0], ANIM_HIGH_ROLLER);
    video = open_output("out/character/high_roller.rgb");
    for (uint32_t frame = 0; frame < 7*60; frame++) {
        const uint32_t t = frame*1000/60;
        tick(&scenes[0], t);
        if (frame%3 == 0) {
            draw(&scenes[0], t, "HIGH ROLLER", false); put_cell(0,1);
            fwrite(canvas, 3, CELL*CELL, video);
        }
    }
    fclose(video);

    static const anim_id_t play[] = {ANIM_HEARTS, ANIM_HEARTBREAK, ANIM_HIGH_ROLLER, ANIM_NOD,
                                     ANIM_PEEKABOO, ANIM_LOADING, ANIM_BOOP, ANIM_SNEEZE};
    for (int i = 0; i < 8; i++) init(&scenes[i], play[i]);
    video = open_output("out/character/playful.rgb");
    for (uint32_t frame = 0; frame < 7*60; frame++) {
        const uint32_t t = frame*1000/60;
        for (int i = 0; i < 8; i++) {
            tick(&scenes[i], t);
            if (frame%3 == 0) { draw(&scenes[i], t, anim_name(play[i]), false); put_cell(i, 4); }
        }
        if (frame%3 == 0) fwrite(canvas, 3, 4*2*CELL*CELL, video);
        if (t == 2200) ppm("out/character/playful.ppm", 4, 2);
    }
    fclose(video);

    static const anim_id_t chain[] = {ANIM_SMUG, ANIM_HEARTS, ANIM_HEARTBREAK, ANIM_HIGH_ROLLER, ANIM_PEEKABOO, ANIM_NEUTRAL};
    init(&scenes[0], chain[0]);
    video = open_output("out/character/transitions.rgb");
    for (uint32_t frame = 0; frame < 18*60; frame++) {
        const uint32_t t = frame*1000/60;
        if (frame && frame%180 == 0) anim_set(&scenes[0].anim, &scenes[0].eyes, chain[frame/180], t);
        tick(&scenes[0], t);
        if (frame%3 == 0) {
            draw(&scenes[0], t, anim_name(scenes[0].anim.id), false); put_cell(0,1);
            fwrite(canvas, 3, CELL*CELL, video);
        }
    }
    fclose(video);

    /* Compare the old and new full reactions, with their real 8 s + 3 s holds.
     * Starts 1.5 s before KO; behavior integration is checked separately. */
    init(&scenes[0], ANIM_DIZZY); init(&scenes[1], ANIM_DIZZY);
    video = open_output("out/character/recovery.rgb");
    for (uint32_t frame = 0; frame < 14*60; frame++) {
        const uint32_t t = frame*1000/60;
        if (t == 1500 || t == 9500 || t == 12500) {
            for (int i = 0; i < 2; i++) {
                const anim_id_t id = t == 1500 ? (i ? ANIM_KNOCKED_OUT : ANIM_SLEEPING)
                                    : t == 9500 ? (i ? ANIM_RECOVERING : ANIM_SLEEPY) : ANIM_NEUTRAL;
                anim_set(&scenes[i].anim, &scenes[i].eyes, id, t);
                acc_set_knocked_out(&scenes[i].acc, t == 1500, t);
            }
        }
        for (int i = 0; i < 2; i++) {
            tick(&scenes[i], t);
            if (frame % (60/FPS) == 0) {
                draw(&scenes[i], t, i ? "AFTER" : "BEFORE", !i && t >= 1500 && t < 9500);
                put_cell(i, 2);
            }
        }
        if (frame % (60/FPS) == 0) fwrite(canvas, 3, 2 * CELL * CELL, video);
        if (t == 2400) ppm("out/character/knocked_out.ppm", 2, 1);
    }
    fclose(video);
    FILE *solo[10];
    for (int i=0; i<10; i++) {
        init(&scenes[i],(anim_id_t)(ANIM_CAUTIOUS_PEEK+i));
        char path[120]; snprintf(path,sizeof path,"out/character/%s.rgb",anim_name(scenes[i].anim.id));
        solo[i]=open_output(path);
    }
    video=open_output("out/character/rim.rgb");
    static unsigned char solo_canvas[CELL*(CELL+24)*3];
    for (uint32_t frame=0; frame<8*60; frame++) {
        const uint32_t t=frame*1000/60;
        for (int i=0; i<10; i++) {
            tick(&scenes[i],t);
            if (frame%3 == 0) {
                draw(&scenes[i],t,"",false);
                rim_cell(i,5,anim_name(scenes[i].anim.id));
                for (int y=0; y<CELL+24; y++)
                    memcpy(solo_canvas+y*CELL*3,
                           canvas+((i/5*(CELL+24)+y)*5*CELL+i%5*CELL)*3,CELL*3);
                fwrite(solo_canvas,3,CELL*(CELL+24),solo[i]);
            }
        }
        if (frame%3 == 0) fwrite(canvas,3,5*2*CELL*(CELL+24),video);
        if (t==2300) {
            FILE *still=open_output("out/character/rim.ppm");
            fprintf(still,"P6\n%d %d\n255\n",5*CELL,2*(CELL+24));
            fwrite(canvas,3,5*2*CELL*(CELL+24),still); fclose(still);
        }
    }
    fclose(video);
    for (int i=0; i<10; i++) fclose(solo[i]);

    printf("wrote %d-pose catalog and 20 fps attitude/playful/rim/transition/recovery previews\n", ANIM_COUNT);
    return 0;
}
