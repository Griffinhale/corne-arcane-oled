# The desktop product: its own drawing layers and an autonomous world, over
# the simulation the firmware also compiles.
#
# The sources are the acceptance rig's, read from the shared list, so the
# library cannot drift from what the tests and the firmware compile. Only
# LIB_SRC is new, and nothing under ../sim is touched to build it.
#
# No sanitizers here: this object is loaded into a long-running desktop process
# through ctypes, not run under a test harness. -Werror stays.

CC       ?= cc
CFLAGS   ?= -std=c11 -O2 -g
CFLAGS   += -Wall -Wextra -Werror -fPIC -I../firmware/sim -I.

SIM_DIR := ../firmware/sim
include ../firmware/sim_sources.mk

LIB_SRC := duel_city.c duel_ambient.c duel_town_draw.c
HDRS := $(wildcard ../firmware/sim/*.h) $(wildcard *.h)
LIB  := libcornearcane.so

$(LIB): $(LIB_SRC) $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -shared -o $@ $(filter %.c,$^)

clean:
	rm -f $(LIB)

.PHONY: clean
