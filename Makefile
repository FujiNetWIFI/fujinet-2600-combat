# Makefile -- a convenience wrapper. build.sh is the real build.
#
# Networked two-player Combat for the Atari 2600 over the FujiNet cartridge
# mailbox. See the milestone ladder in PORTING.md; every target below is a
# gate, and each one has to pass before the next is worth starting.

FUJI_FIRMWARE ?= $(HOME)/Workspace/fn-2600
MAME_DIR      ?= $(HOME)/Workspace/mame
SECS          ?= 30
PORT          ?= 9600

SRC := $(wildcard src/*.asm src/*.inc)

.PHONY: all verify-org probe echo combat frames det inputs lag sim lobby session rig rig-hold rig-play rig-repair ladder play stop clean

all: verify-org

# M0 -- the conversion gate. rom/combat.asm is the pristine DASM disassembly
# and is never edited; this regenerates the AS translation from it every run
# and requires the result to be the cartridge dump, byte for byte.
verify-org:
	FUJI_FIRMWARE="$(FUJI_FIRMWARE)" ./build.sh verify-org

clean:
	rm -rf build

# M1 -- the transaction latency probe. THE FIRST THING THAT RUNS: K and d are
# derived from this number, and PORTING.md §2 exists because a family that
# guessed it was wrong by 3x for years.
probe: build/probe.bin
build/probe.bin: src/probe.asm src/cbcore.inc src/cbdefs.inc src/fujinet.inc src/vcs.inc build.sh
	FUJI_FIRMWARE="$(FUJI_FIRMWARE)" ./build.sh probe

echo: probe
	SECS=$(SECS) test/run_probe.sh

# M2 -- the bank split. Combat, relaid across three 2K banks and the fixed half,
# with the netcode not yet in it: a split build must play EXACTLY like stock, so
# that any difference later is the netcode's fault and nothing else's.
combat: build/combat.bin
build/combat.bin: $(SRC) build.sh tools/mkbanks.py tools/patches.py tools/dasm2as.py
	FUJI_FIRMWARE="$(FUJI_FIRMWARE)" CBLAG="$(CBLAG)" ./build.sh combat

build/stock.bin: rom/combat.bin
	@mkdir -p build && cp rom/combat.bin build/stock.bin

# Every frame the same length, through two bank switches a frame. Combat is NOT
# 262 lines -- it has no overscan and its kernel runs to the last line -- so the
# gate is a constant, and it is compared against stock rather than a spec.
frames: build/stock.bin
	ENDPOINT="TCP://127.0.0.1:9699/" FUJI_FIRMWARE="$(FUJI_FIRMWARE)" ./build.sh combat >/dev/null
	SECS=$(SECS) SLOT=a26_2k_4k ./run.sh stock frames | tail -4
	SECS=$(SECS) ./run.sh combat frames | tail -4

# M2/M3 -- the split and the input shim both play like stock, frame for frame.
# DET_QUIET, because injecting stick movement through MAME's ports is not
# frame-exact between two builds; see the note in emu/det.lua.
# ENDPOINT deliberately points at a port nothing listens on. This gate tests the
# LOCAL path -- the claim that a patched Combat plays exactly like the 1977 ROM
# -- and it reaches that path through the session's fallback. Built against the
# real endpoint it would depend on whether a relay happened to be running on
# this machine, and `make play` leaves one up.
det: DETENDPOINT = TCP://127.0.0.1:9699/
det: build/stock.bin
	ENDPOINT="$(DETENDPOINT)" FUJI_FIRMWARE="$(FUJI_FIRMWARE)" ./build.sh combat >/dev/null
	DET_QUIET=1 SECS=$(SECS) SLOT=a26_2k_4k ./run.sh stock det 2>/dev/null \
	    | grep -E '^[0-9]+ [0-9A-F]{4}$$' > build/det_stock.txt
	DET_QUIET=1 SECS=$(SECS) ./run.sh combat det 2>/dev/null \
	    | grep -E '^[0-9]+ [0-9A-F]{4}$$' > build/det_split.txt
	python3 tools/ramdiff.py build/det_stock.txt build/det_split.txt

# M3 -- the interception proof, in two forms. The static one is the stronger:
# after the patch the game's own seven input sites are GONE and every read of a
# console port in the whole run comes from CBSHIM.
inputs: build/stock.bin
	ENDPOINT="TCP://127.0.0.1:9699/" FUJI_FIRMWARE="$(FUJI_FIRMWARE)" ./build.sh combat >/dev/null
	SECS=$(SECS) ./run.sh combat inputs 2>/dev/null | sed -n '/^SITES/,$$p'

lag:
	CBLAG=1 $(MAKE) combat
	SECS=$(SECS) ./run.sh combat lag | tail -3
	$(MAKE) combat

# The protocol, with no emulator anywhere. Runs in a second and catches every
# framing and pairing mistake the ROM would otherwise find at fifteen ticks a
# second through two emulators and two fujinet-pc instances.
sim:
	python3 tools/combat_client_sim.py

# The Lobby registration contract, against a MOCK lobby on an ephemeral port.
# Never the real one: this family has rewritten a machine-wide appkey by
# pointing a test at production before now.
lobby:
	python3 tools/test_lobby_pub.py

# M5 -- the session: appkey-less for now, but a real socket, a real HELLO and a
# real wait. One console, because half the handshake needs no opponent.
session:
	SECS=$(SECS) test/run_sess.sh $(SECS)

# M6/M7 -- the whole thing: two consoles, two FujiNets, one relay, one match.
# SELECT then RESET, pressed on the HOST console only, obeyed by both.
rig:
	SECS=$(SECS) test/run_rig.sh $(SECS)

# The case a person found and no scheduled tap-and-release ever did: SELECT HELD
# DOWN for the whole run, so Combat's once-a-second debounce walks the variation
# onwards again and again. Two consoles that re-arm at even slightly different
# moments end up playing different games -- a maze on one screen and biplanes on
# the other -- while every checksum they exchange agrees, because nothing has
# yet happened to move the tanks apart. Its own gate because it is the one that
# broke, and 45 seconds because five advances is what it takes to show.
rig-hold:
	RIG_HOLD=select SNAPTICK=250 SECS=45 test/run_rig.sh 45

# The gate the rig never had: two consoles with their HANDS ON THE STICK,
# playing asynchronously the way two people do, and a per-tick diff of the sim
# state that names the first cell to disagree. Every switch gate above was green
# while this was broken, because pressing RESET and SELECT only ever exercises
# the two inputs that are ANDed on the wire and identical on both machines by
# construction.
rig-play:
	RIG_LUA=play SECS=40 test/run_rig.sh 40

# Desync REPAIR, which cannot be tested by waiting for a bug: a correct pair
# never diverges. So one console is deliberately corrupted mid-game -- TankY0
# nudged by one scanline, the smallest desync there is -- and the assertion is
# that the two come back together on their own and stay together.
rig-repair:
	RIG_LUA=play PLAY_INJECT=120 SECS=60 test/run_rig.sh 60

# The whole ladder, in order. Each gate has to pass before the next is worth
# starting, which is the only reason the order is what it is.
ladder: verify-org sim lobby combat frames det inputs rig rig-hold rig-play rig-repair
	@echo "LADDER: every gate passed"

# Two consoles, side by side, open-ended. The rig proves it; this is for
# watching it.
play:
	test/run_play.sh
stop:
	test/stop.sh
