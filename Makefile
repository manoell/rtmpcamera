# Makefile
ARCHS = arm64 arm64e
TARGET = iphone:clang:latest:14.0
INSTALL_TARGET_PROCESSES = Camera

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

RTMPCamera_FILES = \
    rtmp_core.c \
    rtmp_protocol.c \
    rtmp_chunk.c \
    rtmp_amf.c \
    rtmp_session.c \
    rtmp_stream.c \
    RTMPCameraTweak.xm

RTMPCamera_CFLAGS = -fobjc-arc \
    -I$(THEOS_PROJECT_DIR) \
    -Wno-deprecated-declarations \
    -Wno-unused-variable \
    -Wno-unused-function

RTMPCamera_FRAMEWORKS = \
    AVFoundation \
    CoreMedia \
    CoreVideo \
    UIKit

include $(THEOS_MAKE_PATH)/tweak.mk