#ed2kd

eDonkey2000 server.

[![Coverity Scan Build Status](https://scan.coverity.com/projects/3436/badge.svg)](https://scan.coverity.com/projects/3436)

### Build instructions

You need the following libraries installed:
- [libevent](http://libevent.org/), >= 2.0, event-based network I/O library
- [zlib](http://zlib.net/), compression library
- [libconfig](http://www.hyperrealm.com/libconfig/), >=1.4, configuration management library

Build using cmake:

```shell
mkdir build
cd build
cmake ..
make
```
