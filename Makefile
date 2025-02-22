TARGET := iphone:clang:latest:7.0
INSTALL_TARGET_PROCESSES = SpringBoard

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = RTMPCamera

# Lista completa de arquivos fonte
RTMPCamera_FILES = \
	rtmp_amf.c \
	rtmp_camera_compat.m \
	rtmp_chunk.c \
	rtmp_commands.c \
	rtmp_core.c \
	rtmp_failover.c \
	rtmp_handshake.c \
	rtmp_preview.m \
	rtmp_protocol.c \
	rtmp_quality.c \
	rtmp_server_integration.c \
	rtmp_stream.c \
	rtmp_utils.c \
	RTMPCameraTweak.xm

# Frameworks necessários
RTMPCamera_FRAMEWORKS = UIKit AVFoundation CoreMedia VideoToolbox
RTMPCamera_PRIVATE_FRAMEWORKS = MediaToolbox

# Bibliotecas extras
RTMPCamera_LIBRARIES = ssl crypto

# Flags de compilação otimizadas
RTMPCamera_CFLAGS = -O2 -fno-strict-aliasing
RTMPCamera_CCFLAGS = -O2 -fno-strict-aliasing
RTMPCamera_OBJCFLAGS = -fobjc-arc

# Diretórios de inclusão
RTMPCamera_INCLUDE_DIRS = include

# Definições
RTMPCamera_DEFINES = RTMP_CAMERA_VERSION=\"1.0.0\"

# Configuração de instalação
PACKAGE_VERSION = $(THEOS_PACKAGE_VERSION)
PACKAGE_BUILDNAME ?= debug

ifeq ($(PACKAGE_BUILDNAME), release)
	RTMPCamera_CFLAGS += -DNDEBUG
	RTMPCamera_CCFLAGS += -DNDEBUG
	RTMPCamera_OBJCFLAGS += -DNDEBUG
else
	RTMPCamera_CFLAGS += -DDEBUG
	RTMPCamera_CCFLAGS += -DDEBUG
	RTMPCamera_OBJCFLAGS += -DDEBUG
endif

after-install::
	install.exec "killall -9 SpringBoard"

include $(THEOS_MAKE_PATH)/tweak.mk