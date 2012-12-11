#ed2kd

eDonkey2000 server.
Currently server is not finished and unstable.

### Supported platforms
- Windows
- Linux

Anyway you can try to build it in another little-endian OS.

### Build instructions

You need the following libraries installed:
- [libevent](http://libevent.org/), >= 2.0, event-based network I/O library
- [zlib](http://zlib.net/), compression library
- [libconfig](http://www.hyperrealm.com/libconfig/), >=1.4, confiuguration management library

Build using cmake:

mkdir build
cd build
cmake ..
make