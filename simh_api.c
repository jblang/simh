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
#include "sim_defs.h"
#include "scp.h"

/* Forward-declare main (defined in scp.c). */
extern int main (int argc, char *argv[]);

/* Declared in scp.c — when set, main() skips process_stdin_commands(). */
extern int simh_skip_cmdloop;

/* SIMH globals we need access to */
extern int32        sim_step;
extern volatile t_bool sim_is_running;
extern volatile t_bool stop_cpu;
extern int32        sim_switches;
extern t_stat       sim_instr (void);
extern CONST char  *get_glyph_cmd (const char *iptr, char *optr);
extern CTAB        *find_cmd (const char *gbuf);

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

if (!cmd_line || !*cmd_line)
    return SCPE_ARG;

strncpy (cbuf, cmd_line, sizeof(cbuf) - 1);
cbuf[sizeof(cbuf) - 1] = '\0';

cptr = get_glyph_cmd (cbuf, gbuf);
sim_switches = 0;

cmdp = find_cmd (gbuf);
if (!cmdp)
    return SCPE_UNK;

return (int) cmdp->action (cmdp->arg, cptr);
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
}

#endif /* __EMSCRIPTEN__ */
