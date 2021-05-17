em28xx-y +=	em28xx-core.o em28xx-i2c.o em28xx-cards.o em28xx-camera.o

em28xx-v4l-objs := em28xx-video.o em28xx-vbi.o
em28xx-alsa-objs := em28xx-audio.o
em28xx-rc-objs := em28xx-input.o

obj-$(CONFIG_VIDEO_EM28XX) += em28xx.o
obj-$(CONFIG_VIDEO_EM28XX_V4L2) += em28xx-v4l.o
obj-$(CONFIG_VIDEO_EM28XX_ALSA) += em28xx-alsa.o
obj-$(CONFIG_VIDEO_EM28XX_DVB) += em28xx-dvb.o
obj-$(CONFIG_VIDEO_EM28XX_RC) += em28xx-rc.o

ccflags-y += -Idrivers/media/i2c
ccflags-y += -Idrivers/media/tuners
ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends
ccflags-y += -Idrivers/media/pci/ptx

#only for test, remove later!
#SRCK := /usr/src/kernel-$(KVER) | sed s/-server//g | sed s/-desktop//g
ccflags-y += -I/usr/src/kernel-4.1.15-2.mga5/drivers/media/tuners
ccflags-y += -I/usr/src/kernel-4.1.15-2.mga5/drivers/media/dvb-frontends
#$(shell rm -f )

#drivers/media/usb/em28xx/em28xx-camera.c
#drivers/media/usb/em28xx/em28xx-i2c.c
#drivers/media/usb/em28xx/em28xx-core.c
#drivers/media/usb/em28xx/em28xx-reg.h