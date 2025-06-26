FROM fedora:39
RUN dnf install -y 'dnf-command(copr)' 
RUN dnf copr enable ccorsi/gittup -y
RUN dnf install -y \
  tup cmake git rsync cargo \
  gcc-c++ clang rust SDL2-devel glew-devel  doxygen \
  fmt-devel jsoncpp-devel zlib-devel gtest-devel \
  vulkan-headers vulkan-loader-devel vulkan-validation-layers-devel \
  arm-none-eabi-*
