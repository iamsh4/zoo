#!/usr/bin/env bash

# This is a simple command to mimic what happens in our CI, allowing for testing GCC builds in Fedora.

if ! command -v docker &> /dev/null
then
  echo "Docker is required to run this script"
  exit
fi

# Ensure you have penguin:build image built from the Dockerfile in this directory

SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
REPO_PATH="$SCRIPTPATH/../"

mkdir -p "$REPO_PATH/ci-build"
mkdir -p /tmp/penguin-ci.build-tup
mkdir -p /tmp/penguin-ci.third_party

docker run \
  --privileged \
  -v $REPO_PATH:/source:ro \
  -v /tmp/penguin-ci.third_party:/build/third_party:delegated \
  --cpus 12 \
  -it \
  penguin:build \
  bash -c "
    mkdir -p /build &&
    cp -R /source/{build-tup,.git,Makefile,src,fox,tools,third_party,Tup*} /build && 
    cd /build && 
    rm -f src/guest/sh4/sh4_opcode.cpp &&
    rm -f fox/amd64/amd64_assembler.cpp &&
    rm -f fox/amd64/amd64_assembler.h &&
    rm -f fox/arm64/arm64_assembler{.cpp,.h} &&
    bash
  "
