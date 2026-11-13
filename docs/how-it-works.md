# How vkmPlatform works

What each piece of the platform does, and why. The API is in the headers; this page is what they
cannot say.

## The window

`WindowManager` owns one GLFW window and the `WindowSurface` a renderer draws into it through. The
surface is hinted before the window exists (a context of some version, or none), given the window
once it does, and kept to present each frame and set the swap interval: so the window names no
graphics API, and a renderer is the one place that does.

**A closed window accepts every request and performs none.** Every call checks for the window
first, its reporters answer zero (`framebufferScale` one), and the close request is a field of its
own rather than GLFW's, so a program with no window still runs its frames and can still be told to
end.

**Input arrives by callback, inside `updateInput()`'s poll**, on the main thread. A callback writes
into the `InputHandle`; the pointer's position is kept by callback and handed over once a frame,
so its step is the frame's movement. Leaving the window reports no position, so the last one
inside is asked for on the way out. Entering and leaving `CursorMode::Disabled` moves the cursor,
which is no movement of the user's: the handle is told where it went without a step (`warpTo`).
`setCursorFree` shows the cursor over whatever mode was set and puts the mode back once freed no
longer, so a menu can take the pointer without forgetting the game's.

The framebuffer's size and the window's are kept by callback, not asked each frame: on X11 each
question is a round trip to the server. The monitor a window is on is the one it overlaps most,
since GLFW names one only when it is fullscreen. `fittingWindowSizes` turns a monitor's video modes
(any order, sizes repeated by refresh rate) into the sizes that fit, each once, largest first.

Dropped files are kept only once the consumer asks (`keepDroppedFiles`), so one that never takes
them keeps none.

## Keys and buttons

`Key` and `MouseButton` are declared in GLFW's code order, and the window maps a code to one through
two tables in `window_manager.cpp`, each checked at compile time to hold a code for every key, in
rising order and ending at GLFW's last: so a code is found by bisection, a walk over the keys meets
them as a walk over the codes does, and a GLFW with a key past its last fails to compile until it has
a name. A code no key has, as `GLFW_KEY_UNKNOWN`, is dropped there.

## The device state

`InputHandle` is one frame of keys, buttons, pointer, scroll and typed text, by the library's names
and with no GLFW in it, so a program with no window, or a test, feeds it through the calls the
window makes. One poll can carry a press and its release together, which the level alone would
miss: so a key also records a **strike** (it went down since `clearStrikes`), a **restrike** (it was
down at `clearStrikes`, and was let go and struck again since) and **typed** (it went down or
repeated: a repeat is a press of a key already held). `clearStrikes` keeps what is held. Scroll
adds up within a frame, as one frame can carry several notches, and is cleared before each poll.

## The frame limiter

A frame is paced from the last frame's deadline, not from its own start, or the present and the
events between frames would add to every period and the cadence would run slow. An overrun restarts
the pace from now: a stall is not paid back with short frames. The limiter sleeps all but the last
millisecond and spins that, since a sleep wakes late. On Windows it asks for a 1 ms scheduler tick
for its life and sleeps on a high-resolution waitable timer, as a plain sleep wakes on the
scheduler's 15.6 ms tick on machines that ignore the request.

`swapBuffers` presents, then keeps the cap. Each is a profiler zone of its own (`SwapBuffers`,
`FrameLimiter`), so a sleep for the cap is never read as a stall in the swap; a capture counts frames
by `SwapBuffers`, so its name does not change.

## Threads

`ThreadPool::get()` is the process's one pool, made on first use with one worker fewer than the
physical cores the process may run on (its affinity; a core's SMT siblings counted once), as the
thread that calls `parallelFor` runs a chunk too, and the process's other threads need room.

- **Waiting is per batch, never global.** The pool runs background tasks too, and a barrier on
  everything in flight would park a frame behind a file read.
- **Batches go first.** Batches and tasks sit in two queues, and a worker drains the batch queue
  first: a batch has a caller blocked on it, a task does not. Only a caller that waits fills the
  batch queue, so the tasks are not starved for long.
- **A batch allocates nothing.** It borrows its task and stays one queue entry until its last index
  is claimed; the caller claims indices too while it waits, wherever the entry stands.
- **An index that throws still retires**, and the first throw is rethrown to the waiter; a task's
  throw is caught and logged by the worker, as nobody waits on it.
- **`shutdown()` joins the workers before `main` returns.** The static's destructor would run after
  what the tasks write into, and on Windows inside a library's unload, where a join hangs. Queued
  work is dropped but still retires, so no waiter blocks, and `parallelFor` runs serially after.

`parallelFor(count, function)` runs a small range on the caller and cuts a larger one into several
chunks per thread, so a thread the system preempts leaves its share to the others rather than
stretching the join. Called from a worker it runs serially there: the other workers are busy, so
nested chunks would only come back at a lock each. A throw reaches the caller after every other
chunk has run, since the queued chunks reach into the caller's frame; the rest of the chunk that
threw is skipped.

## Processes

**A child process** writes its standard output and error into one pipe, read on a thread of its
own, so a caller polls `takeOutput()` from its frame. It runs in a group of its own on Linux and a
job on Windows, so `stop()` ends it and everything it started. It is asked first - SIGTERM on Linux,
on Windows the event `stopRequested()` opens - and ended two seconds on; on Windows one that never
asked has no event, and is ended at once. On Linux the reader asks after the program between reads,
as a daemon the program left behind holds the pipe open forever.

**A dynamic library loaded through a copy** (`loadCopy`) leaves the original free to be rebuilt:
Windows locks a loaded library against writes. The copy is `<stem>.loaded.<n><extension>` beside
it, n new to the process, stepping past a name another process holds on Windows, where a loaded file
cannot be deleted; `unload()` deletes it. A
folder that takes no copy, read-only as an installed program's, loads the original in place. Any
other failure to copy loads nothing, as the original would then be locked against its rebuild.
`sweepCopies` deletes what a process that died left behind, skipping, on Windows, a copy another
still holds.
On Linux a library is opened `RTLD_NOW | RTLD_LOCAL`: a missing symbol fails at the load, and a
library loaded again stays apart from the one before.

**A file's claim** (`FileLock`) is a lock the system holds, taken without waiting, on a file it
creates if missing: `flock` on Linux, `LockFileEx` on Windows. A Windows lock is mandatory, so it is
on one byte past any end a file reaches: on a byte a read or a write reached, it would refuse them to
every other handle, the process's own too, and a log claimed could not be written. A second claim,
from this process or another, is refused, and the system lets a claim go when its process ends,
however it ends, so a crash leaves none behind. The file is not inherited by a child process.

**This process.** `executableDir`, and where the system keeps a user's configuration and state,
read from the environment on each call (`%APPDATA%` and `%LOCALAPPDATA%`; `$XDG_CONFIG_HOME` and
`$XDG_STATE_HOME`, else under `$HOME`), wide on Windows, where the narrow environment mangles a
non-ANSI user name. `attachParentConsole` lets a Windows GUI executable print to the console it was
started from. `stopRequested()` installs handlers on its first call: for SIGINT and SIGTERM, and on
Windows for Ctrl+C, Ctrl+Break and the console closing; there, in place of SIGTERM, it also opens an
event named for the process, for `ChildProcess::stop` to set. Each of them sets one lock-free flag,
so a program leaves through its normal exit, and a process that never asks keeps the system's
default. A console closing ends the process once its handler returns, so that handler holds until
the process leaves, or the system's few seconds run out.

**The discrete GPU.** On a laptop with two GPUs, NVIDIA's and AMD's drivers read an export of the
executable, never of a library, to pick the discrete one: `VKM_PLATFORM_PREFER_DISCRETE_GPU()`,
written once at file scope in an executable's `main.cpp`, declares both. Elsewhere it is nothing.
