#!/bin/sh

WORKSPACE="$(pwd -P)"

docker run -it --rm -v "$WORKSPACE:/workspace" -w /workspace hakarlsson/riscv-picolibc
