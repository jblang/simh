# Interactive I/O Issue with Emscripten SIMH

## Problem Summary

The SIMH simulator compiled with Emscripten to WebAssembly does not work with interactive input in Node.js or browsers. The simulator continuously displays the `sim>` prompt without accepting any keyboard input.

## What Works

- **Piped input**: Commands work correctly when piped
  ```bash
  echo "help" | node /Users/jblang/repos/simh/BIN/altair.js
  ```
- **Non-interactive mode**: File-based input and batch operations work

## What Doesn't Work

- **Interactive PTY input**: Typing commands at the prompt has no effect
- **Browser console**: Same issue in web browsers
- **Node.js TTY**: Input is not received even with raw mode enabled

## Root Cause

Emscripten's stdin emulation in Node.js does not integrate properly with PTY (pseudo-terminal) input streams. The `read()` syscall in the C code (sim_console.c:3882) never receives data from the PTY, even though:
- Characters are being sent to the PTY (verified with `expect` scripting)
- The ASYNCIFY feature prevents CPU spinning (allows yielding to event loop)
- stdin is in raw mode and data events are being fired

## Approaches Attempted

### 1. ASYNCIFY Configuration ✅ (Partial Success)
**File**: Emscripten-HTML.cmake
**What it does**: Allows synchronous C code to yield to JavaScript event loop
**Result**: Prevents infinite looping/CPU spinning, but doesn't fix stdin

```cmake
-s ASYNCIFY=1
-s ASYNCIFY_STACK_SIZE=131072
-s ASYNCIFY_IGNORE_INDIRECT=1
```

### 2. Modified sim_console.c ❌ (Failed)
**Lines modified**: 3831, 3847, 3882-3900
**Attempts**:
- Prevented O_NONBLOCK on stdin for Emscripten
- Added `emscripten_sleep()` calls (function not available)
- Added `usleep()` calls (compiled but still broken)
- Added `EM_ASM` with `Asyncify.handleSleep()` (still broken)

**Result**: None of these fixes allowed PTY input to be received

### 3. Custom stdin-wrapper.js ❌ (Failed)
**File**: /Users/jblang/repos/simh/emscripten-build/stdin-wrapper.js
**Approach**: Override Emscripten's Module.stdin with custom Node.js stdin handler
- Set stdin to raw mode
- Buffer incoming data in JavaScript array
- Provide custom `Module.stdin()` function

**Result**: Input still not received by C code's `read()` call

### 4. PTY Testing with Expect
**File**: /Users/jblang/repos/simh/emscripten-build/test-pty.exp
**Purpose**: Verify input is being sent to PTY correctly
**Result**: Confirmed input is sent but never received by simulator

## Test Results

### PTY Test Output
```
spawn node stdin-wrapper.js
sim>
GOT PROMPT
sim>
FAILURE - Got another prompt without help output
[infinite sim> prompts...]
```

This shows:
1. Prompt appears correctly
2. "help" command is sent via PTY
3. Input is never received (no help output appears)
4. Simulator immediately prompts again

## Technical Details

### Key Files and Changes

1. **sim_console.c** (stdin handling)
   - Line 154: Added `#include <emscripten.h>`
   - Lines 3831, 3847: Prevented O_NONBLOCK for Emscripten
   - Lines 3882-3900: Added ASYNCIFY sleep in `sim_os_poll_kbd()`

2. **Emscripten-HTML.cmake** (build configuration)
   - Enables ASYNCIFY for yielding to event loop
   - Sets up proper runtime exports (FS, TTY)
   - Configures memory growth and filesystem

3. **stdin-wrapper.js** (custom Node.js stdin handler)
   - Attempts to bypass Emscripten's broken stdin
   - Provides custom buffering and Module.stdin() function

## Hypothesis

Emscripten's filesystem emulation (`FS.init()` and related code) in Node.js creates file descriptors that don't properly connect to Node.js's stdin stream when it's a TTY. The gap appears to be in how Emscripten maps Node.js's `process.stdin` readable stream to the Unix-like `read()` syscall that the C code expects.

Specifically:
- Piped input works because it's a simple readable stream that gets buffered
- PTY input fails because it requires real-time character-by-character reading from a TTY
- The C code's blocking `read(0, buf, 1)` never receives the PTY data

## Potential Solutions (Not Attempted)

1. **Rewrite stdin handling in C code**: Replace blocking `read()` with JavaScript callbacks
   - Major code changes to sim_console.c
   - Would break compatibility with native builds

2. **Custom Emscripten FS driver**: Implement a custom `/dev/stdin` that properly hooks Node.js stdin
   - Requires deep Emscripten internals knowledge
   - May not be possible due to Emscripten limitations

3. **Terminal proxy server**: Run a websocket server that accepts browser connections
   - Adds deployment complexity
   - Doesn't solve Node.js interactive mode

4. **Wait for Emscripten fix**: This may be a known Emscripten limitation
   - Check Emscripten issue tracker
   - May require upstream changes

## Files Modified (Compilation Fixes)

These changes successfully fixed Emscripten compilation errors:

- **sim_sock.c**: Added `__EMSCRIPTEN__` to socklen_t platform checks
- **sim_frontpanel.c**: Disabled pthread scheduling (lines 2455-2460)
- **sim_timer.c**: Disabled pthread scheduling (lines 366-381, 2297-2303)
- **sim_ether.h**: Excluded Emscripten from PCAP auto-enable (lines 132-135)
- **sim_ether.c**: Added PCAP stub types (lines 1033-1045)
- **sim_ws.c**: Added SDL type stubs (lines 47-52)
- **sim_video.c**: Added video function stubs (lines 2896-2914)

## Build Status

✅ Compilation successful
✅ HTML/JS/WASM files generated
✅ Piped input works
❌ Interactive PTY input broken
❌ Browser interactive input broken (not tested recently)

## Current Build Artifacts

- `/Users/jblang/repos/simh/BIN/altair.html` (19KB)
- `/Users/jblang/repos/simh/BIN/altair.js` (109KB)
- `/Users/jblang/repos/simh/BIN/altair.wasm` (703KB)

## Build Commands

```bash
cd /Users/jblang/repos/simh/emscripten-build
./reconfigure-html.sh  # Configure with HTML output
make altair            # Build Altair simulator
```

## Recommendation

**For Production Use**: Stick with piped/batch input mode until Emscripten's stdin/TTY support improves.

**For Interactive Use**: Consider alternative approaches:
- Run native SIMH binary (not WebAssembly)
- Use a web-based terminal emulator with websocket backend
- Create a GUI interface that doesn't rely on stdin

## Date

January 30, 2026
