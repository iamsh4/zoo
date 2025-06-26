# vim: noexpandtab:ts=2:sw=2


#
# Configuration
#

ARTIFACT_PATH := .
CFLAGS := -O3 -std=c++17 -g
UNAME := $(shell uname)
LINKFLAGS :=

# CMake default types are... Debug Release MinSizeRel RelWithDebInfo
BUILD_TYPE := RelWithDebInfo

ifeq ($(UNAME), Darwin)
CFLAGS += -I/opt/homebrew/include
LINKFLAGS += -undefined dynamic_lookup
endif

#
# Rules
#

.PHONY : all
all : $(ARTIFACT_PATH)/libchdr.a

.PHONY : clean
clean :
	rm -rf build
	git clean -fxd

$(ARTIFACT_PATH)/libchdr.a :
	cmake -Bbuild -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) .
	make -C build
	cp build/libchdr-static.a $(ARTIFACT_PATH)/libchdr.a
	cp build/deps/lzma*/liblzma.a $(ARTIFACT_PATH)/liblzma.a
	cp build/deps/zlib*/libz.a $(ARTIFACT_PATH)/libz.a
