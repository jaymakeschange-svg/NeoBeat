# House rules

From *MidiJaQ and Batchcut: handover*, 16 November 2026
(https://claude.ai/artifact/DXuWnTuGspj4sHv4anHgeN). These are Marbelite's, and they hold whoever
is doing the work.

## How to work with him

- He brings the ideas; the assistant writes the code and offers information that strengthens an
  idea or a better way to build it. **Don't redesign his idea — improve how it is made.**
- **Ask before starting anything long**, and say roughly how long it will take.
- He reads on a phone much of the time. Lead with the answer, keep it short, and send a picture
  when the thing is visual.
- Commit and push only when he asks, always to the plugin's own branch, **never to `main`**.
  Every commit ends with the session's co-author lines.

## What must never be done

- No circumventing copy protection or licensing of any kind, and no reproducing another program's
  proprietary code. The research is in *principles*, reimplemented.
- Don't read the licence or key files of commercial plugins he owns, and don't enter serial
  numbers for him.
- The Wave emulator source he has is AGPL: learn from how it works, don't copy it in.
- Sting is studied **by its output only** — record what it sends and measure that. Its code stays
  closed.
- His email address is private.

## Working on his computer

He is on Windows. A session can reach his laptop when the desktop app is connected. Two things
learned the hard way: Windows blocks input to an app running as administrator, and it never allows
typing into a save dialog — so **leave saving files to him**.

## The house style

Plain words in the interface and in comments — no jargon where an ordinary word will do, and no
exclamation marks or sales talk. Comments explain *why* a thing is done, not what the line says.
Tooltips are written for someone who has never seen a sequencer. Tests print a sentence a person
can read, with the measured number in it, so a passing test still says something.

## The shared-header convention

Each plugin is its own tree, but they share code by keeping a set of headers **identical in every
tree**, under `Source/neobeat/`. Change one and copy it to the others in the same stretch of work —
that convention is the whole reason the three programs agree with each other.

> **Known defect, 26 Nov:** NeoBeat's tree is flat (`Source/shared/`) where the other two are
> nested (`Source/neobeat/shared/rooms/`), so byte-identity is impossible there and copies differ
> by include depth. This has produced two unbuildable headers so far.

## And look at the snapshots

The window is written without seeing it; a PNG catches overlapping controls and unreadable text
that no test will. **Always run the host test on several seeds before pushing** — it is what
catches the things unit tests do not.
