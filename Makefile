ARCHS = arm64
TARGET = iphone:clang:14.5:14.1
include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCameraTweak
$(TWEAK_NAME)_FILES = \
    RTMPCameraTweak.xm \
    rtmp_core.c \
    rtmp_handshake.c \
    rtmp_commands.c \
    rtmp_utils.c \
    rtmp_amf.c \
    rtmp_chunk.c

$(TWEAK_NAME)_CFLAGS = -fobjc-arc -O2 -Wall
$(TWEAK_NAME)_FRAMEWORKS = UIKit AVFoundation CoreMedia CoreImage
$(TWEAK_NAME)_LIBRARIES = substrate

include $(THEOS_MAKE_PATH)/tweak.mk

after-install::
	install.exec "killall -9 SpringBoard"