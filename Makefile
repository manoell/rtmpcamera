TARGET := iphone:clang:latest:14.0
INSTALL_TARGET_PROCESSES = SpringBoard

ARCHS = arm64 arm64e

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

$(TWEAK_NAME)_FILES = RTMPCameraTweak.xm \
    rtmp_amf.c \
    rtmp_camera_compat.m \
    rtmp_chunk.c \
    rtmp_commands.c \
    rtmp_compatibility.m \
    rtmp_core.c \
    rtmp_diagnostics.c \
    rtmp_failover.c \
    rtmp_handshake.c \
    rtmp_preview.m \
    rtmp_protocol.c \
    rtmp_quality.c \
    rtmp_server_integration.c \
    rtmp_session.c \
    rtmp_stability.c \
    rtmp_stream.c \
    rtmp_utils.c

# C specific flags
$(TWEAK_NAME)_CFLAGS = -Wno-error -Wno-unused-variable -O2

# Objective-C specific flags
$(TWEAK_NAME)_OBJCFLAGS = -fobjc-arc 

# C++ flags
$(TWEAK_NAME)_CCFLAGS = -std=c11 -O2

# Public frameworks
$(TWEAK_NAME)_FRAMEWORKS = Foundation \
    AVFoundation \
    UIKit \
    CoreMedia \
    CoreVideo \
    CoreGraphics \
    QuartzCore \
    Security

# Private frameworks
$(TWEAK_NAME)_PRIVATE_FRAMEWORKS = MediaToolbox \
    CameraUI \
    PhotoLibrary \
    Camera \
    CMCapture

# Extra linker flags
$(TWEAK_NAME)_LDFLAGS = -Wl,-segalign,4000

include $(THEOS_MAKE_PATH)/tweak.mk