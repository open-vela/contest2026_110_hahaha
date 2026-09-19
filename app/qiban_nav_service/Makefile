############################################################################
# Contest 2026 team 110 qiban navigation service - Makefile
############################################################################

include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../external/curl/curl/include

PROGNAME  = qiban_nav_service
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 24576
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_NAV_SERVICE)

MAINSRC = qiban_nav_service_main.c

include $(APPDIR)/Application.mk
