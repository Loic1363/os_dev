FROM randomdude/gcc-cross-x86_64-elf

RUN apt-get update 
RUN apt-get upgrade -y
RUN apt-get install -y nasm
RUN apt-get install -y xorriso
RUN apt-get install -y grub-pc-bin
RUN apt-get install -y grub-common

VOLUME /root/env
WORKDIR /root/env

#run theses commands : docker build buildenv -t myos-buildenv

#                      docker run --rm -it -v %cd%:/root/env myos-buildenv
#                      make build-x86_64
#                      exit
#                      qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso
