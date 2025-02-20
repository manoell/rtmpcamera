TARGET := iphone:clang:14.5:12.0
ARCHS = arm64

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

$(TWEAK_NAME)_FILES = RTMPCameraTweak.xm \
                     rtmp_core.c \
                     rtmp_log.c \
                     rtmp_handshake.c \
                     rtmp_chunk.c \
                     rtmp_session.c \
                     rtmp_stream.c \
                     rtmp_amf.c \
                     rtmp_protocol.c \
                     rtmp_preview.m \
                     rtmp_util.c

$(TWEAK_NAME)_FRAMEWORKS = UIKit AVFoundation Foundation VideoToolbox
$(TWEAK_NAME)_CFLAGS = -I. -fobjc-arc
$(TWEAK_NAME)_CCFLAGS = -fobjc-arc
$(TWEAK_NAME)_OBJC_FILES = $(wildcard *.m)
$(TWEAK_NAME)_LOGOS_DEFAULT_GENERATOR = internal

include $(THEOS_MAKE_PATH)/tweak.mk