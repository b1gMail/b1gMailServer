#!/bin/bash
set -o errexit
# third-party crypto lives in git submodules (libbcrypt, phc-winner-argon2)
git submodule update --init --recursive
mkdir -p dist-files
DOCKER_BUILDKIT=1 docker build . -f ./docker/Dockerfile -t b1gmailserver:1.0.0 -o - | tar xf - -C ./dist-files/
