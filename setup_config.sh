#!/bin/sh
# Generate iked config directory structure and local key.

set -e

DIR=$1

if [ -z "$DIR" ]; then
	echo "usage: $0 CONFIG_DIR"
	exit 1
fi

mkdir -p "$DIR/ca"
mkdir -p "$DIR/certs"
mkdir -p "$DIR/crls"
mkdir -p "$DIR/private"
mkdir -p "$DIR/pubkeys/ipv4"
mkdir -p "$DIR/pubkeys/ipv6"
mkdir -p "$DIR/pubkeys/fqdn"
mkdir -p "$DIR/pubkeys/ufqdn"

chmod -R 0700 "$DIR/private"

openssl ecparam -genkey -name prime256v1 -noout -out "$DIR/private/local.key"
openssl ec -in "$DIR/private/local.key" -pubout -out "$DIR/local.pub"
