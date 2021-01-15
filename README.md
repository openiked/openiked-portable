# Portable OpenIKED

[![CMake](https://github.com/openiked/openiked-portable/workflows/CMake/badge.svg)](https://github.com/openiked/openiked-portable/actions?query=workflow%3ACMake)
[![builds.sr.ht status](https://builds.sr.ht/~mbuhl/openiked-portable.svg)](https://builds.sr.ht/~mbuhl/openiked-portable?)

This is a port of OpenBSD's [OpenIKED](https://openiked.org) to different
Unix-like operating systems, including Linux, macOS and FreeBSD.

## Documentation

The official documentation for OpenIKED are the man pages for each tool:

* [iked(8)](https://man.openbsd.org/iked.8)
* [ikectl(8)](https://man.openbsd.org/ikectl.8)
* [iked.conf(5)](https://man.openbsd.org/iked.conf.5)

## Building Portable OpenIKED

### Dependencies

Portable OpenIKED is built using ``cmake``.
It requires a working C compiler, standard library and headers,  a 
``yacc`` compatible parser generator, ``libevent``, and ``libcrypto`` from either
[LibreSSL](https://www.libressl.org/) or [OpenSSL](https://www.openssl.org).

### Building from source

```
git clone https://github.com/openiked/openiked-portable.git
cd openiked-portable
mkdir build && cd build
cmake ..
make
```
