# Configurações do projeto
TWEAK_NAME = RTMPCamera
INSTALL_TARGET_PROCESSES = Camera

# Arquivos fonte
SOURCES = rtmp_amf.c \
         rtmp_camera_compat.m \
         rtmp_chunk.c \
         rtmp_commands.c \
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

# Arquivos de cabeçalho
HEADERS = $(wildcard *.h)

# Flags de compilação
ARCHS = arm64 arm64e
TARGET = iphone:clang:14.5:14.0
DEBUG = 0
FINALPACKAGE = 1

# Flags de otimização
OPTIMIZATION_FLAGS = -O3 -ffast-math -funroll-loops

# Flags adicionais
ADDITIONAL_FLAGS = -fobjc-arc \
                  -Wno-deprecated-declarations \
                  -Wno-unused-variable \
                  -Wno-unused-function

# Frameworks necessários
FRAMEWORKS = UIKit \
            AVFoundation \
            CoreMedia \
            CoreVideo \
            Metal \
            MetalKit \
            QuartzCore \
            OpenGLES \
            Security

# Bibliotecas
LIBRARIES = ssl crypto

# Flags de link
LDFLAGS = -Wl,-dead_strip \
          -Wl,-dead_strip_dylibs \
          -Wl,-bind_at_load

# Diretórios de inclusão
INCLUDE_DIRS = $(THEOS)/include \
               $(THEOS_PROJECT_DIR)/include

# Configurações de compilação
CFLAGS = -I$(INCLUDE_DIRS) \
         $(OPTIMIZATION_FLAGS) \
         $(ADDITIONAL_FLAGS) \
         -DNDEBUG

OBJCFLAGS = $(CFLAGS)
OBJC_ARC = 1

# Regras de compilação
include $(THEOS)/makefiles/common.mk
include $(THEOS_MAKE_PATH)/tweak.mk

# Regras personalizadas
before-all::
	@echo "Building $(TWEAK_NAME)..."

after-install::
	@echo "Installed $(TWEAK_NAME)"

clean::
	rm -rf .theos
	rm -rf packages
	@echo "Cleaned $(TWEAK_NAME)"

# Regras de debug
debug::
	$(MAKE) DEBUG=1

# Regras de release
release::
	$(MAKE) FINALPACKAGE=1

# Regras de instalação
install.device::
	$(MAKE) package install

# Regras de empacotamento
package::
	@echo "Packaging $(TWEAK_NAME)..."
	$(MAKE) stage
	dpkg-deb -Zgzip -b .theos/_ ./packages

# Verificações de dependências
check-dependencies::
	@which dpkg-deb > /dev/null || (echo "dpkg-deb not found"; exit 1)
	@which ldid > /dev/null || (echo "ldid not found"; exit 1)

# Regras de teste
test:: all
	@echo "Running tests..."
	# Adicionar testes aqui

# Regras de documentação
docs::
	@echo "Generating documentation..."
	# Adicionar geração de documentação aqui

# Regras de benchmark
benchmark:: release
	@echo "Running benchmarks..."
	# Adicionar benchmarks aqui

# Regras de profile
profile:: debug
	@echo "Building with profiling..."
	$(MAKE) DEBUG=1 CFLAGS="$(CFLAGS) -pg"

# Configurações específicas do dispositivo
ifeq ($(THEOS_PACKAGE_SCHEME),rootless)
	ARCHS = arm64 arm64e
	TARGET = iphone:clang:16.2:15.0
endif

.PHONY: all clean debug release install.device package check-dependencies test docs benchmark profile