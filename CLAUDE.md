# CLAUDE.md — KernelTunDriver

## What this is

The user's **final project for the OTUS Linux kernel development course**: a driver for a virtual
network interface of the **TUN type** — a simplified analogue of `drivers/net/tun.c`. The topic is
the user's own (none of the three course-suggested topics was taken) and was **approved by the
instructor on 2026-09-17**.

**Deadline: 2026-10-01 — effectively the evening of 2026-09-30.** October 1st is the user's first
day at a new job, and there is **no slack left** in the calendar.

The module registers two things at once and wires them together:

- a **character device** `/dev/ktun` — the only channel to userspace;
- a **network interface** `ktun0` via `register_netdev()`.

Two mirrored paths, and the direction flip is the thing to keep straight:

```text
write() to /dev/ktun  -> sk_buff -> netif_rx()      -> stack sees a RECEIVED packet
stack sends to ktun0  -> ndo_start_xmit() -> queue  -> read() from /dev/ktun
```

Control via `ioctl`, statistics in `/proc`, parameters in `/sys`. The course requirement "each
process has its own buffer" is met naturally: every open file descriptor owns its own packet queue.

## Context that lives elsewhere — read it, don't re-derive it

The decision, the plan and the calendar live in the education repo, not here. Memory in Claude Code
is keyed by project path and **does not cross between repos or devices**, so everything that must
survive the jump is in files:

| What | Where |
|------|-------|
| **Requirements R-*, key API facts, tests T1–T12, N-*, stages, cut order** | `/home/admuser/education/Linux/otus_project_tun_requirements.md` — the contract for what "done" means; imported below. Condensed from `docs/SPEC.md`, which the user removed from the repo |
| **Project context: why this topic, requirements coverage, calendar, API map, pitfalls, open questions** | `/home/admuser/education/Linux/otus_project_tun.md` — **read before every session** |
| Session log (one shared journal, no `progress.md` here) | `/home/admuser/education/Linux/progress.md` |
| Kernel track: the rung ladder, homework-repo rules | `/home/admuser/education/Linux/CLAUDE.md` |
| Reading map per topic | `/home/admuser/education/Linux/materials.md` |
| Personal conventions | `/home/admuser/education/conventions.md` — imported below |
| Weekly schedule (Thursday = kernel slot, Mon/Wed = buffers) | `/home/admuser/education/schedule.md` |

Do not copy the calendar or the API map into this file — update `otus_project_tun.md` instead, so
there is one source of truth.

The line below is a real `CLAUDE.md` import. The path is machine-specific: on another machine,
point it at wherever the education repo was cloned.

@/home/admuser/education/conventions.md

Requirements, imported the same way (machine-specific path):

@/home/admuser/education/Linux/otus_project_tun_requirements.md

## Names

| Thing | Name |
|-------|------|
| Repository | `KernelTunDriver` (`git@github.com:Nalowo/KernelTunDriver.git`) |
| Module | `ktun` → `build/ktun.ko` |
| Character device | `/dev/ktun` |
| Network interfaces | `ktun0`, `ktun1`, … |
| Userspace utility | `ktunctl` |

**Never name the module `tun`** — that is the in-tree module, and `/dev/net/tun` is its device. The
`k` prefix avoids both clashes and makes `ktun0` distinguishable from a system `tun0` in `ip link`.

**Renaming from the template is done** (2026-09-17, scaffold): `Makefile`, `src/Kbuild`, the guest
banner in `devtools/initramfs/init` and `README.md` are renamed; `src/` holds the file layout from
requirements §10 with `TODO R-*` stubs. The template's `main.c` and README survive only in git history.

## Naming: the user's style, on the whole project

**Decided 2026-09-17:** the user's own naming style from `conventions.md` applies to **the whole
project — the kernel module included**, not only `ktunctl`. The user put it as "camelCase for the
whole project". This deliberately departs from the kernel coding style (`snake_case`). It is the
user's decision. Do not re-argue it, and do not "fix" names back to `snake_case` in review.

What this means in practice:

- **Locals, parameters, data members** follow `conventions.md`: `camelCase` locals, a leading
  underscore on struct members.
- **Functions — PascalCase** (asked and decided 2026-09-17), as in `conventions.md`:
  `KtunNetStartXmit`, `KtunChrOpen`. Globals are camelCase with a `ktun` prefix (`ktunFops`,
  `ktunList`); struct types too (`struct ktunNet`, `struct ktunAttach`).
- **Sysfs attributes are spelled out with `__ATTR()`**, not `DEVICE_ATTR_RW()`: the macro forces
  snake_case callback names.
- **Not renamed, ever:** kernel API and struct fields (`ndo_start_xmit`, `.unlocked_ioctl`,
  `netif_rx`, `sk_buff`), macros and constants (`UPPER_SNAKE`), and anything that becomes a
  user-visible name — module `ktun`, `/dev/ktun`, `ktun0`, `/proc` and `/sys` entries (lowercase by
  convention of those filesystems).
- **The C reserved-identifier rule still bites:** `_` followed by a capital letter is reserved, so a
  struct member is `_txQueue`, never `_TxQueue`.
- **`checkpatch.pl` will emit `CAMELCASE` warnings.** They are expected and accepted. Run it with
  `--ignore CAMELCASE` so real findings are not buried.
- Other kernel-style points (tabs, 8-column indent, brace placement) were **not** discussed and stay
  as in the template until the user says otherwise.

## How we work here: Claude is the mentor

**Settled 2026-09-17:** "we do it together. I will try to do it myself, and sometimes I'll ask you,
as it goes. You are here as a mentor."

- **The user writes the code** by default, the scaffolding included, unless he hands a piece over.
- **Claude is the mentor:** explains before the user writes, reviews, breaks down blockers, asks
  Socratic questions, points at the next small step. Claude does not take over a piece on its own
  initiative, even under deadline pressure. If the schedule is slipping, **say so** and name the cut
  order from §6 of the context file. Do not quietly write the code instead.
- **When the user asks for code, write it.** "Where I ask you" is an explicit handover. Then walk
  through the piece out loud: **at the defence the instructor can ask about any line**, and the
  user must be able to explain it.
- The final say on correctness belongs to the compiler, KASAN, lockdep and a live `ping`, not to
  Claude.

## The stand

This repo is a copy of `~/projects/KmodQemuTemplate`: the module is built against and loaded into a
**separate kernel running in QEMU**, not the host (WSL2 has no host kernel headers).

```bash
make qemu-setup-debug   # one-time: kernel + BusyBox initramfs WITH KASAN/lockdep/kmemleak
make qemu-build         # build the module against the QEMU kernel -> build/*.ko
make qemu-boot          # interactive guest; project mounted at /mnt/host via 9p
make qemu-test          # automated insmod -> dmesg -> rmmod, prints TEST_OK
make qemu-debug         # guest paused for GDB (terminal 1)
make gdb-attach         # attach GDB (terminal 2)
make compdb             # compile_commands.json for IntelliSense
```

Facts about the stand that matter for this project:

- **Use the debug profile.** A driver that allocates and frees `sk_buff` on many paths, including
  error paths, is exactly what KASAN exists for; `DEBUG_ATOMIC_SLEEP` catches sleeping inside
  `ndo_start_xmit`. KASAN roughly doubles memory — set `QEMU_MEM="2G"` in `devtools/config.local`.
- **The kernel for this repo was being built on 2026-09-17**, locally in `devtools/.cache`, by the
  user's choice (reusing `KernelPCDemo`'s cache was declined). If `make qemu-build` fails, check that
  this build finished and that it is the **debug** profile (`CONFIG_KASAN=y` in
  `devtools/.cache/kernel-build/.config`) before debugging anything else.
- **The kernel has networking but no TUN:** `CONFIG_NET`, `CONFIG_INET`, `CONFIG_NETDEVICES` are on,
  `CONFIG_TUN` is not set in the 6.18.37 builds on this machine. So there is no in-kernel `tun` to
  clash with, and there is no `/dev/net/tun` in the guest to compare against.
- **BusyBox** (defconfig) provides `ip` and `ping`, which is all the demo needs.
- **Expect unsolicited packets.** `defconfig` enables IPv6, so bringing `ktun0` up may make the
  stack send its own packets (IPv6 router solicitations / MLD) before any `ping`. Verify on the
  stand; if they appear, the utility must skip non-IPv4 packets, and they are a free first test of
  the TX path.
- **Dump ftrace before `rmmod`**: module symbols leave kallsyms on unload, so a trace dumped
  afterwards shows bare addresses (learned on rung 0, see `Linux/progress.md`).

## Session start checklist

1. Read `/home/admuser/education/Linux/otus_project_tun.md` — the status line, §6 (calendar) and §9
   (open questions).
2. Compare today's date with §6: which slot is this, is the project on schedule? If behind, apply
   the cut order written in §6 rather than inventing a new one.
3. `git log --oneline -5` here — what actually landed since the last session.

## Session end checklist

1. Append a row to `/home/admuser/education/Linux/progress.md` (date / "— (проектная работа курса)"
   / done / blocker / next).
2. Update the status line and §9 of `otus_project_tun.md` if anything changed.
3. Commit or push only when the user asks. On 2026-09-17 he declined committing the context files.
