# vim: noexpandtab:ts=2:sw=2

# Individual packages will run their steps in parallel, but multiple packages
# will not be built at the same time.
.NOTPARALLEL:

.PHONY : all
all :
	tup

.PHONY : third_party
third_party :

.PHONY : third_party-imgui
third_party : third_party-imgui
third_party-imgui :
	git submodule update --init --recursive third_party/imgui
	cd third_party/imgui && $(MAKE) -f ../imgui.mk

.PHONY : third_party-libchdr
third_party : third_party-libchdr
third_party-libchdr :
	git submodule update --init --recursive third_party/libchdr
	cd third_party/libchdr && $(MAKE) -f ../libchdr.mk

.PHONY : third_party-wgpu-native
third_party : third_party-wgpu-native
third_party-wgpu-native :
	git submodule update --init --recursive third_party/wgpu-native
	cd third_party/wgpu-native && $(MAKE) lib-native

.PHONY : distclean
distclean :
	cd third_party/imgui && git clean -fxd
	cd third_party/libchdr && git clean -fxd

.PHONY : format
format :
	find src -name *.cpp | xargs clang-format -i
	find src -name *.h | xargs clang-format -i
