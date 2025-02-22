TARGET := iphone:clang:latest:14.0
INSTALL_TARGET_PROCESSES = SpringBoard

ARCHS = arm64 arm64e

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

$(TWEAK_NAME)_FILES = ./control \
    Filter.plist \
    rtmp_amf.c \
    rtmp_amf.h \
    rtmp_camera_compat.h \
    rtmp_camera_compat.m \
    rtmp_chunk.c \
    rtmp_chunk.h \
    rtmp_commands.c \
    rtmp_commands.h \
    rtmp_compatibility.m \
    rtmp_core.c \
    rtmp_core.h \
    rtmp_diagnostics.c \
    rtmp_diagnostics.h \
    rtmp_failover.c \
    rtmp_failover.h \
    rtmp_handshake.c \
    rtmp_handshake.h \
    rtmp_preview.h \
    rtmp_preview.m \
    rtmp_protocol.c \
    rtmp_protocol.h \
    rtmp_quality.c \
    rtmp_quality.h \
    rtmp_server_integration.c \
    rtmp_server_integration.h \
    rtmp_session.c \
    rtmp_session.h \
    rtmp_stability.c \
    rtmp_stability.h \
    rtmp_stream.c \
    rtmp_stream.h \
    rtmp_utils.c \
    rtmp_utils.h \
    RTMPCameraTweak.xm

$(TWEAK_NAME)_CFLAGS = -fobjc-arc
$(TWEAK_NAME)_CCFLAGS = -std=c11
$(TWEAK_NAME)_FRAMEWORKS = Foundation AVFoundation UIKit CoreMedia CoreVideo
$(TWEAK_NAME)_PRIVATE_FRAMEWORKS = MediaToolbox

include $(THEOS_MAKE_PATH)/tweak.mk