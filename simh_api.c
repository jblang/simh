/*
 * simh_api.c — Thin C API for calling SIMH from JavaScript/Emscripten.
 *
 * Provides four exported functions:
 *   simh_init()      — Run SIMH initialization, skip command loop.
 *   simh_cmd(cmd)    — Execute one SIMH command string.
 *   simh_step(n)     — Execute n CPU instructions.
 *   simh_stop()      — Request CPU stop.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sim_defs.h"
#include "scp.h"
#include "i650_defs.h"

/* Forward-declare main (defined in scp.c). */
extern int main (int argc, char *argv[]);

/* Declared in scp.c — when set, main() skips process_stdin_commands(). */
extern int simh_skip_cmdloop;

/* Yield configuration for cooperative run loop slices (in scp.c). */
int simh_yield_enabled = 1;
int simh_yield_steps = 10000;
volatile t_bool simh_stop_requested = FALSE;
volatile t_bool simh_cmd_active = FALSE;

/* SIMH globals we need access to */
extern int32        sim_step;
extern volatile t_bool sim_is_running;
extern volatile t_bool stop_cpu;
extern volatile t_bool simh_stop_requested;
extern volatile t_bool simh_cmd_active;
extern int32        sim_switches;
extern t_stat       sim_instr (void);
extern CONST char  *get_glyph_cmd (const char *iptr, char *optr);
extern CTAB        *find_cmd (const char *gbuf);

/* IBM 650 state we stream for the front panel */
extern t_int64 ACC[2];
extern t_int64 DIST;
extern t_int64 PR;
extern uint16 AR;
extern uint16 IC;
extern uint8 OV;
extern int AccNegativeZeroFlag;
extern int DistNegativeZeroFlag;

#define SIMH_STATE_STREAM_SIZE 1024

#pragma pack(push, 1)
typedef struct {
  char pr[12];
  char ar[5];
  char ic[5];
  char acc_lo[12];
  char acc_up[12];
  char dist[12];
  uint8 ov;
} simh_state_sample;
#pragma pack(pop)

static simh_state_sample simh_state_ring[SIMH_STATE_STREAM_SIZE];
static simh_state_sample simh_state_outbuf[SIMH_STATE_STREAM_SIZE];
static char simh_state_json_buf[256];
static uint32_t simh_state_head = 0;
static uint32_t simh_state_tail = 0;
static int simh_state_stream_enabled = 0;
static int simh_state_stream_stride = 1;
static int simh_state_stream_counter = 0;

static void simh_state_format_word(t_int64 value, int negzero, char out[12])
{
  sprintf(out, "%06d%04d%c", printfw(value, negzero));
  out[11] = '\0';
}

static void simh_state_format_addr(int value, char out[5])
{
  if (value < 0) value = -value;
  sprintf(out, "%04d", value % 10000);
  out[4] = '\0';
}

void simh_state_stream_push_i650(void)
{
  if (!simh_state_stream_enabled) return;
  if (++simh_state_stream_counter < simh_state_stream_stride) return;
  simh_state_stream_counter = 0;

  simh_state_sample sample;
  simh_state_format_word(PR, 0, sample.pr);
  simh_state_format_addr(AR, sample.ar);
  simh_state_format_addr(IC, sample.ic);
  simh_state_format_word(ACC[0], AccNegativeZeroFlag, sample.acc_lo);
  simh_state_format_word(ACC[1], AccNegativeZeroFlag, sample.acc_up);
  simh_state_format_word(DIST, DistNegativeZeroFlag, sample.dist);
  sample.ov = (uint8) (OV ? 1 : 0);

  simh_state_ring[simh_state_head] = sample;
  simh_state_head = (simh_state_head + 1) % SIMH_STATE_STREAM_SIZE;
  if (simh_state_head == simh_state_tail) {
    simh_state_tail = (simh_state_tail + 1) % SIMH_STATE_STREAM_SIZE;
  }
}

EMSCRIPTEN_KEEPALIVE
void simh_state_stream_enable (int enabled)
{
  simh_state_stream_enabled = enabled ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void simh_state_stream_set_stride (int stride)
{
  if (stride < 1) stride = 1;
  simh_state_stream_stride = stride;
}

EMSCRIPTEN_KEEPALIVE
void simh_state_stream_clear (void)
{
  simh_state_head = 0;
  simh_state_tail = 0;
}

EMSCRIPTEN_KEEPALIVE
int simh_state_stream_sample_size (void)
{
  return (int) sizeof(simh_state_sample);
}

EMSCRIPTEN_KEEPALIVE
int simh_state_stream_read (int max, void *out)
{
  if (!out || max <= 0) return 0;
  if (max > SIMH_STATE_STREAM_SIZE) max = SIMH_STATE_STREAM_SIZE;
  const int sample_size = (int) sizeof(simh_state_sample);
  int count = 0;
  uint8 *dst = (uint8 *) out;

  while ((simh_state_tail != simh_state_head) && (count < max)) {
    memcpy(dst + (count * sample_size), &simh_state_ring[simh_state_tail], sample_size);
    simh_state_tail = (simh_state_tail + 1) % SIMH_STATE_STREAM_SIZE;
    count++;
  }
  return count;
}

EMSCRIPTEN_KEEPALIVE
int simh_state_stream_read_to_buffer (int max)
{
  return simh_state_stream_read(max, simh_state_outbuf);
}

EMSCRIPTEN_KEEPALIVE
uintptr_t simh_state_stream_buffer_ptr (void)
{
  return (uintptr_t) simh_state_outbuf;
}

EMSCRIPTEN_KEEPALIVE
const char *simh_state_stream_read_last_json (void)
{
  simh_state_sample last;
  int have_last = 0;

  while (simh_state_tail != simh_state_head) {
    last = simh_state_ring[simh_state_tail];
    simh_state_tail = (simh_state_tail + 1) % SIMH_STATE_STREAM_SIZE;
    have_last = 1;
  }

  if (!have_last) {
    simh_state_json_buf[0] = '\0';
    return simh_state_json_buf;
  }

  snprintf(
    simh_state_json_buf,
    sizeof(simh_state_json_buf),
    "{\"pr\":\"%s\",\"ar\":\"%s\",\"ic\":\"%s\",\"accLo\":\"%s\",\"accUp\":\"%s\",\"dist\":\"%s\",\"ov\":%u}",
    last.pr,
    last.ar,
    last.ic,
    last.acc_lo,
    last.acc_up,
    last.dist,
    (unsigned int) last.ov
  );

  return simh_state_json_buf;
}

/*
 * simh_init — Initialize the emulator.
 *
 * Calls SIMH's main() which handles all subsystem initialization
 * (sockets, filesystem, timers, terminal, device resets, breakpoints).
 * The simh_skip_cmdloop flag causes it to return after init instead
 * of entering the interactive command loop.
 *
 * Returns 0 on success, non-zero on failure.
 */
EMSCRIPTEN_KEEPALIVE
int simh_init (void)
{
char *argv[] = { "i650", NULL };

simh_skip_cmdloop = 1;
return main (1, argv);
}

/*
 * simh_cmd — Execute a single SIMH command.
 *
 * Parses the command string, looks it up in the command table,
 * and dispatches it. All output goes through sim_printf → stdout
 * → Emscripten Module.print, where it is captured by JavaScript.
 *
 * Returns the SIMH status code (t_stat). 0 = SCPE_OK.
 */
EMSCRIPTEN_KEEPALIVE
int simh_cmd (const char *cmd_line)
{
char cbuf[4*CBUFSIZE], gbuf[CBUFSIZE];
CONST char *cptr;
CTAB *cmdp;
t_stat stat;
t_stat bare;
int nomessage;

if (!cmd_line || !*cmd_line)
    return SCPE_ARG;

#ifdef __EMSCRIPTEN__
/* Clear any stale stop request before starting a new top-level command. */
simh_stop_requested = FALSE;
#endif

strncpy (cbuf, cmd_line, sizeof(cbuf) - 1);
cbuf[sizeof(cbuf) - 1] = '\0';

cptr = get_glyph_cmd (cbuf, gbuf);
sim_switches = 0;

cmdp = find_cmd (gbuf);
if (!cmdp) {
    sim_printf ("%s\n", sim_error_text (SCPE_UNK));
    return SCPE_UNK;
    }

simh_cmd_active = TRUE;
stat = cmdp->action (cmdp->arg, cptr);
simh_cmd_active = FALSE;
nomessage = (stat & SCPE_NOMESSAGE) != 0;
bare = SCPE_BARE_STATUS (stat);
if (!nomessage) {
    if (cmdp->message)
        cmdp->message (NULL, stat);
    else if ((bare >= SCPE_BASE) && (bare != SCPE_EXPECT))
        sim_printf ("%s\n", sim_error_text (bare));
    }
return (int) stat;
}

/*
 * simh_step — Execute n CPU instructions.
 *
 * This is a minimal version of run_cmd (scp.c) that skips console
 * mode switching (already no-op on Emscripten) and signal setup.
 * Sets sim_step so sim_instr() returns after n instructions.
 *
 * Returns the SIMH status code:
 *   SCPE_STEP (36) — step count exhausted (normal for tick loop)
 *   SCPE_STOP (8)  — programmed stop or halt instruction
 *   SCPE_OK (0)    — shouldn't normally happen
 *   Other values    — breakpoint hit, error, etc.
 */
EMSCRIPTEN_KEEPALIVE
int simh_step (int n)
{
t_stat r;

if (n <= 0)
    return SCPE_ARG;

sim_step = n;
sim_is_running = TRUE;
stop_cpu = FALSE;

r = sim_instr ();

sim_is_running = FALSE;
#ifdef __EMSCRIPTEN__
if ((r != SCPE_STEP) && (r != SCPE_OK)) {
    fprint_stopped (stdout, r);
    }
#endif
return (int) r;
}

/*
 * simh_stop — Request the CPU to stop.
 *
 * Sets the stop_cpu flag which sim_instr() checks periodically.
 * The current or next simh_step() call will return early.
 */
EMSCRIPTEN_KEEPALIVE
void simh_stop (void)
{
stop_cpu = TRUE;
simh_stop_requested = TRUE;
}

/*
 * simh_is_running — Returns 1 if the CPU is currently running.
 */
EMSCRIPTEN_KEEPALIVE
int simh_is_running (void)
{
return sim_is_running ? 1 : 0;
}

/*
 * simh_is_busy — Returns 1 if the emulator is executing a command or running CPU.
 */
EMSCRIPTEN_KEEPALIVE
int simh_is_busy (void)
{
return (sim_is_running || simh_cmd_active) ? 1 : 0;
}

/*
 * simh_get_yield_steps — Returns the current yield step count.
 */
EMSCRIPTEN_KEEPALIVE
int simh_get_yield_steps (void)
{
return simh_yield_steps;
}

/*
 * simh_set_yield_steps — Configure how many instructions run per yield slice.
 */
EMSCRIPTEN_KEEPALIVE
void simh_set_yield_steps (int steps)
{
if (steps < 0)
    steps = 0;
simh_yield_steps = steps;
}


/*
 * simh_get_yield_enabled — Returns 1 if yielding is enabled.
 */
EMSCRIPTEN_KEEPALIVE
int simh_get_yield_enabled (void)
{
return simh_yield_enabled ? 1 : 0;
}

/*
 * simh_set_yield_enabled — Enable or disable yielding during long runs.
 */
EMSCRIPTEN_KEEPALIVE
void simh_set_yield_enabled (int enabled)
{
simh_yield_enabled = enabled ? 1 : 0;
}

#endif /* __EMSCRIPTEN__ */
