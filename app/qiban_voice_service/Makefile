############################################################################
# Contest 2026 team 110 qiban voice service - Makefile
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/curl/curl/include

PROGNAME  = qiban_voice_service
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 49152
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VOICE_SERVICE)

MAINSRC = qiban_voice_service_main.c

include $(APPDIR)/Application.mk
