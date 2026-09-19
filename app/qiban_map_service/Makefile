############################################################################
# Contest 2026 team 110 qiban map service - Makefile
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/curl/curl/include

PROGNAME  = qiban_map_service
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 16384
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_MAP_SERVICE)

MAINSRC = qiban_map_service_main.c

include $(APPDIR)/Application.mk
