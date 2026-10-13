# routertc - Makefile
#
# Targets
#   make test      : build and run the DPDK-free mktdata self-test
#   make relay     : build the DPDK market-data relay (needs RTE_SDK on Linux)
#   make install   : copy scripts + dpdk sources into the UTM OpenWrt router
#                    via scp (set RTC_ROUTER to the router address)
#   make clean     : remove local build artefacts
#
# The DPDK relay cannot be built on macOS (DPDK is Linux-only). Build 'relay'
# on a Linux host that has RTE_SDK/RTE_TARGET exported, see docs/03_dpdk.md.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra

# --- targets needing a full DPDK SDK (Linux only) -------------------------
RTE_SDK ?= /opt/dpdk-stable-22.11
RTE_TARGET ?= x86_64-native-linuxapp-gcc

REL_SRC = dpdk/rte_market_relay.c

.PHONY: test relay install clean

test:
	$(CC) -std=c11 $(CFLAGS) -I. dpdk/test_mktdata.c -o /tmp/routertc_test_mktdata
	/tmp/routertc_test_mktdata

relay:
	@command -v pkg-config >/dev/null 2>&1 && \
	  pkg-config --exists libdpdk && \
	  echo "linking against system DPDK" && \
	  $(CC) $(CFLAGS) $(shell pkg-config --cflags libdpdk) $(REL_SRC) \
	     $(shell pkg-config --libs libdpdk) -o rte_market_relay || \
	  echo "build with:  make -C \$$RTE_SDK/examples  or provide libdpdk.pc"
	@echo "relay build finished"

install:
	@test -n "$(RTC_ROUTER)" || { \
	  echo "set RTC_ROUTER to the router address (e.g. root@192.168.128.100)"; \
	  exit 1; }
	scp -r scripts dpdk docs "$(RTC_ROUTER):/root/routertc/"
	ssh "$(RTC_ROUTER)" 'chmod +x /root/routertc/scripts/*.sh'

clean:
	rm -f /tmp/routertc_test_mktdata rte_market_relay
