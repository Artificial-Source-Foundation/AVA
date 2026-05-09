FROM ubuntu:24.04

RUN apt-get update && apt-get install -y build-essential git ccache cmake ninja-build libncursesw5-dev pkg-config libboost-dev libdw-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /work
