#!/bin/sh

docker pull --platform=linux/amd64 espressif/idf:v5.1.1
docker run --platform=linux/amd64 --rm -v $PWD:/project -w /project espressif/idf:v5.1.1 idf.py build
