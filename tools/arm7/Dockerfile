FROM fedora:latest
RUN dnf -y update
RUN dnf -y install vim 
RUN dnf -y install arm-none-eabi-gcc-cs

# BUILD CONTAINER IMAGE
# docker build -t arm7di-tools .

# Compile
# docker run -it arm7di-tools
# arm-none-eabi-gcc -march=armv4 -O2 -fomit-frame-pointer -c test.c -o test.o
# arm-none-eabi-objdump -S test.o
