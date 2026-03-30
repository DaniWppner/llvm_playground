FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

# 1. Install kernel, llvm, golang dependencies
RUN apt-get update && apt-get install -y \
    #LLVM pass dependencies
    build-essential cmake python3 zlib1g wget subversion unzip git \
    # ninja-build is a faster alternative to make for cmake projects
    ninja-build \
    # LLVM 14 toolchain 
    clang-14 llvm-14 \
    && rm -rf /var/lib/apt/lists/*

# 2. Set LLVM 14 as the default LLVM version
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100 && \
    update-alternatives --install /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-14 100 && \
    update-alternatives --install /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-14 100 && \
    update-alternatives --install /usr/bin/llvm-objcopy llvm-objcopy /usr/bin/llvm-objcopy-14 100 && \
    update-alternatives --install /usr/bin/llvm-objdump llvm-objdump /usr/bin/llvm-objdump-14 100 && \
    update-alternatives --install /usr/bin/llvm-readelf llvm-readelf /usr/bin/llvm-readelf-14 100 && \
    update-alternatives --install /usr/bin/llvm-strip llvm-strip /usr/bin/llvm-strip-14 100 && \
    update-alternatives --install /usr/bin/llvm-link llvm-link /usr/bin/llvm-link-14 100 && \
    update-alternatives --install /usr/bin/llvm-as llvm-as /usr/bin/llvm-as-14 100 && \
    update-alternatives --install /usr/bin/llvm-dis llvm-dis /usr/bin/llvm-dis-14 100 && \
    update-alternatives --install /usr/bin/opt opt /usr/bin/opt-14 100 && \
    update-alternatives --install /usr/bin/llc llc /usr/bin/llc-14 100 && \
    update-alternatives --install /usr/bin/llvm-config llvm-config /usr/bin/llvm-config-14 100 && \
    update-alternatives --install /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-14 100 && \
    update-alternatives --install /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-14 100 && \
    update-alternatives --install /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-14 100 && \
