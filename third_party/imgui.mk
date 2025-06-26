# vim: noexpandtab:ts=2:sw=2


#
# Configuration
#

ARTIFACT_PATH := .
BACKENDS := opengl3 sdl vulkan
CFLAGS := $(shell pkg-config --cflags sdl2) -O3 -std=c++17 -g
UNAME := $(shell uname)

#
# Rules
#

BACKENDS_CPP := $(patsubst %,backends/imgui_impl_%.cpp,$(BACKENDS))
CORE_OBJ := $(patsubst %.cpp,%.o,$(wildcard *.cpp))
BACKENDS_OBJ := $(patsubst %.cpp,%.o,$(BACKENDS_CPP))

.PHONY : all
all : $(ARTIFACT_PATH)/libimgui.a

.PHONY : clean
clean :
	git clean -fxd

%.o : %.cpp
	$(CXX) -fPIC -I. $(CFLAGS) -c $< -o $@

#$(ARTIFACT_PATH)/libimgui.so : $(CORE_OBJ) $(BACKENDS_OBJ)
#	$(CXX) -shared $(LINKFLAGS) -o $@ $(CORE_OBJ) $(BACKENDS_OBJ)

$(ARTIFACT_PATH)/libimgui.a : $(CORE_OBJ) $(BACKENDS_OBJ)
	ar rc $@ $(CORE_OBJ) $(BACKENDS_OBJ)
