# pdfview-os4 build image — layered on walkero's OS4 GCC toolchain.
#
# Cross-compiles MuPDF and its dependencies for AmigaOS 4 PPC.
# Same walkero base as python-amigaos4 so the toolchain (and clib4 v2.3
# SDK we don't use here yet) is a shared thing across projects.

FROM walkero/amigagccondocker:os4-gcc11

# Native-side tools needed for MuPDF's build: cmake for third_party
# scripts, python3 for build helpers, make/pkg-config/perl for
# subprojects (perl is required by some jbig2dec/openjpeg build steps).
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        make cmake pkg-config perl python3 python3-dev \
        curl ca-certificates git patch xxd zip && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Verify the OS4 toolchain is present.
RUN ppc-amigaos-gcc --version | head -1 && \
    ls /opt/ppc-amigaos/ppc-amigaos/SDK/newlib/lib/libc.so
