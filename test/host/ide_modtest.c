/*
 * ide_modtest.c -- host test of the root-class module form of ../../src/ide.c
 * (-DIDE_MODULE; docs/MODULE-PORT.md, amix-dlm contracts C4-1, C4-2, C3-6).
 *
 * The same driver source as ide_test.c, built with the module switch and
 * HOST_TEST, against the same mock Gayle/ATA model.  The kernel calls the
 * module form adds are mocked here: bprobe(), sd_register(), root_defer(),
 * intr_attach()/intr_detach(), sleep()/wakeup().  Checks:
 *   - the probe order: one VPOSR read, then bprobe() of ALTSTAT 0xDD303A,
 *     then the task-file readback; no command before the attach's IDENTIFY;
 *   - ENXIO, silent and unregistered, on OCS/ECS (after the single VPOSR
 *     read), on a bprobe() bus error (no task-file access), with no drive,
 *     and on an A1200-shaped machine;
 *   - the registration: sd_register(idequeue, 0, 0xDD2020, "A4000 IDE", 4),
 *     polled requests through it;
 *   - intr: default 0 (no root_defer); intr=1 defers; the deferred call
 *     attaches at level 2 and self-tests with one READ VERIFY: pass ->
 *     interrupt I/O, a mute drive or a refused attach -> polled, nIEN set,
 *     the handler detached;
 *   - parameters: defaults, overrides, out-of-range values;
 *   - sd_register and root_defer errors;
 *   - _halt: nIEN set, no command; _unload: nIEN set, the probe forgotten;
 *   - no access to the Gayle ID register 0xDE1000 on any of these paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mock_gayle.h"

/* the kernel ABI as the driver sees it (mirrors stubs/sd.h, host types) */
struct sdcom {
	volatile struct sdcom	*next;
	char		reading, okay;
	unsigned char	status, cdb[12];
	char		*addr;
	unsigned	nbyte, card, unit;
	void		(*intr)();
};

extern int	gayle_load(), gayle_unload();
extern void	gayle_halt();
extern char	idequeue();
extern void	idereset();
extern unsigned long	ide_spins, ide_spins_reset, ide_spins_short;
extern unsigned long	ide_intr_count, ide_intr_spurious, ide_timeout_secs;
extern unsigned long	ide_halted, ide_selftest, ide_selftest_ticks;
extern int	ide_slave_enable, ide_debug, ide_master_unit, ide_slave_unit, ide_intr_enable;

extern int	mock_ipl;
extern long	mock_spl_calls, mock_splx_calls;
extern char	mock_console[];
extern void	mock_spl_reset(), mock_console_reset(), mock_fire_timeouts(), mock_timeouts_reset();
extern int	mock_timeouts_pending();

#define ENXIO_	6
#define EBUSY_	16
#define ENOSPC_	28
#define EEXIST_	17

static int	failures, checks;
static unsigned long	id_total;	/* 0xDE1000 accesses over every case */

#define CHECK( cond, msg) do { checks++; if (!(cond)) { failures++; \
	fprintf( stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- the kernel calls of the module form, mocked ----------------------- */

static int		bp_calls, bp_ret, bp_v;
static unsigned long	bp_addr, bp_accesses_before;

int
bprobe( addr, v)
char	*addr;
int	v;
{
	bp_calls++;
	bp_addr = (unsigned long)addr;
	bp_v = v;
	bp_accesses_before = mock_accesses;	/* register accesses made before the guard */
	return bp_ret;
}

static int		sr_calls, sr_ret;
static char		(*sr_fn)();
static unsigned		sr_c, sr_rank;
static char		*sr_base, *sr_name;

int
sd_register( fn, c, base, name, rank)
char	(*fn)();
unsigned c, rank;
char	*base, *name;
{
	sr_calls++;
	sr_fn = fn; sr_c = c; sr_base = base; sr_name = name; sr_rank = rank;
	return sr_ret;
}

static int		rd_calls, rd_ret;
static int		(*rd_fn)();
static char		*rd_arg;

int
root_defer( fn, arg)
int	(*fn)();
char	*arg;
{
	rd_calls++;
	rd_fn = fn;
	rd_arg = arg;
	return rd_ret;
}

static int		ia_calls, ia_ret, ia_level, id_calls, id_level;
static int		(*ia_fn)(), (*id_fn)();
static char		*ia_arg, *ia_name;
static int		(*handler)();	/* attached at level 2 */
static int		claims, unclaimed;

int
intr_attach( level, fn, arg, name)
int	level;
int	(*fn)();
char	*arg, *name;
{
	ia_calls++;
	ia_level = level; ia_fn = fn; ia_arg = arg; ia_name = name;
	if (ia_ret == 0)
		handler = fn;
	return ia_ret;
}

int
intr_detach( level, fn, arg)
int	level;
int	(*fn)();
char	*arg;
{
	id_calls++;
	id_level = level; id_fn = fn;
	if (fn == handler)
		handler = 0;
	return 0;
}

/* p2int stand-in: run the attached chain while the board asserts INT2 */
static void
deliver()
{
	int	old, guard = 1000;

	while (handler && mock_int2_asserted() && guard-- > 0) {
		old = mock_ipl;
		mock_ipl = 2;
		if ((*handler)( (char *)0))
			claims++;
		else
			unclaimed++;
		mock_ipl = old;
	}
}

static int		sl_calls, sl_badipl, wk_calls;

int
wakeup( chan)
char	*chan;
{
	wk_calls++;
	return 0;
}

/* One clock tick passes while the caller sleeps: the callouts fire, and a
 * pending INT2 is taken (the idle loop runs at level 0). */
int
sleep( chan, pri)
char	*chan;
int	pri;
{
	sl_calls++;
	if (mock_ipl != 2)
		sl_badipl++;
	mock_fire_timeouts();
	deliver();
	return 0;
}

/* ---- helpers ------------------------------------------------------------ */

static int	intr_calls;
static struct sdcom *intr_last;

static void
done( cp)
struct sdcom	*cp;
{
	intr_calls++;
	intr_last = cp;
}

static void
fresh( ndrives)
int	ndrives;
{
	id_total += mock_id_reads + mock_id_writes;
	mock_free();
	mock_reset();
	mock_spl_reset();
	mock_console_reset();
	mock_timeouts_reset();
	idereset();
	mock_board = MOCK_BOARD_A4000;
	ide_spins = 200000;
	ide_spins_reset = 200000;
	ide_spins_short = 20000;
	ide_intr_count = ide_intr_spurious = 0;
	ide_halted = ide_selftest = ide_selftest_ticks = 0;
	bp_calls = bp_ret = 0; bp_addr = 0; bp_v = 0; bp_accesses_before = 0;
	sr_calls = sr_ret = 0; sr_fn = 0; sr_base = sr_name = 0; sr_c = sr_rank = 99;
	rd_calls = rd_ret = 0; rd_fn = 0; rd_arg = (char *)1;
	ia_calls = ia_ret = id_calls = 0; ia_fn = id_fn = 0; handler = 0;
	claims = unclaimed = 0;
	sl_calls = sl_badipl = wk_calls = 0;
	intr_calls = 0; intr_last = 0;
	if (ndrives >= 1)
		mock_add_drive( 0, 4096L, "MOCK CF CARD 2GB", 1);
	if (ndrives >= 2)
		mock_add_drive( 1, 2048L, "MOCK SLAVE", 0);
}

static void
quiet_enxio( msg)
char	*msg;
{
	CHECK( sr_calls == 0, msg);
	CHECK( rd_calls == 0, "ENXIO: no root_defer");
	CHECK( mock_console[0] == '\0', "ENXIO is silent");
	CHECK( mock_cmds() == 0, "ENXIO: no ATA command issued");
	CHECK( mock_id_reads + mock_id_writes == 0, "ENXIO: 0xDE1000 untouched");
}

/* READ(10) of one sector at lba through the registered queue function */
static void
read1( cp, unit, lba, buf)
struct sdcom	*cp;
int		unit;
unsigned long	lba;
char		*buf;
{
	memset( cp, 0, sizeof *cp);
	cp->cdb[0] = 0x28;
	cp->cdb[2] = lba >> 24; cp->cdb[3] = lba >> 16; cp->cdb[4] = lba >> 8; cp->cdb[5] = lba;
	cp->cdb[8] = 1;
	cp->addr  = buf;
	cp->nbyte = 512;
	cp->unit  = unit;
	cp->intr  = done;
	cp->reading = 1;
	intr_calls = 0;
	(*sr_fn)( 0, cp);
	CHECK( mock_spl_calls == mock_splx_calls, "spl bracket balanced after the queue call");
}

/* load with one drive and intr=1, run the deferred call */
static int
load_intr( params)
char	*params;
{
	int	e;

	e = gayle_load( params);
	CHECK( e == 0, "intr=1: loads");
	CHECK( rd_calls == 1 && rd_fn != 0 && rd_arg == 0, "intr=1: root_defer(fn, 0) called once");
	if (rd_fn == 0)
		return -1;
	mock_console_reset();
	mock_spl_reset();
	return (*rd_fn)( rd_arg);
}

/* ---- the cases ---------------------------------------------------------- */

static void
test_chipset_gate()
{
	static unsigned short	ids[] = { 0x0000, 0x1000, 0x2000, 0x3000 };	/* OCS, ECS PAL/NTSC */
	int	i, e;

	for (i = 0; i < 4; ++i) {
		fresh( 1);
		mock_vposr_override = 1;
		mock_vposr_value = ids[i];
		e = gayle_load( (char *)0);
		CHECK( e == ENXIO_, "OCS/ECS: ENXIO");
		CHECK( mock_accesses == 1, "OCS/ECS: the single VPOSR read, nothing else");
		CHECK( bp_calls == 0, "OCS/ECS: no bprobe");
		quiet_enxio( "OCS/ECS: not registered");
	}
	fresh( 1);
	mock_board = MOCK_BOARD_NONE;		/* an A3000: ECS, no Gayle */
	e = gayle_load( (char *)0);
	CHECK( e == ENXIO_ && mock_accesses == 1 && mock_bad_accesses == 0, "A3000: ENXIO after VPOSR only");
	quiet_enxio( "A3000: not registered");
}

static void
test_bprobe_fault()
{
	int	e;

	fresh( 1);
	bp_ret = 1;
	e = gayle_load( (char *)0);
	CHECK( e == ENXIO_, "bprobe bus error: ENXIO");
	CHECK( bp_calls == 1, "bprobe called once");
	CHECK( mock_accesses == 1, "bprobe bus error: no task-file access");
	quiet_enxio( "bprobe bus error: not registered");
}

static void
test_probe_order()
{
	int	e;

	fresh( 1);
	e = gayle_load( (char *)0);
	CHECK( e == 0, "A4000 with a drive: loads");
	CHECK( bp_calls == 1, "bprobe called once");
	CHECK( bp_addr == 0xDD303AL, "bprobe reads ALTSTAT at 0xDD303A");
	CHECK( bp_v < 0, "bprobe is a read");
	CHECK( bp_accesses_before == 1, "VPOSR first, then bprobe, before any task-file access");
	CHECK( mock_cmd_log_n >= 1 && mock_cmd_log[0] == 0xEC, "the first command is the attach's IDENTIFY");
	CHECK( mock_cmd_count[0xEC] == 1, "one IDENTIFY (master only)");
	CHECK( mock_slave_selects == 0, "slave never selected by default");
	CHECK( mock_nien == 1, "polled: nIEN set");
	CHECK( mock_bad_accesses == 0, "no access outside the board");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

static void
test_no_drive()
{
	int	e;

	fresh( 0);				/* AGA, the port empty: an A4000T without a drive */
	e = gayle_load( (char *)0);
	CHECK( e == ENXIO_, "no drive: ENXIO");
	CHECK( bp_calls == 1, "no drive: the guard ran");
	quiet_enxio( "no drive: not registered");
	CHECK( mock_accesses < 40, "no drive: settled in a few accesses, no spin");
}

static void
test_a1200_shape()
{
	int	e;

	fresh( 1);
	mock_board = MOCK_BOARD_A1200;		/* AGA, a Gayle at 0xDA0000 */
	e = gayle_load( (char *)0);
	CHECK( e == ENXIO_, "A1200: ENXIO (the module knows the A4000 row only)");
	quiet_enxio( "A1200: not registered");
}

static void
test_register_polled()
{
	struct sdcom	cp;
	static char	buf[512];
	int		e;

	fresh( 1);
	e = gayle_load( (char *)0);
	CHECK( e == 0, "loads");
	CHECK( sr_calls == 1, "sd_register called once");
	CHECK( sr_fn == idequeue, "registers idequeue");
	CHECK( sr_c == 0, "card argument 0");
	CHECK( (unsigned long)sr_base == 0xDD2020L, "base 0xDD2020");
	CHECK( sr_name && strcmp( sr_name, "A4000 IDE") == 0, "name A4000 IDE");
	CHECK( sr_rank == 4, "rank 4 (ide)");
	CHECK( rd_calls == 0, "intr default 0: no root_defer");
	CHECK( strstr( mock_console, "ide0: MOCK CF CARD 2GB") != 0, "attach line printed");
	CHECK( strstr( mock_console, "ide: A4000 IDE at 0xdd2020, polled I/O") != 0, "polled banner");
	CHECK( mock_spl_calls == 0, "the load entry takes no spl (the root pass stays at level 7)");
	CHECK( mock_timeouts_pending() == 0, "the load entry sets no timeout");
	CHECK( sl_calls == 0, "the load entry never sleeps");

	read1( &cp, 6, 3L, buf);
	CHECK( intr_calls == 1 && cp.okay && cp.status == 0, "polled READ(10) completes in the call");
	CHECK( mock_cmd_count[0x20] == 1, "one READ SECTOR(S)");
	read1( &cp, 5, 3L, buf);
	CHECK( intr_calls == 1 && !cp.okay, "unit 5 (slave off): no device");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

static void
test_params()
{
	int	e;

	fresh( 2);
	e = gayle_load( "debug=0 slave=1 master=4 slaveunit=3");
	CHECK( e == 0, "params: loads");
	CHECK( ide_slave_enable == 1 && ide_master_unit == 4 && ide_slave_unit == 3 && ide_intr_enable == 0,
	    "params taken");
	CHECK( mock_cmd_count[0xEC] == 2 && mock_slave_selects > 0, "slave=1: both drives identified");

	fresh( 1);
	e = gayle_load( "intr=7 slave=x master=9");
	CHECK( e == 0, "bad params: loads with the defaults");
	CHECK( ide_intr_enable == 0 && ide_slave_enable == 0 && ide_master_unit == 6, "bad params: defaults");
	CHECK( strstr( mock_console, "ide: parameter intr is not 0 to 1; 0 taken") != 0, "bad intr said");
	CHECK( strstr( mock_console, "ide: parameter master is not 0 to 7; 6 taken") != 0, "bad master said");
	CHECK( rd_calls == 0, "bad intr: no root_defer");

	fresh( 1);
	e = gayle_load( "");
	CHECK( e == 0 && ide_intr_enable == 0 && ide_master_unit == 6 && ide_slave_unit == 5, "empty string: defaults");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

static void
test_intr_selftest_pass()
{
	struct sdcom	cp;
	static char	buf[512];
	int		e;

	fresh( 1);
	e = load_intr( "intr=1");
	CHECK( e == 0, "deferred call returns 0");
	CHECK( ia_calls == 1 && ia_level == 2 && ia_arg == 0, "intr_attach at level 2");
	CHECK( ia_name && strcmp( ia_name, "gayle") == 0, "attached as gayle");
	CHECK( mock_cmd_count[0x40] == 1, "self-test: one READ VERIFY");
	CHECK( claims == 1 && unclaimed == 0, "the handler claimed the self-test's interrupt");
	CHECK( sl_calls >= 1 && sl_badipl == 0, "waited in sleep rounds at sdspl");
	CHECK( ide_selftest == 1, "self-test passed");
	CHECK( ide_intr_spurious == 0, "the self-test's interrupt is not spurious");
	CHECK( id_calls == 0, "not detached");
	CHECK( mock_nien == 0, "interrupt I/O: nIEN clear");
	CHECK( strstr( mock_console, "ide: A4000 IDE, interrupt I/O") != 0, "interrupt banner");
	CHECK( mock_spl_calls == mock_splx_calls && mock_ipl == 0, "spl balanced after the deferred call");

	read1( &cp, 6, 7L, buf);
	CHECK( intr_calls == 0, "interrupt I/O: the READ completes later, not in the call");
	deliver();
	CHECK( intr_calls == 1 && cp.okay && cp.status == 0, "the READ completes from the interrupt");

	gayle_halt();
	CHECK( ide_halted == 1, "halt recorded");
	CHECK( mock_nien == 1, "halt: nIEN set");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

static void
test_intr_selftest_fail()
{
	struct sdcom	cp;
	static char	buf[512];
	unsigned long	cmds;
	int		e;

	fresh( 1);
	mock_drv[0].mute_intrq = 1;		/* the interrupt never arrives */
	ide_timeout_secs = 2;
	e = load_intr( "intr=1");
	ide_timeout_secs = 20;
	CHECK( e == 0, "failed self-test: still 0 (the module stays linked)");
	CHECK( ide_selftest == 2, "self-test failed");
	CHECK( ide_selftest_ticks == 2 * 60, "bounded: ide_timeout_secs * HZ ticks");
	CHECK( id_calls == 1 && id_level == 2 && id_fn == ia_fn, "handler detached");
	CHECK( mock_nien == 1, "polled: nIEN set again");
	CHECK( strstr( mock_console, "ide: interrupt self-test failed, polled I/O") != 0, "failure said");
	CHECK( mock_spl_calls == mock_splx_calls && mock_ipl == 0, "spl balanced");
	cmds = mock_cmds();
	read1( &cp, 6, 7L, buf);
	CHECK( intr_calls == 1 && cp.okay && cp.status == 0, "polled READ completes in the call");
	CHECK( mock_cmds() == cmds + 1, "one command for the READ");

	fresh( 1);
	ia_ret = EEXIST_;
	e = load_intr( "intr=1");
	CHECK( e == 0 && ide_selftest == 2, "refused attach: polled");
	CHECK( mock_cmd_count[0x40] == 0, "refused attach: no self-test command");
	CHECK( mock_nien == 1, "refused attach: nIEN set");
	CHECK( strstr( mock_console, "ide: intr_attach failed (errno 17), polled I/O") != 0, "refusal said");

	fresh( 0);
	mock_add_cdrom( 0, 1000L, "MOCK CD-ROM");	/* an ATAPI master: no ATA drive */
	e = load_intr( "intr=1");
	CHECK( e == 0 && ide_selftest == 2, "ATAPI only: no self-test, polled");
	CHECK( mock_cmd_count[0x40] == 0, "ATAPI only: no READ VERIFY");
	CHECK( id_calls == 1, "ATAPI only: handler detached");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

static void
test_errors()
{
	struct sdcom	cp;
	static char	buf[512];
	int		e;

	fresh( 1);
	sr_ret = EBUSY_;
	e = gayle_load( "intr=1");
	CHECK( e == EBUSY_, "sd_register's errno passed back");
	CHECK( strstr( mock_console, "ide: sd_register failed (errno 16)") != 0, "said");
	CHECK( rd_calls == 0, "no root_defer after a failed registration");

	fresh( 1);
	rd_ret = ENOSPC_;
	e = gayle_load( "intr=1");
	CHECK( e == 0, "root_defer refused: still loads, polled");
	CHECK( strstr( mock_console, "ide: root_defer failed (errno 28), polled I/O") != 0, "said");

	fresh( 1);
	e = gayle_load( (char *)0);
	CHECK( e == 0, "loads");
	e = gayle_unload();
	CHECK( e == 0, "_unload returns 0");
	CHECK( mock_nien == 1, "_unload: nIEN set");
	read1( &cp, 6, 1L, buf);
	CHECK( intr_calls == 1 && !cp.okay, "_unload: the probe forgotten, no device");

	fresh( 0);
	gayle_halt();
	CHECK( ide_halted == 1 && mock_accesses == 0, "halt with nothing attached touches no register");
	CHECK( mock_id_reads + mock_id_writes == 0, "0xDE1000 untouched");
}

int
main()
{
#if IDE_CACHE
	const char	*name = "ide_modtest (040/060: IDE_CACHE, IDE_NBPP 4096)";
#else
	const char	*name = "ide_modtest";
#endif
	fprintf( stderr, "== test_chipset_gate\n"); test_chipset_gate();
	fprintf( stderr, "== test_bprobe_fault\n"); test_bprobe_fault();
	fprintf( stderr, "== test_probe_order\n"); test_probe_order();
	fprintf( stderr, "== test_no_drive\n"); test_no_drive();
	fprintf( stderr, "== test_a1200_shape\n"); test_a1200_shape();
	fprintf( stderr, "== test_register_polled\n"); test_register_polled();
	fprintf( stderr, "== test_params\n"); test_params();
	fprintf( stderr, "== test_intr_selftest_pass\n"); test_intr_selftest_pass();
	fprintf( stderr, "== test_intr_selftest_fail\n"); test_intr_selftest_fail();
	fprintf( stderr, "== test_errors\n"); test_errors();
	fresh( 0);
	CHECK( id_total == 0, "no access to 0xDE1000 on any module path");
	mock_free();
	fprintf( stderr, "%s: %d checks, %d failures\n", name, checks, failures);
	return failures != 0;
}
