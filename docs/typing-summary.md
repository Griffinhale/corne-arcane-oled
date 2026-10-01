# Typing summary

The typing helper lets the desktop city respond to how you type on a keyboard
that does not run this firmware. It is the one place on the host that reads
keys. This page lists everything it may do. Anything not listed is outside
the rule.

The Corne never needs it. Its firmware works out typing rhythm on the
keyboard, and none of that leaves the keyboard.

## The rule

- **Opt-in and off by default.** Nothing reads keys until you turn the helper on.
- **Behind the OS permission.** The helper reads only devices the system lets
  your session read. It never asks for more.
- **Local.** Summaries go over the session bus to the desktop city and nowhere
  else. Nothing goes over the network, into a file or onto the keyboard wire.
- **Raw events stay in the helper.** Keys, keycodes, key-up events and the
  time of any single key never leave the helper process. Only the summary
  below leaves it. This comes from the shape of the code, not from a filter:
  the reducer takes only rows and times, never keys, and returns only four
  enums.
- **One window in memory.** The helper holds one window of events, reduces
  it, and discards it. Nothing is written to disk.

## What leaves the helper

One record per 60-second window, four small enums, and nothing else. There is
no timestamp, count, key or per-key time.

| Field | Values | Rule |
|---|---|---|
| `tempo` | DELIBERATE, FLOWING, RAPID, FRANTIC | Mean in-burst gap: under 110 ms FRANTIC, under 150 ms RAPID, under 220 ms FLOWING, else DELIBERATE. |
| `spread` | STEADY, VARIED, IRREGULAR | Interquartile range of in-burst gaps: under 40 ms, under 160 ms, else. |
| `row` | TOP, HOME, BOTTOM, THUMB | The row with the most keydowns. A tie goes to HOME. |
| `row_spread` | FOCUSED, MIXED, EVEN | That row's share of keydowns: 60% or more, 40% or more, else. |

- **Window.** 60 seconds, back to back, lined up with the wall-clock minute.
  A window does not start at the first key, so its edges say nothing about
  when typing began.
- **In-burst gap.** The time between two keydowns, counted only when it is
  under 520 ms. A longer pause ends a burst and is not counted.
- **Counted keys.** Keydowns of letters, digits, punctuation, space, Enter,
  Backspace and Tab. The number row counts as TOP. Space and the modifiers
  below the bottom row count as THUMB. Key-up, autorepeat, arrows, F-keys and
  the keypad are ignored. How long a key is held is never measured.
- **Minimum.** A window with fewer than 40 counted keydowns, or fewer than 20
  in-burst gaps, is dropped. Nothing is sent for it, not even "dropped".

The tempo buckets are set for desk typing, where a typical gap is about
240 ms. They are not the firmware's spell buckets, which are faster.

`host/arcane_host/typing_summary.py` is the reducer. Its tests check that the
output is bounded, that sparse windows are dropped, and that two streams
which differ key by key but fall in the same buckets give the same summary.

## Why this little

Anyone who can read the summaries could be any program running as you. Here
is what each limit guards against.

- **Recovering text from timing.** The time between particular key pairs
  leaks about one bit per character, and that is enough to shrink a password
  search a lot. The summary has no per-key or per-pair time. One mean and one
  spread over a minute, in four and three buckets, cannot be turned back into
  a sequence.
- **Short secrets.** A PIN typed alone in a minute is the worst case. The
  40-key minimum drops that window. In a busy window it is a few keys out of
  40 or more, and moves a bucket at most.
- **Who is typing.** Hold times and pair timings identify a typist. Neither
  exists here, and a summary carries about 8 bits a minute.
- **Mood and health.** Hold times can hint at illness. None are measured.
  A long log of tempo could still show daily patterns. That is the risk you
  accept when you turn the helper on.
- **What was typed.** Row counts could hint at content, such as lots of
  digits or Backspace. Only the busiest row and a three-step share leave, and
  digits and Backspace both count as TOP.

## Where it reads from

On Linux the helper reads evdev (`/dev/input/event*`) read-only and never
grabs a device. It uses a device only if it reports letter keys. A udev rule
gives the user at the active seat access to keyboards other than the Corne,
and only while that session is active. The `input` group is a fallback, and it
means the account can read every keyboard and mouse at all times.

The Corne is never opened. The helper skips any device with the Corne's USB
ID (`4653:0001`, or `CORNE_ARCANE_USB_ID` when set), and the udev rule skips it
too.

## Where it goes

The helper owns its own bus name and sends one signal per kept window. Only
the desktop city listens. The daemon does not, so a summary cannot reach the
heartbeat or the keyboard.
