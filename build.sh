#!/bin/bash
set -o errexit
mkdir -p dist-files
DOCKER_BUILDKIT=1 docker build . -f ./docker/Dockerfile -t b1gmailserver:1.0.0 -o - | tar xf - -C ./dist-files/
