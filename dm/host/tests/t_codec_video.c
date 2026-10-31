// The Switch 2's video decoder API as the host serves it (hle_codec_video.c), without a guest: the buffer protocol, results, sync handles, sync point events and the
// readback, on a synthetic 16x16 red VP9 key frame (the one t_movie_vpx.c decodes); no game data. The SystemEvent functions are stubs that record each event's state.
#include "../hle.h"
#undef HLE_FORCE
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#include "../hle_movie.c"
#include "../hle_codec_video.c"
#include <assert.h>

uint64_t now_ns(void) { return 1; }
__thread _Atomic int dm_host_work;   // hle_thread.c
static struct { void *ev; int bound, set; } g_ev[16];
static int ev_slot(void *e) { for (int i = 0; i < 16; i++) if (g_ev[i].ev == e) return i; for (int i = 0; i < 16; i++) if (!g_ev[i].ev) { g_ev[i].ev = e; return i; } abort(); }
void hle_bind_system_event(void *e, int autoclear) { (void)autoclear; g_ev[ev_slot(e)].bound = 1; }
void hle_signal_system_event(void *e) { g_ev[ev_slot(e)].set = 1; }
void hle_clear_system_event(void *e) { g_ev[ev_slot(e)].set = 0; }
static int ev_set(void *e) { return g_ev[ev_slot(e)].set; }
void *guest_sym(const char *n) { (void)n; return NULL; }

static const uint8_t vp9[] = {
    0x82,0x49,0x83,0x42,0x00,0x00,0xf0,0x00,0xf6,0x00,0x38,0x24,0x1c,0x18,0x4a,0x00,
    0x00,0x30,0x60,0x00,0x00,0x10,0xbf,0xff,0xf7,0x1d,0xaf,0xff,0xff,0xff,0x5f,0xdf,
    0xff,0xff,0xff,0xf2,0x2a,0xc0,0x00,
};

int main(void) {
    _Alignas(16) static uint8_t dec[0x400], pool[3][0x40], spt[0x80], mem[16 * 16 * 4];
    uint64_t size, align, id; uint32_t idx; cv_sync sync; uint16_t w, h, cw, ch; int64_t ts; bool eos;

    // Pools: one picture each, NV12 (format 3, as Bananza asks)
    cv_pref pref = h_cv_pref(16, 16, 3, 0);
    assert(!h_cv_poolsize(&size, &align, &pref, 1) && size >= 16 * 16 * 4 && align == 0x1000);
    for (int i = 0; i < 3; i++) assert(!h_cv_pool(pool[i], mem, size, &pref, 1, 0) && pool_of(pool[i]));

    // Decoder: the submission-ready event is bound in its type and signalled
    assert(!h_cv_vp9init2(dec, 0x10000, 16, 16, 1, 1, 3));
    cv_dec *d = cv_of(dec); assert(d && d->vpx_ok);
    void *ev = NULL; assert(!h_cv_vp9event(&ev, dec) && ev == dec + CV_EVENT_OFF && g_ev[ev_slot(ev)].bound && ev_set(ev));

    // Registering makes a buffer acquirable at once, its sync handle signalled; an empty queue answers 3604
    for (uint32_t i = 0; i < 2; i++) assert(!h_cv_vp9reg(dec, i, pool[i], 0));
    for (uint32_t i = 0; i < 2; i++) { assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == i && sync.magic == CV_SYNC && sync.gen == 0 && h_gs_trywait(&sync)); }
    assert(h_cv_vp9acq(&idx, &sync, dec) == CV_NONE);
    assert(h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 0) == CV_NONE);

    // Released: acquirable again with a new handle that signals once a picture is in the buffer; the picture's properties and its NV12 / RGBA readback
    h_cv_vp9rel(dec, 0, sync);
    assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 0 && sync.gen == 1 && !h_gs_trywait(&sync));
    assert(h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 0) == CV_NONE);
    assert(!h_cv_vp9submit(&id, dec, vp9, sizeof vp9, 1000, false) && id == 1);
    assert(h_gs_trywait(&sync));
    assert(!h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 0) && w == 16 && h == 16 && cw == 16 && ch == 16 && ts == 1000 && !eos);
    uint8_t nv12[16 * 16 * 3 / 2], rgba[16 * 16 * 4];
    assert(!h_cv_nv12(pool[0], 0, 0, 0, 16, 16, nv12, 16, 256, nv12 + 256, 16, 128));
    for (int i = 0; i < 256; i++) assert(abs(nv12[i] - 81) <= 3);
    for (int i = 256; i < 384; i += 2) { assert(abs(nv12[i] - 90) <= 3); assert(abs(nv12[i + 1] - 240) <= 3); }
    uint8_t y[256], uv[128];   // separate planes
    assert(!h_cv_nv12(pool[0], 0, 0, 0, 16, 16, y, 16, 256, uv, 16, 128) && !memcmp(y, nv12, 256) && !memcmp(uv, nv12 + 256, 128));
    assert(!h_cv_rgba(pool[0], 0, 0, 0, 16, 16, rgba, 64, sizeof rgba));
    for (int i = 0; i < 1024; i += 4) assert(rgba[i] >= 250 && rgba[i + 1] <= 5 && rgba[i + 2] <= 5 && rgba[i + 3] == 255);

    // No released buffer: pictures wait (4), then packets are refused (3603) and the event is cleared; a release takes the oldest and signals again
    for (int i = 0; i < CV_HOLD; i++) assert(!h_cv_vp9submit(&id, dec, vp9, sizeof vp9, 2000 + i, false));
    assert(d->nheld == CV_HOLD && !ev_set(ev));
    assert(h_cv_vp9submit(&id, dec, vp9, sizeof vp9, 9999, false) == CV_BUSY);
    h_cv_vp9rel(dec, 1, sync);
    assert(d->nheld == CV_HOLD - 1 && ev_set(ev));
    assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 1 && h_gs_trywait(&sync));
    assert(!h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 1) && ts == 2000);

    // Sync point event: armed on an unfilled buffer, signalled when the picture lands; one armed on a filled buffer is signalled at once
    assert(!h_spt_create(spt) && g_ev[ev_slot(spt)].bound);
    h_cv_vp9rel(dec, 0, sync);   // buffer 0 takes the next waiting picture at once
    assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 0);
    assert(!h_spt_request(spt, &sync) && ev_set(spt) && !SPT(spt)->armed);
    assert(!h_cv_vp9reg(dec, 2, pool[2], 0) && !h_cv_vp9acq(&idx, &sync, dec) && idx == 2);
    for (int i = 0; i < 2; i++) { h_cv_vp9rel(dec, (uint32_t)i, sync); assert(!h_cv_vp9acq(&idx, &sync, dec)); }   // the last two waiting pictures go out
    h_cv_vp9rel(dec, 2, sync); assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 2 && !h_gs_trywait(&sync));
    assert(!h_spt_request(spt, &sync) && !ev_set(spt) && SPT(spt)->armed);
    assert(!h_cv_vp9submit(&id, dec, vp9, sizeof vp9, 3000, false) && ev_set(spt) && !SPT(spt)->armed && h_gs_trywait(&sync));

    // End of stream with no data: the next released buffer carries it, with no picture
    assert(!h_cv_vp9submit(&id, dec, NULL, 0, 0, true) && d->eos_pending);
    h_cv_vp9rel(dec, 2, sync); assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 2 && h_gs_trywait(&sync));
    assert(!h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 2) && eos && w == 0 && h == 0);

    // A pool finalized while its buffer is still registered: the buffer keeps working, its picture has nowhere to go (ASan checks no freed pool is written)
    h_cv_poolfin(pool[1]); assert(!pool_of(pool[1]) && d->buf[1].pool == NULL);
    h_cv_vp9rel(dec, 1, sync); assert(!h_cv_vp9acq(&idx, &sync, dec) && idx == 1 && !h_gs_trywait(&sync));
    assert(!h_cv_vp9submit(&id, dec, vp9, sizeof vp9, 4000, false) && h_gs_trywait(&sync));
    assert(!h_cv_vp9props(&w, &h, &cw, &ch, &ts, &eos, dec, 1) && ts == 4000 && w == 16);
    // A flush drops the waiting pictures and restarts the decoder; finalize frees it
    assert(!h_cv_vp9flush(dec) && d->nheld == 0 && d->vpx_ok && ev_set(ev));
    assert(!h_cv_vp9fin(dec) && !cv_of(dec));
    for (int i = 0; i < 3; i += 2) { h_cv_poolfin(pool[i]); assert(!pool_of(pool[i])); }
    printf("t_codec_video: all passed\n");
    return 0;
}
