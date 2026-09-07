FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
	build-essential \
	ca-certificates \
	clang-format \
	g++ \
	g++-12 \
	gcc \
	gcc-12 \
	git \
	linux-headers-generic \
	make \
	meson \
	ninja-build \
	pkg-config \
	python3 \
	&& rm -rf /var/lib/apt/lists/*

ARG R2_REF=6.2.2

RUN git clone --depth 1 --branch "$R2_REF" https://github.com/radareorg/radare2.git /tmp/radare2 \
	&& cd /tmp/radare2 \
	&& meson setup build --prefix=/usr \
	&& ninja -C build \
	&& ninja -C build install \
	&& ldconfig \
	&& rm -rf /tmp/radare2

WORKDIR /src
