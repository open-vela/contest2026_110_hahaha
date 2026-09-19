############################################################################
# Contest 2026 team 110 qiban video service - Makefile
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = qiban_video_service
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = 32768
MODULE    = $(CONFIG_LVX_USE_DEMO_CONTEST2026_110_QIBAN_VIDEO_SERVICE)

MAINSRC = qiban_video_service_main.c

# Link against libcedarx for hardware video decoding
CSRCS +=

ifeq ($(CONFIG_MULTIMEDIA_LIBCEDARX),y)
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/inc
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/xplayer/include
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/libcore/playback/include
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/libcore/base/include
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/libcore/parser/include
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/libcore/parser/mov
CFLAGS += -I$(APPDIR)/../vendor/allwinnertech/chips/r528/components/multimedia/src/libcedarx/decoding/include
endif

include $(APPDIR)/Application.mk
