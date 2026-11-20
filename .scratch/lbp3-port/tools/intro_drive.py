"""Milestone 5, Introduction stalls: stamps the game's log and (walk mode) drives the walk to Newton.

    python -I intro_drive.py stamp <out.txt> [--stop-file F] [--max-s S]
    python -I intro_drive.py walk  <out.txt> [--f10-on REGEX]... [--f10-on-save] [--f10-delay-ms N] [--f10-control-s S]
                                              [--after-save-s S] [--max-walk-s S] [--capture-every STEPS]

Run from the clone (D:\\Recomps\\lbp3-recompiled). Every line that appears in
analysis\\remote_stderr.txt is written to <out.txt> as "<ms>\\t<line>", where <ms> is
time.perf_counter() in ms: on Windows both it and the runtime's steady_clock are
QueryPerformanceCounter, so <ms> is on the same clock as frame_split.csv's submit_ms
(to the 10 ms poll). The watcher starts at the end of the current file and starts over
when the file shrinks (a new launch truncates it).

walk: the same menu route as pace_run_intro.ps1 / t10g_load.sh into the Introduction, then
walks right (the keyboard action move_right) with a long jump and a hop every 3.6 s, past the
tutorial video and Newton, until --after-save-s after the story save (a playfart_* opened while
walking: Newton's button, then the Legend of Bunkum loads), at most --max-walk-s.
F10 (an all-thread dump into the log, then 3000 samples each of main and GpuCommandProcessor):
--f10-on REGEX fires once, --f10-delay-ms after the first log line matching REGEX (at any time;
the option may be repeated); --f10-on-save fires on the story save; --f10-control-s fires one
that many seconds into the walk (a normal-walking control). The runtime takes one report at a
time, so triggers need to be several seconds apart. Writes "[drive] ..." event lines into the
stamped file. The caller launches and kills the game and handles the saves.
"""
import os
import re
import subprocess
import sys
import time

LOG = os.path.join("analysis", "remote_stderr.txt")
CMD = os.path.join("analysis", "input_cmd.txt")


def now_ms():
    return time.perf_counter() * 1000.0


def opt(name, default, cast=str):
    if name in sys.argv:
        return cast(sys.argv[sys.argv.index(name) + 1])
    return default


class Stamper:
    def __init__(self, out_path, from_end=True):
        self.out = open(out_path, "w", encoding="utf-8", newline="\n")
        self.pos = os.path.getsize(LOG) if from_end and os.path.exists(LOG) else 0
        self.partial = b""
        self.hooks = []  # (regex, callback) checked on each new line
        self.tick = None  # called on every poll (due F10 requests)

    def note(self, text):
        self.out.write(f"{now_ms():.1f}\t[drive] {text}\n")
        self.out.flush()

    def poll(self):
        try:
            size = os.path.getsize(LOG)
        except OSError:
            return
        if size < self.pos:  # truncated: a new launch
            self.pos, self.partial = 0, b""
            self.note("log restarted")
        if size == self.pos:
            return
        with open(LOG, "rb") as f:
            f.seek(self.pos)
            data = f.read(size - self.pos)
        self.pos += len(data)
        t = now_ms()
        data = self.partial + data
        lines = data.split(b"\n")
        self.partial = lines.pop()
        for raw in lines:
            line = raw.decode("utf-8", "replace").rstrip("\r")
            self.out.write(f"{t:.1f}\t{line}\n")
            for rx, cb in list(self.hooks):
                if rx.search(line):
                    cb(line, t)
        self.out.flush()

    def wait(self, seconds, until=None):
        """Polls for `seconds` (or until until() is true); returns until()'s last value."""
        end = time.perf_counter() + seconds
        while time.perf_counter() < end:
            self.poll()
            if self.tick:
                self.tick()
            if until and until():
                return True
            time.sleep(0.01)
        return bool(until and until())


def send(st, text):
    """Writes a remote-input command file once the game has taken the previous one."""
    tick, st.tick = st.tick, None
    for _ in range(40):  # the game polls every 50 ms and deletes the file
        if not os.path.exists(CMD):
            break
        st.wait(0.01)
    with open(CMD, "w", newline="\n") as f:
        f.write(text)
    st.tick = tick


def stamp_mode(out):
    st = Stamper(out)
    stop, max_s = opt("--stop-file", None), opt("--max-s", 600.0, float)
    st.note("stamp start")
    st.wait(max_s, until=lambda: stop is not None and os.path.exists(stop))
    st.note("stamp stop")


def walk_mode(out):
    st = Stamper(out, from_end=False)  # the caller launched just before us
    st.note("walk driver start")
    seen = {}

    def flag(name, rx):
        st.hooks.append((re.compile(rx), lambda line, t: seen.setdefault(name, t)))

    flag("level", r"00_craftworld/craftworld\.farc  ->")
    # The story save: a playfart_* written once walking has started (the level entry writes one too).
    st.hooks.append((re.compile(r"open /savedata\d/00000001/playfart_\S+  ->"),
                     lambda line, t: seen.setdefault("save", t) if walking["on"] else None))
    flag("perf_wrote", r"^\[perf\] wrote")
    f10_rxs = [sys.argv[k + 1] for k, a in enumerate(sys.argv) if a == "--f10-on"]
    f10_delay = opt("--f10-delay-ms", 0.0, float)
    f10_save = "--f10-on-save" in sys.argv
    f10_control = opt("--f10-control-s", None, float)
    after_save, max_walk = opt("--after-save-s", 25.0, float), opt("--max-walk-s", 200.0, float)
    capture_every = opt("--capture-every", 0, int)
    tag = os.path.basename(out).replace("_stamped.txt", "")
    due = []  # times (ms) at which to send F10
    walking = {"on": False}
    for k, f10_rx in enumerate(f10_rxs):
        def on_match(line, t, k=k):
            if f"f10_trigger{k}" not in seen:
                seen[f"f10_trigger{k}"] = t
                due.append(t + f10_delay)
                st.note(f"f10 trigger {k} matched: {line[:160]}")
        st.hooks.append((re.compile(f10_rx), on_match))
    if f10_save:
        def on_save(line, t):
            if walking["on"] and "f10_save" not in seen:
                seen["f10_save"] = t
                due.append(t + f10_delay)
                st.note(f"f10 save trigger matched: {line[:160]}")
        st.hooks.append((re.compile(r"open /savedata\d/00000001/playfart_\S+  ->"), on_save))

    def tick():
        if due and now_ms() >= min(due):
            due.remove(min(due))
            send(st, "f10\n")
            st.note("f10 sent")
    st.tick = tick

    # Menu route (pace_run_intro.ps1): Home -> Play -> Adventure -> Prologue map -> Introduction.
    st.wait(28)
    send(st, "cross\nwait 2000\nsquare\nwait 2500\n")
    st.wait(6)
    send(st, "up 100\nwait 700\n")
    st.wait(1.5)
    send(st, "cross\nwait 3000\ncross\nwait 6500\ncross\nwait 4500\nleft 100\nwait 1500\ncross\nwait 1000\n")
    if not st.wait(60, until=lambda: "level" in seen):
        st.note("craftworld.farc never opened")
        sys.exit(2)
    st.note("level farc opened; waiting 12 s for the spawn")
    st.wait(12)
    walking["on"] = True
    t_walk = now_ms()
    st.note("walking")
    i = 0
    while True:
        t = now_ms()
        if "save" in seen and t > seen["save"] + after_save * 1000:
            st.note("done: after-save time reached")
            break
        if t > t_walk + max_walk * 1000:
            st.note("done: max walk time reached (no save seen)" if "save" not in seen else "done: max walk time")
            break
        if f10_control is not None and "f10_control" not in seen and t > t_walk + f10_control * 1000:
            seen["f10_control"] = t
            due.append(t)
            st.note("f10 control due")
        # One walk step every 3.6 s: walk-right held the whole step (the keyboard action, which
        # runs beside the button queue), a long jump at 0.9 s and a hop at 2.3 s; F10 in between.
        step_end = t + 3600
        jumps = [(t + 900, "action jump 600\n"), (t + 2300, "action jump 200\n")]
        send(st, "action move_right 3700\n")
        if capture_every and i % capture_every == 0:
            subprocess.Popen(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", r"tools\capture.ps1",
                              "-Out", os.path.join(os.path.dirname(out), f"{tag}_w{i:03d}.png")],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        i += 1
        while now_ms() < step_end:
            if jumps and now_ms() >= jumps[0][0]:
                send(st, jumps.pop(0)[1])
            st.wait(0.01)  # also sends any F10 that is due (tick)
    st.note(f"walk steps {i}")
    st.wait(3)  # let the last frame-split rows reach the file (flushed every 60 rows / status line)


def main():
    mode, out = sys.argv[1], sys.argv[2]
    if mode == "stamp":
        stamp_mode(out)
    elif mode == "walk":
        walk_mode(out)
    else:
        sys.exit("mode: stamp | walk")


if __name__ == "__main__":
    main()
