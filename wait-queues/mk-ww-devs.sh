#!/bin/bash

mknod /dev/ww-wait c 60 0
mknod /dev/ww-wake c 60 1
