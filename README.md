# OpenIKED

[![License](https://img.shields.io/github/license/openiked/openiked-portable)](https://github.com/openiked/openiked-portable/blob/master/LICENSE)
[![CMake](https://github.com/openiked/openiked-portable/workflows/CMake/badge.svg)](https://github.com/openiked/openiked-portable/actions?query=workflow%3ACMake)
[![#openiked on matrix.org](https://img.shields.io/badge/matrix-%23openiked-blue)](https://app.element.io/#/room/#openiked:matrix.org)
[![#openiked on libera.chat](https://img.shields.io/badge/IRC-%23openiked-blue)](https://kiwiirc.com/nextclient/irc.libera.chat/#openiked)

This is a port of OpenBSD's [OpenIKED](https://openiked.org) to other
Unix-like operating systems including Linux, macOS, FreeBSD and NetBSD.

## Documentation

The official documentation for OpenIKED are the man pages for each tool:

* [iked(8)](https://man.openbsd.org/iked.8)
* [ikectl(8)](https://man.openbsd.org/ikectl.8)
* [iked.conf(5)](https://man.openbsd.org/iked.conf.5)

and the [OpenBSD VPN FAQ](https://www.openbsd.org/faq/faq17.html).

## Installing OpenIKED

### Binary Packages

Binary packages for OpenIKED are available for the package managers of various operating systems and Linux distributions:
* [FreeBSD](https://www.freshports.org/security/openiked/)
* [Debian](https://tracker.debian.org/pkg/openiked)
* [Fedora](https://packages.fedoraproject.org/pkgs/openiked/openiked/index.html)
* [Ubuntu](https://launchpad.net/ubuntu/+source/openiked)
* [Arch Linux User Repository (AUR)](https://aur.archlinux.org/packages/openiked)
* [openSUSE and SUSE Linux Enterprise](https://build.opensuse.org/package/show/network:vpn/openiked)
* [MacPorts](https://ports.macports.org/port/openiked/)
* [Homebrew](https://formulae.brew.sh/formula/openiked)

### Building from source

Portable OpenIKED is built using ``cmake``.
It requires a working C compiler, standard library and headers,  a 
``yacc`` compatible parser generator, ``libevent``, and ``libcrypto`` from either
[LibreSSL](https://www.libressl.org/) or [OpenSSL](https://www.openssl.org).

```
git clone https://github.com/openiked/openiked-portable.git
cd openiked-portable
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
# install
make install
```
A few additional setup steps are required to create the required system group
and user.
The easiest way to do this is running the `useradd.sh script included in the
source repository.
