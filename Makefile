ARCHS = arm64
TARGET = iphone:clang:14.5:14.1

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCameraTweak

$(TWEAK_NAME)_FILES = \
    RTMPCameraTweak.xm \
    rtmp_core.c \
    rtmp_handshake.c \
    rtmp_protocol.c \
    rtmp_amf.c \
    rtmp_commands.c \
    rtmp_chunk.c \
    rtmp_stream.c \
    rtmp_session.c \
    rtmp_utils.c \
    rtmp_preview.m

$(TWEAK_NAME)_FRAMEWORKS = \
    UIKit \
    AVFoundation \
    CoreMedia \
    CoreImage \
    CoreVideo

$(TWEAK_NAME)_CFLAGS = -fobjc-arc -O2 -Wall
$(TWEAK_NAME)_CCFLAGS = -std=c11
$(TWEAK_NAME)_LIBRARIES = substrate
$(TWEAK_NAME)_LDFLAGS = -Wl,-segalign,4000

include $(THEOS_MAKE_PATH)/tweak.mk

after-install::
	install.exec "killall -9 SpringBoard"