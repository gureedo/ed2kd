#ed2kd

eDonkey2000 server.

### Supported platforms
- Windows
- Linux

Anyway you can try to build it in another little-endian OS.

### Supported databases

- [sqlite3](http://sqlite.org), >=3.7.12 with FTS4 support and unicode61 tokenizer

### Build instructions

You need the following libraries installed:
- [libevent](http://libevent.org/), >= 2.0, event-based network I/O library
- [zlib](http://zlib.net/), compression library
- [libconfig](http://www.hyperrealm.com/libconfig/), >=1.4, confiuguration mamagement library
- [libatomic_ops](https://github.com/ivmai/libatomic_ops/),>=7.2alpha3, atomic operations library

Build using cmake:

cmake . -DDB_BACKEND=\<db_name\>