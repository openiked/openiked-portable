#!/bin/sh

set -e
groupadd _iked
useradd -M -d /var/empty -s $(which nologin) -c "IKEv2 Daemon" -g _iked _iked
