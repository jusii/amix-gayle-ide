# Changelog

## 2026-09-29 (amigaux.org fork)

- **Kernel patches:** `support.c.patch` dropped — the A3000 SCSI auto-detection patch is
  `amix-a4091`'s (with the Agnus-id low-nibble gate) and the build hub applies it to every kernel.
  `kernel.c.patch` regenerated against stock `master.d/kernel.c` so it applies at zero fuzz.
- **Probe:** automatic detection is A4000-only (VPOSR gate, then the A4000 task-file readback); the
  Gayle ID register at `0xDE1000` is read only when an A1200/A600 row is selected with
  `ide_board_force`, never on an A3000 or a driveless A4000. The readback runs before the BSY wait, so
  an empty port no longer spins `ide_spins_reset`.
- **Queue:** `idequeue()` ignores a request re-submitted while it is active or queued (`dd.c`'s
  iodone-before-startio order can do that; `ide_dup_submits` counts). SYNCHRONIZE CACHE and VERIFY(10)
  take the request queue in interrupt mode instead of running inline over a transfer in flight.
- **Recovery:** after a timeout on either path the cable gets a software reset (SRST, bounded !BSY
  wait, STATUS read to drop a pending INTRQ, nIEN restored; `ide_resets` counts) before the next request
  starts; the watchdog counts ticks per request generation.
- **Correctness:** VERIFY(10) executes ATA READ VERIFY SECTOR(S) instead of answering GOOD (BYTCHK
  refused); DF without ERR is a hardware error; the LBA + count range check no longer wraps at 2^32;
  a LUN other than 0 gets LUN NOT SUPPORTED (INQUIRY: qualifier 011b); a full completion FIFO
  delivers nested instead of dropping; interrupts taken while BSY and raw requests crossing a page
  boundary are counted (`ide_intr_busy`, `ide_pagecross_n`, page size `IDE_NBPP`).
- **Docs:** README, design record and source notes aligned (unit 6 / `c6d0`, one byte-order
  formulation, remaining `[verify]` items listed, a section on sharing a kernel with a SCSI adapter,
  licensing note vs the patch's quoted context lines, A1200/A600 marked untested).
- **Host harness:** 439 → 787 checks; the mock gained a command-order log, a hang-until-SRST drive,
  an SRST counter and a device-fault sector.

## 2026-09-28

- **First release** (private repo, shared with the amigaux.org maintainers). License decided: MIT (`LICENSE`). Repository shaped after the
  amix-kerntools driver-author contract (`driver.conf`, `src/`, `src/kernel-patches/` + `NOTICE`,
  `docs/`, `test/host/`).

## 2026-09-27

- **Real hardware: Amix boots from IDE on an Amiga 4000/030** (Commodore A4000/030 card, 68882,
  Kickstart 3.1, SanDisk 1 GB CF on a 44-pin adapter). Kickstart autoboots the `UNIX_Boot` partition,
  the kernel mounts root `c6d0s1` and swap `c6d0s2` through this driver, root login works. IDENTIFY
  model string and sector count decode correctly, confirming the DATA-register byte-order model.
- **Phase 2 — interrupts and board table.** `ideintr()` registered in `int2_tbl[]` via
  `kernel.c.patch`; one interrupt per sector, `timeout()` watchdog, polled fallback when the hook is
  absent. `struct ideboard` confines every board-specific address and interrupt convention (A4000
  entry active; A1200/A600 entries prepared). In the Amiberry A4000/AGA profile: 107k interrupts,
  2 spurious, 0 timeouts; sys time for a 10 MB raw read 3.4 s → 0.7 s. Host harness 439 checks.
- **Phase 1 — polled PIO pseudo SCSI host adapter.** `idepresent()` probe (AGA gate via VPOSR, task-file
  write/readback), `idequeue()` translating READ/WRITE(6)/(10), REQUEST SENSE, INQUIRY, TEST UNIT READY,
  READ CAPACITY(10), MODE SENSE(6), START/STOP, SYNCHRONIZE CACHE to ATA-2 commands with synthesised
  sense data. Compiled by the native Amix `cc` without diagnostics on the first attempt; linked through
  amix-kerntools; boots on an A3000 profile with the probe correctly silent; root on IDE boots in the
  Amiberry A4000/AGA profile. IDE master presented as SCSI unit 6 so a stock A3000-installed image
  boots unchanged.
- `support.c.patch` carried with the AGA gate corrected to the low nibble of the VPOSR id
  (`docs/vposr-aga-gate.md`): `id >= 0x22` misclassifies NTSC ECS machines (0x30) as AGA.
- Architecture decided: pseudo SCSI host adapter (Option A), `docs/decision-hba-vs-block.md`.
