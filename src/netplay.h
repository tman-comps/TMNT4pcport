// Online two-player play: rollback netcode over a datagram link.
//
// Both sides run the same deterministic game and exchange only the controller input of each frame.
// The local player's input is used at once; the other player's is predicted (the last one received)
// and when the real input arrives and differs, the game goes back to that frame (save state) and
// replays to the present. When a player joins, the host's save state is sent first, so play
// continues from wherever the host was. Every 32 frames the two sides compare a checksum of the
// game's RAM; if they ever differ, the host sends its state again.
//
// This file knows nothing about sockets, SDL or the emulator: the owner passes received packets in,
// sends what the send callback gives it, and provides save/load/advance. That keeps it testable
// (tmnt_tool's nettest runs two sides over a simulated link with delay and loss).
#ifndef NETPLAY_H
#define NETPLAY_H
#include <stdint.h>
#include <stdbool.h>

#define NP_PORT 7845
#define NP_STATE_MAX (1 << 20)   // largest save state
#define NP_MAX_PREDICT 8         // frames that may run ahead of the other player's last input

typedef struct Netplay Netplay;

typedef struct {
  void* ctx;
  int (*save)(void* ctx, uint8_t* buf);                 // save state into buf (NP_STATE_MAX), return its size
  void (*load)(void* ctx, const uint8_t* buf, int size);
  // run session frame `frame` with these buttons (frames already run come again after a rollback);
  // render = false for replayed frames, whose picture is never shown
  void (*advance)(void* ctx, int32_t frame, uint16_t p1, uint16_t p2, bool render);
  uint32_t (*checksum)(void* ctx);                      // of the game state (RAM)
  void (*send)(void* ctx, const uint8_t* pkt, int len); // send a packet to the other side
} NpCallbacks;

// Settings that change how the game plays: the host's are used on both sides for the session.
typedef struct {
  uint8_t overclock;   // Remove slowdown
  uint8_t x_lives, x_health, x_stage;   // extras (only used if unlocked)
  uint8_t unlocked;
} NpSettings;

typedef enum {
  NP_WAITING,      // host: waiting for someone to join (the host can keep playing alone)
  NP_CONNECTING,   // client: asking the host
  NP_SYNCING,      // the host's state is being sent: the game is frozen on both sides
  NP_RUNNING,      // playing together
  NP_ENDED,        // over; np_message() says why
} NpStatus;

typedef struct {
  int rollbacks, max_depth, frames_resimulated, stalls, desyncs, resyncs;
  int ping_ms;
} NpStats;

Netplay* np_create(bool host, NpCallbacks cb, uint32_t rom_crc, const NpSettings* settings, int input_delay);
void np_destroy(Netplay* np);
// true for the packet a joining player sends first (a host locks onto its sender)
bool np_is_join_request(const uint8_t* pkt, int len);
// Feed a packet that arrived from the other side. now = seconds on a steady clock.
void np_receive(Netplay* np, const uint8_t* pkt, int len, double now);
// Once per displayed frame, with the local player's buttons. Sends what's due, rolls back if needed
// and runs the next frame. Returns the number of frames run on top of any replay (0 or 1); 0 while
// not running, or when this side waits for the other one.
int np_tick(Netplay* np, uint16_t local_buttons, double now);
// Tell the other side we're leaving (sent a few times) and end the session.
void np_leave(Netplay* np);

NpStatus np_status(const Netplay* np);
int np_local_player(const Netplay* np);        // 0 = player 1 (host), 1 = player 2
const NpSettings* np_settings(const Netplay* np);   // the session's game settings (the host's)
bool np_stalled(const Netplay* np);            // nothing heard for a while (show "waiting")
const char* np_message(const Netplay* np);     // why it ended, or "" while it's fine
const NpStats* np_stats(const Netplay* np);
uint32_t np_frame(const Netplay* np);          // frames played in this session
int32_t np_confirmed(const Netplay* np);       // last frame whose input is known on both sides
bool np_debug_crc(const Netplay* np, int32_t frame, uint32_t* crc);   // final checksum stored for frame (tests)
#endif
