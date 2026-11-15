// Online two-player play: rollback netcode (see netplay.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "netplay.h"

#define MAGIC 0x504E4D54u   // "TMNP"
#define VERSION 1
#define RING 256            // frames of input history kept (must exceed NP_MAX_PREDICT + 64)
#define SLOTS (NP_MAX_PREDICT + 2)
#define CHUNK 1024          // state transfer piece
#define MAX_CHUNKS (NP_STATE_MAX / CHUNK)
#define CRC_EVERY 32
#define SEND_INPUTS 32      // inputs repeated in each packet at most (covers lost packets)

enum { PK_HELLO = 1, PK_REJECT, PK_WELCOME, PK_STATE, PK_STATE_ACK, PK_READY, PK_INPUT, PK_BYE };

struct Netplay {
  bool host;
  NpCallbacks cb;
  uint32_t rom_crc;
  NpSettings settings;
  int delay;
  NpStatus status;
  char msg[96];
  NpStats stats;
  uint8_t gen;              // session generation: bumped by each (re)sync, packets of other generations are ignored
  double now, last_recv, last_send_hs, started;

  // state transfer
  uint8_t* xfer; int xfer_size, nchunks;
  uint8_t acked[MAX_CHUNKS]; double sent_at[MAX_CHUNKS];   // host
  uint8_t got[MAX_CHUNKS]; int got_count;                  // client
  bool ready_seen;          // client: the host has started (an input packet arrived)

  // rollback
  int32_t frame;            // next frame to run
  int32_t local_last;       // last frame with local input
  int32_t remote_last;      // last frame up to which all remote input is known
  int32_t peer_ack;         // the other side has all our input up to here
  uint16_t in_local[RING], in_remote[RING], predicted[RING];
  int32_t remote_have[RING];   // frame number stored in in_remote[i] (or -1)
  int32_t rollback_to;      // earliest frame whose remote input turned out different (INT32_MAX = none)
  uint8_t* slot[SLOTS]; int slot_size[SLOTS]; int32_t slot_frame[SLOTS];
  uint32_t crc_val[RING]; int32_t crc_frame[RING];
  // the peer's latest final checksum, compared once ours for that frame is final too
  int32_t peer_crc_frame; uint32_t peer_crc;
  int32_t desync_frame;     // last frame reported as different (once per frame)
  // time sync
  double adv_local, adv_remote; int32_t peer_frame; double peer_frame_at; int stall_cooldown;
  uint32_t echo_ts; double echo_at;
};

// ---- little-endian packing
static uint8_t* p8(uint8_t* p, uint32_t v) { *p++ = (uint8_t)v; return p; }
static uint8_t* p16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t* p32(uint8_t* p, uint32_t v) { p = p16(p, v & 0xffff); return p16(p, v >> 16); }
static uint32_t g16(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t g32(const uint8_t* p) { return g16(p) | g16(p + 2) << 16; }

static void np_send(Netplay* np, const uint8_t* b, uint8_t* e) { np->cb.send(np->cb.ctx, b, (int)(e - b)); }
static void end(Netplay* np, const char* why) {
  if (np->status == NP_ENDED) return;
  np->status = NP_ENDED;
  snprintf(np->msg, sizeof np->msg, "%s", why);
}

Netplay* np_create(bool host, NpCallbacks cb, uint32_t rom_crc, const NpSettings* settings, int input_delay) {
  Netplay* np = calloc(1, sizeof *np);
  np->host = host; np->cb = cb; np->rom_crc = rom_crc;
  if (settings) np->settings = *settings;
  np->delay = input_delay < 0 ? 0 : input_delay > 6 ? 6 : input_delay;
  np->status = host ? NP_WAITING : NP_CONNECTING;
  np->xfer = malloc(NP_STATE_MAX);
  for (int i = 0; i < SLOTS; i++) np->slot[i] = malloc(NP_STATE_MAX);
  np->started = -1;
  return np;
}
void np_destroy(Netplay* np) {
  if (!np) return;
  free(np->xfer);
  for (int i = 0; i < SLOTS; i++) free(np->slot[i]);
  free(np);
}

// both sides: start playing from the state now loaded, at frame 0
static void start_running(Netplay* np) {
  np->status = NP_RUNNING;
  np->frame = 0; np->remote_last = -1; np->peer_ack = -1;
  np->local_last = np->delay - 1;    // the first frames (input delay) have no input
  memset(np->in_local, 0, sizeof np->in_local);
  memset(np->in_remote, 0, sizeof np->in_remote);
  for (int i = 0; i < RING; i++) { np->remote_have[i] = -1; np->crc_frame[i] = -1; }
  for (int i = 0; i < SLOTS; i++) np->slot_frame[i] = -1;
  np->rollback_to = INT32_MAX;
  np->peer_crc_frame = -1; np->desync_frame = -1;
  np->adv_local = np->adv_remote = 0; np->peer_frame = 0; np->peer_frame_at = np->now; np->stall_cooldown = 0;
  np->last_recv = np->now;
}

// host: snapshot the game and start sending it
static void host_begin_sync(Netplay* np) {
  np->gen++;
  np->xfer_size = np->cb.save(np->cb.ctx, np->xfer);
  np->nchunks = (np->xfer_size + CHUNK - 1) / CHUNK;
  memset(np->acked, 0, sizeof np->acked);
  for (int i = 0; i < np->nchunks; i++) np->sent_at[i] = -1;
  np->status = NP_SYNCING;
  np->last_send_hs = -1;
}

static void send_welcome(Netplay* np) {
  uint8_t b[64], *p = b;
  p = p8(p, PK_WELCOME); p = p8(p, np->gen); p = p32(p, (uint32_t)np->xfer_size); p = p8(p, (uint8_t)np->delay);
  p = p8(p, np->settings.overclock); p = p8(p, np->settings.x_lives); p = p8(p, np->settings.x_health);
  p = p8(p, np->settings.x_stage); p = p8(p, np->settings.unlocked);
  np_send(np, b, p);
}

static void host_send_chunks(Netplay* np) {
  if (np->now - np->last_send_hs > 0.25) { send_welcome(np); np->last_send_hs = np->now; }
  int budget = 48;
  for (int i = 0; i < np->nchunks && budget > 0; i++) {
    if (np->acked[i] || (np->sent_at[i] >= 0 && np->now - np->sent_at[i] < 0.3)) continue;
    uint8_t b[16 + CHUNK], *p = b;
    int len = np->xfer_size - i * CHUNK; if (len > CHUNK) len = CHUNK;
    p = p8(p, PK_STATE); p = p8(p, np->gen); p = p16(p, (uint32_t)i);
    memcpy(p, np->xfer + i * CHUNK, len); p += len;
    np_send(np, b, p);
    np->sent_at[i] = np->now; budget--;
  }
}

// ---- input packets
static uint16_t remote_input(Netplay* np, int32_t f) {
  if (f <= np->remote_last) return np->in_remote[f % RING];
  return np->remote_last >= 0 ? np->in_remote[np->remote_last % RING] : 0;   // prediction: the last one known
}
static int32_t final_crc_frame(Netplay* np) {   // newest frame whose checksum can no longer change
  int32_t f = np->remote_last + 1;                // all input before it is known...
  if (f > np->frame - 1) f = np->frame - 1;       // ...and it has been run
  if (f < 0) return -1;
  return f - f % CRC_EVERY;
}
static void send_input(Netplay* np) {
  uint8_t b[64 + SEND_INPUTS * 2], *p = b;
  int32_t first = np->peer_ack + 1;   // everything the other side doesn't have yet, oldest first
  int n = np->local_last - first + 1;
  if (n < 0) n = 0;
  if (n > SEND_INPUTS) n = SEND_INPUTS;
  int32_t cf = final_crc_frame(np);
  bool have_crc = cf >= 0 && np->crc_frame[(cf / CRC_EVERY) % RING] == cf;
  p = p8(p, PK_INPUT); p = p8(p, np->gen);
  p = p32(p, (uint32_t)np->frame); p = p32(p, (uint32_t)np->remote_last);
  int adv = (int)(np->adv_local * 4); if (adv > 127) adv = 127; if (adv < -127) adv = -127;
  p = p8(p, (uint8_t)(int8_t)adv);
  p = p32(p, (uint32_t)(np->now * 1000)); p = p32(p, np->echo_ts + (uint32_t)((np->now - np->echo_at) * 1000));
  p = p32(p, have_crc ? (uint32_t)cf : 0xffffffffu); p = p32(p, have_crc ? np->crc_val[(cf / CRC_EVERY) % RING] : 0);
  p = p32(p, (uint32_t)first); p = p8(p, (uint8_t)n);
  for (int i = 0; i < n; i++) p = p16(p, np->in_local[(first + i) % RING]);
  np_send(np, b, p);
}

static void check_crc(Netplay* np) {
  if (np->peer_crc_frame < 0) return;
  int32_t f = np->peer_crc_frame;
  if (f > final_crc_frame(np)) return;              // ours isn't final yet
  int i = (f / CRC_EVERY) % RING;
  if (np->crc_frame[i] != f) { np->peer_crc_frame = -1; return; }   // too old, gone
  if (np->crc_val[i] != np->peer_crc && f != np->desync_frame) {
    np->desync_frame = f;
    np->stats.desyncs++;
    fprintf(stderr, "netplay: desync at frame %d (%08X vs %08X)\n", f, np->crc_val[i], np->peer_crc);
    if (np->host) { np->stats.resyncs++; host_begin_sync(np); }
  }
  np->peer_crc_frame = -1;
}

static void got_input(Netplay* np, const uint8_t* p, int len) {
  if (len < 32) return;
  int32_t pf = (int32_t)g32(p + 2), pack = (int32_t)g32(p + 6);
  int8_t padv = (int8_t)p[10];
  uint32_t ts = g32(p + 11), echo = g32(p + 15);
  int32_t cf = (int32_t)g32(p + 19); uint32_t crc = g32(p + 23);
  int32_t first = (int32_t)g32(p + 27); int n = p[31];
  if (len < 32 + n * 2) return;
  np->echo_ts = ts; np->echo_at = np->now;
  int rtt = (int)((uint32_t)(np->now * 1000) - echo);
  if (rtt >= 0 && rtt < 5000) np->stats.ping_ms = np->stats.ping_ms ? (np->stats.ping_ms * 7 + rtt) / 8 : rtt;
  if (pack > np->peer_ack) np->peer_ack = pack;
  if (pf > np->peer_frame) { np->peer_frame = pf; np->peer_frame_at = np->now; }
  np->adv_remote = np->adv_remote * 0.9 + (padv / 4.0) * 0.1;
  for (int i = 0; i < n; i++) {
    int32_t f = first + i;
    if (f <= np->remote_last || f < np->frame - RING + 8) continue;
    uint16_t v = (uint16_t)g16(p + 32 + i * 2);
    np->in_remote[f % RING] = v; np->remote_have[f % RING] = f;
    if (f < np->frame && v != np->predicted[f % RING] && f < np->rollback_to) np->rollback_to = f;
  }
  while (np->remote_have[(np->remote_last + 1) % RING] == np->remote_last + 1) np->remote_last++;
  if (cf != (int32_t)0xffffffff && cf > np->peer_crc_frame) { np->peer_crc_frame = cf; np->peer_crc = crc; }
}

void np_receive(Netplay* np, const uint8_t* p, int len, double now) {
  np->now = now;
  if (len < 2 || np->status == NP_ENDED) return;
  np->last_recv = now;
  switch (p[0]) {
    case PK_HELLO:   // host: someone wants to join
      if (!np->host || len < 10 || g32(p + 1) != MAGIC) return;
      if (p[5] != VERSION || g32(p + 6) != np->rom_crc) {
        uint8_t b[4] = {PK_REJECT, p[5] != VERSION ? 1 : 2};
        np->cb.send(np->cb.ctx, b, 2);
        return;
      }
      if (np->status == NP_WAITING) host_begin_sync(np);
      else if (np->status == NP_SYNCING) send_welcome(np);
      break;
    case PK_REJECT:
      if (!np->host) end(np, p[1] == 1 ? "THE HOST HAS ANOTHER VERSION" : "THE HOST HAS ANOTHER ROM");
      break;
    case PK_WELCOME:   // client: the host's state follows
      if (np->host || len < 12) return;
      if (np->status == NP_CONNECTING || (p[1] != np->gen && (np->status == NP_RUNNING || np->status == NP_SYNCING))) {
        if (np->status == NP_RUNNING) np->stats.resyncs++;
        np->gen = p[1];
        np->xfer_size = (int)g32(p + 2);
        if (np->xfer_size <= 0 || np->xfer_size > NP_STATE_MAX) { end(np, "BAD STATE FROM THE HOST"); return; }
        np->nchunks = (np->xfer_size + CHUNK - 1) / CHUNK;
        np->delay = p[6];
        np->settings.overclock = p[7]; np->settings.x_lives = p[8]; np->settings.x_health = p[9];
        np->settings.x_stage = p[10]; np->settings.unlocked = p[11];
        memset(np->got, 0, sizeof np->got); np->got_count = 0; np->ready_seen = false;
        np->status = NP_SYNCING;
      }
      break;
    case PK_STATE: {
      if (np->host || np->status != NP_SYNCING || p[1] != np->gen || len < 4) return;
      int i = (int)g16(p + 2), want = np->xfer_size - i * CHUNK; if (want > CHUNK) want = CHUNK;
      if (i >= np->nchunks || len - 4 != want) return;
      if (!np->got[i]) { memcpy(np->xfer + i * CHUNK, p + 4, want); np->got[i] = 1; np->got_count++; }
      uint8_t b[8], *q = b; q = p8(q, PK_STATE_ACK); q = p8(q, np->gen); q = p16(q, (uint32_t)i);
      np_send(np, b, q);
      if (np->got_count == np->nchunks) {
        np->cb.load(np->cb.ctx, np->xfer, np->xfer_size);
        start_running(np);
        uint8_t r[4] = {PK_READY, np->gen}; np->cb.send(np->cb.ctx, r, 2);
      }
      break;
    }
    case PK_STATE_ACK:
      if (!np->host || np->status != NP_SYNCING || p[1] != np->gen || len < 4) return;
      if (g16(p + 2) < (uint32_t)np->nchunks) np->acked[g16(p + 2)] = 1;
      break;
    case PK_READY:
      if (np->host && np->status == NP_SYNCING && p[1] == np->gen) start_running(np);
      break;
    case PK_INPUT:
      if (p[1] != np->gen) return;
      if (!np->host && np->status == NP_RUNNING) np->ready_seen = true;
      if (np->status == NP_RUNNING) got_input(np, p, len);
      break;
    case PK_BYE:
      end(np, "THE OTHER PLAYER LEFT");
      break;
  }
}

// run frame f (state saved first), using the local and best-known remote input
static void run_frame(Netplay* np, int32_t f, bool render) {
  int s = f % SLOTS;
  np->slot_size[s] = np->cb.save(np->cb.ctx, np->slot[s]);
  np->slot_frame[s] = f;
  if (f % CRC_EVERY == 0) {
    int i = (f / CRC_EVERY) % RING;
    np->crc_val[i] = np->cb.checksum(np->cb.ctx); np->crc_frame[i] = f;
  }
  uint16_t l = np->in_local[f % RING], r = remote_input(np, f);
  np->predicted[f % RING] = r;
  if (np->host) np->cb.advance(np->cb.ctx, f, l, r, render); else np->cb.advance(np->cb.ctx, f, r, l, render);
}

int np_tick(Netplay* np, uint16_t local_buttons, double now) {
  np->now = now;
  if (np->started < 0) np->started = now;
  switch (np->status) {
    case NP_ENDED: return 0;
    case NP_WAITING: return 0;
    case NP_CONNECTING:
      if (now - np->started > 15) { end(np, "NO ANSWER FROM THE HOST"); return 0; }
      if (now - np->last_send_hs > 0.5) {
        uint8_t b[16], *p = b; p = p8(p, PK_HELLO); p = p32(p, MAGIC); p = p8(p, VERSION); p = p32(p, np->rom_crc);
        np_send(np, b, p); np->last_send_hs = now;
      }
      return 0;
    case NP_SYNCING:
      if (now - np->last_recv > 15) { end(np, "CONNECTION LOST"); return 0; }
      if (np->host) host_send_chunks(np);
      return 0;
    case NP_RUNNING: break;
  }
  if (now - np->last_recv > 10) { end(np, "CONNECTION LOST"); return 0; }
  if (!np->host && !np->ready_seen && now - np->last_send_hs > 0.25) {   // until the host has seen READY
    uint8_t r[4] = {PK_READY, np->gen}; np->cb.send(np->cb.ctx, r, 2); np->last_send_hs = now;
  }

  // 1. go back and replay if the other player's input turned out different from the prediction
  if (np->rollback_to < np->frame) {
    int32_t from = np->rollback_to, s = from % SLOTS;
    if (np->slot_frame[s] != from) { fprintf(stderr, "netplay: no state for frame %d\n", from); end(np, "INTERNAL ERROR"); return 0; }
    np->cb.load(np->cb.ctx, np->slot[s], np->slot_size[s]);
    int depth = np->frame - from;
    np->stats.rollbacks++; np->stats.frames_resimulated += depth;
    if (depth > np->stats.max_depth) np->stats.max_depth = depth;
    for (int32_t f = from; f < np->frame; f++) run_frame(np, f, false);
  }
  np->rollback_to = INT32_MAX;
  check_crc(np);
  if (np->status != NP_RUNNING) return 0;   // a resync started

  // 2. this tick's local input, for frame + delay
  int32_t fi = np->frame + np->delay;
  while (np->local_last < fi) { np->local_last++; np->in_local[np->local_last % RING] = local_buttons; }

  // 3. time sync: how far ahead of the other side are we (their frame now, estimated)?
  double peer_now = np->peer_frame + (now - np->peer_frame_at) * 60.0988 + np->stats.ping_ms / 2000.0 * 60.0988;
  np->adv_local = np->adv_local * 0.9 + (np->frame - peer_now) * 0.1;
  bool run = true;
  if (np->frame > np->remote_last + NP_MAX_PREDICT) run = false;   // too far ahead: wait for input
  else if (np->stall_cooldown > 0) np->stall_cooldown--;
  else if ((np->adv_local - np->adv_remote) / 2 >= 1.0) { run = false; np->stall_cooldown = 20; }   // ease off a frame
  int ran = 0;
  if (run) { run_frame(np, np->frame, true); np->frame++; ran = 1; }
  else np->stats.stalls++;
  send_input(np);
  return ran;
}

void np_leave(Netplay* np) {
  if (np->status != NP_ENDED) {
    uint8_t b[2] = {PK_BYE, np->gen};
    for (int i = 0; i < 3; i++) np->cb.send(np->cb.ctx, b, 2);
  }
  end(np, "YOU LEFT");
}

NpStatus np_status(const Netplay* np) { return np->status; }
int np_local_player(const Netplay* np) { return np->host ? 0 : 1; }
const NpSettings* np_settings(const Netplay* np) { return &np->settings; }
bool np_stalled(const Netplay* np) { return np->status == NP_RUNNING && np->now - np->last_recv > 1.0; }
const char* np_message(const Netplay* np) { return np->msg; }
const NpStats* np_stats(const Netplay* np) { return &np->stats; }
uint32_t np_frame(const Netplay* np) { return (uint32_t)np->frame; }
int32_t np_confirmed(const Netplay* np) { return np->remote_last < np->frame - 1 ? np->remote_last : np->frame - 1; }
bool np_debug_crc(const Netplay* np, int32_t f, uint32_t* crc) {
  int i = (f / CRC_EVERY) % RING;
  if (f % CRC_EVERY || np->crc_frame[i] != f || f > final_crc_frame((Netplay*)np)) return false;
  *crc = np->crc_val[i];
  return true;
}
bool np_is_join_request(const uint8_t* p, int len) { return len >= 10 && p[0] == PK_HELLO && g32(p + 1) == MAGIC; }
