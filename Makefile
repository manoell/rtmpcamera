TARGET := iphone:clang:latest:7.0
INSTALL_TARGET_PROCESSES = SpringBoard

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

$(TWEAK_NAME)_FILES = \
    RTMPCameraTweak.xm \
    rtmp_core.c \
    rtmp_protocol.c \
    rtmp_handshake.c \
    rtmp_commands.c \
    rtmp_utils.c \
    rtmp_amf.c \
    rtmp_chunk.c \
    rtmp_stream.c \
    rtmp_session.c \
    rtmp_quality.c \
    rtmp_failover.c \
    rtmp_preview.m \
    rtmp_camera_compat.m

$(TWEAK_NAME)_CFLAGS = -fobjc-arc
$(TWEAK_NAME)_FRAMEWORKS = UIKit AVFoundation CoreMedia VideoToolbox
$(TWEAK_NAME)_PRIVATE_FRAMEWORKS = MediaToolbox

include $(THEOS_MAKE_PATH)/tweak.mk