# Contributing

This is one person's keyboard, published because it might be useful or
interesting to someone else. Issues and patches are welcome, and there is no
roadmap you need to fit into.

Before anything else, please read the one rule that is easy to break by
accident.

## Goldens are reviewed, never regenerated

`firmware/sim_test/golden/visual_current.hashes` holds 622 exact framebuffer
hashes. When a visual test fails, the failure is the point. It is telling you
that a rendering change happened, and the job is to look at that change and
decide whether it was intended.

Do not re-baseline to make the suite green. Instead, dump the frames and look
at them:

```bash
firmware/sim_test/visual_runner --dump-pgm /tmp/frames
python3 tools/contact_sheet.py /tmp/frames sheet --only <the changed scene>
```

A golden moves only in the same change that explains the new presentation
contract and tests it. The same applies to the figures in the documentation,
which come from that dump through `tools/figures.py` and should be regenerated
in the change that moves the goldens.

## Running the checks

```bash
make test      # mechanics, 622 visual scenes, allocation scan, host tests
make lint      # ruff and clang-format
make hygiene   # repository conventions
```

All three run in CI on every push, so a green local run usually means a green
pull request. `make test` needs a C compiler and Python but no keyboard and no
QMK checkout.

## Two invariants that are load-bearing

**Determinism.** The simulation uses fixed ticks, integer arithmetic, explicit
state, and no time reads inside mechanics. Nothing that allocates or reads a
clock belongs in the simulation path. This is what makes a catalog of exact
hashes a usable test.

**Privacy.** The host normalizes to bounded enums, counters, durations, and
salted digests before anything is retained. Window titles, URLs, file paths,
command lines, notification bodies, and typed text have no path into retained
state or onto either wire protocol. That property comes from the shape of the
code, not from a filter, and a change that would make it depend on filtering is
a change to the architecture.

If you are unsure whether something crosses one of these, say so in the issue
or the pull request and it can be worked out there.

## Style

`make lint` settles formatting, so there is nothing to memorise. Commit
messages describe what changed and why, in ordinary sentences.

For the vocabulary used throughout the code and documentation, see
[`docs/glossary.md`](docs/glossary.md). For how the simulation works, see
[`docs/duel.md`](docs/duel.md).
