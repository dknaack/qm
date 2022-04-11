#!/bin/sh

set -e

CFLAGS="-O2 -std=c11 -Wall -pedantic -I."

mkdir -p build/
cc $CFLAGS -o build/qm qm/main.c
