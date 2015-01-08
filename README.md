spockfs
=======

SpockFS is an HTTP based network filesystem

It is built upon plain HTTP methods and headers (no XML, no XML and no XML) and supports all of the FUSE posix-related hooks (yes you can manage symlinks too).

This page is mainly for Specs, if you only want to download a server and a client (for Linux, FreeBSD and OSX) jump here: https://github.com/unbit/spockfs/blob/master/README.md#the-reference-fuse-client

SPECS 0.1
=========

Note: we use the term "object" for identifying filesystem items. They can be directories, files, symlinks or whatever supported by the specs. When the term "object" is used it means the method can be applied to any kind of resource.

To avoid "collisions" with WebDAV services, new methods have been added, while the "classic" ones are used where possible.

The following methods have been added:

* READDIR
* GETATTR
* MKNOD
* OPEN
* CHMOD
* CHOWN
* TRUNCATE
* ACCESS
* SYMLINK
* READLINK
* RMDIR
* MKDIR
* LINK
* RENAME
* FALLOCATE
* STATFS
* LISTXATTR
* GETXATTR
* SETXATTR
* REMOVEXATTR
* UTIMENS

While the following "standard" ones are used:

* POST
* PUT
* GET
* DELETE

A group of headers are required to manage filesystem-related informations or special operations. All headers are prefixed with `X-Spock-`.

The following ones are for stat()-related operations:

* X-Spock-mode (for mode_t values and similar)
* X-Spock-uid (for uid)
* X-Spock-gid (for gid)
* X-Spock-size (for specifying sizes)
* X-Spock-mtime (the modification time in unix time)
* X-Spock-atime (the access time in unix time)
* X-Spock-ctime (the creation time in unix time)
* X-Spock-nlink (the number of links of an object)
* X-Spock-blocks (the number of blocks os an object)
* X-Spock-dev (the device id)
* X-Spock-ino (the inode number, unused by default in FUSE)
* X-Spock-flag (generic flag, used by open() too)
* X-Spock-target (generic string used for symlink values, rename operations and for the names of extended attributes)

The following ones are for statvfs() calls, they map 1:1 with the stavfs struct, and you will use them only if you want to implement the STATFS method in your server/client:

* X-Spock-bsize
* X-Spock-frsize
* X-Spock-bfree
* X-Spock-bavail
* X-Spock-files
* X-Spock-ffree
* X-Spock-favail
* X-Spock-fsid
* X-Spock-namemax


Finally these three "standard" headers are used:

* Content-Length (required for every HTTP/1.1 response)
* Range (for GET and FALLOCATE methods)
* Content-Range (for PUT method)

Obviously you can use all of the standard headers you want: all of the request/response cycles of SpockFS are HTTP compliant.

Errors are managed with this simple http_code->errno mapping:

* 403 Forbidden -> EACCES
* 404 Not Found -> ENOENT
* 405 Method Not Allowed -> ENOSYS
* 409 Conflict -> EEXIST
* 412 Precondition Failed -> ENOTEMPTY
* 413 Request Entity Too Large -> ERANGE
* 415 Unsupported Media Type -> ENODATA/ENOATTR
* 500 Internal Server Error -> EIO (default error)

Authentication/Authorization/Crypto
-----------------------------------

Security should be managed at the webserver level (nginx, apache, uWSGI, whatever you use), but you are free to extend you server-side app to support HTTP-related security techiques (just ensure the client supports them)

Virtualhosting
--------------

Use of virtualhosting is highly suggested, clients MUST pass the Host header in every request.

Sanitizing paths
================

Client and server should work with absolute paths (as FUSE does). You need to sanitize paths in the server as you cannot be sure a FUSE-compliant client will connect.

The steps to validate a paths are easy:

* ensure every resource starts with /
* ensure the resource does NOT contain /../ and /./
* ensure the resource does NOT end with /.. and /.

So

/foo/bar is valid

while

/foo/bar/../bar is not

Methods specifications
======================

The following tables (maps 1:1 with POSIX) will be useful when building headers:

"mode" flags

|POSIX|int|hex|oct|
|-----|---|---|---|
|S_IFREG|32768|0x8000|0100000|
|S_IFCHR|8192|0x2000|020000|
|S_IFBLK|24576|0x6000|060000|
|S_IFIFO|4096|0x1000|010000|
|S_IFSOCK|49152|0xc000|0140000|
|S_IFDIR|16384|0x4000|040000|
|S_IFLNK|40960|0xa000|0120000|
|S_ISUID|2048|0x800|04000|
|S_ISGID|1024|0x400|02000|
|S_ISVTX|512|0x200|01000|
|S_IRUSR|256|0x100|0400|
|S_IWUSR|128|0x80|0200|
|S_IXUSR|64|0x40|0100|
|S_IRGRP|32|0x20|040|
|S_IWGRP|16|0x10|020|
|S_IXGRP|8|0x8|010|
|S_IROTH|4|0x4|04|
|S_IWOTH|2|0x2|02|
|S_IXOTH|1|0x1|01|

"open" flags

|POSIX|int|hex|oct|
|-----|---|---|---|
|O_RDONLY|0|0x0|00|
|O_WRONLY|1|0x1|01|
|O_RDWR|2|0x2|02|

"access" flags

|POSIX|int|hex|oct|
|-----|---|---|---|
|R_OK|4|0x4|04|
|W_OK|2|0x2|02|
|X_OK|1|0x1|01|

READDIR
-------

FUSE hook: readdir()

X-Spock headers used: none

Expected status: 200 OK on success


This method returns the list of objects in a directory as a text stream with every object separated by '\n':

raw HTTP example:

```
READDIR /foobar HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 21

.
..
file001
file002
```

curl example:

```sh
$ curl -X READDIR -D /dev/stdout http://host:port/
HTTP/1.1 200 OK
Content-Length: 21

.
..
file001
file002
```

WSGI example (spockfs_build_path() is an immaginary function that builds a filesystem path from a PATH_INFO var)

```python
import os
def application(environ, start_response):
    path = spockfs_build_path(environ['PATH_INFO'])
    if environ['REQUEST_METHOD'] == 'READDIR':
        output = '\n'.join(['.', '..'] + os.listdir(path))
        start_response('200 OK', [('Content-Length', str(len(output)))])
        return [output]
```

GETATTR
-------

FUSE hook: getattr()

X-Spock headers used: X-Spock-mode, X-Spock-uid, X-Spock-gid, X-Spock-size, X-Spock-mtime, X-Spock-atime, X-Spock-ctime, X-Spock-nlink, X-Spock-blocks, X-Spock-dev, X-Spock-ino

Expected status: 200 OK on success

This method returns stat() attributes of an object. In POSIX it would be an lstat() call.

raw HTTP example

```
GETATTR /foobar HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 0
X-Spock-mode: 17407
X-Spock-uid: 1000
X-Spock-gid: 1000
X-Spock-size: 374
X-Spock-mtime: 1420481543
X-Spock-atime: 1420481542
X-Spock-ctime: 1420481543
X-Spock-nlink: 11
X-Spock-blocks: 1
X-Spock-dev: 16777224
X-Spock-ino: 106280423

```

the values of the headers map 1:1 with the POSIX `struct stat` fields

curl example:

```sh
$ curl -X GETATTR -D /dev/stdout http://host:port/
HTTP/1.1 200 OK
X-Spock-mode: 16877
X-Spock-uid: 1000
X-Spock-gid: 1000
X-Spock-size: 4096
X-Spock-mtime: 1420489499
X-Spock-atime: 1420499434
X-Spock-ctime: 1420489499
X-Spock-nlink: 5
X-Spock-blocks: 8
X-Spock-dev: 2049
X-Spock-ino: 459840
Content-Length: 0
```

WSGI example

```python
import os
def application(environ, start_response):
    path = spockfs_build_path(environ['PATH_INFO'])
    if environ['REQUEST_METHOD'] == 'GETATTR':
        st = os.stat(path)
        headers = []
        headers.append(('Content-Length', '0'))
        headers.append(('X-Spock-mode', str(st.st_mode))
        headers.append(('X-Spock-uid', str(st.st_uid))
        headers.append(('X-Spock-gid', str(st.st_gid))
        headers.append(('X-Spock-size', str(st.st_size))
        headers.append(('X-Spock-mtime', str(st.st_mtime))
        headers.append(('X-Spock-atime', str(st.st_atime))
        headers.append(('X-Spock-ctime', str(st.st_ctime))
        headers.append(('X-Spock-nlink', str(st.st_nlink))
        headers.append(('X-Spock-blocks', str(st.st_blocks))
        headers.append(('X-Spock-dev', str(st.st_dev))
        headers.append(('X-Spock-ino', str(st.st_ino))
        start_response('200 OK', headers)
        return []
```

MKNOD
-----

FUSE hook: mknod()

X-Spock headers used: X-Spock-mode, X-Spock-dev

Expected status: 201 Created on success

This is the mknod() POSIX function, you can use it for creating fifos, devices and so on (well, even regular files ...)


raw HTTP example:

```
MKNOD /foobar/fifo HTTP/1.1
Host: example.com
X-Spock-mode: 4480
X-Spock-dev: 0

HTTP/1.1 201 Created
Content-Length: 0

```

The X-Spock-mode value (octal: 010600) is built as `S_IFIFO|S_IRUSR|S_IRUSR` (so a fifo with 600 permissions). The dev value is left as 0 as it is not used.

OPEN
----

FUSE hook: open

X-Spock headers used: X-Spock-flag

Expected status: 200 OK on success

This is only a "check" for permission on a file as all of the spockfs operations are stateless.


CHMOD
-----

FUSE hook: chmod

X-Spock headers used: X-Spock-mode

Expected status: 200 OK on success


CHOWN
-----

FUSE hook: chown

X-Spock headers used: X-Spock-uid, X-Spock-gid

Expected status: 200 OK on success

Note: this will probably never success if you run your server over a true filesystem as unprivileged user

TRUNCATE
--------

FUSE hook: truncate

X-Spock headers used: X-Spock-size

Expected status: 200 OK on success

truncate/resize a file to size specified by X-Spock-size header

ACCESS
------

FUSE hook: access

X-Spock headers used: X-Spock-mode

Expected status: 200 OK on success

Very similar to open from a low-level point of view. Albeit the header is called mode, it does not take the stat() mode value but R_OK, W_OK, X_OK flags (the 'mode' name is used in respect to POSIX definition)

raw HTTP example

```
ACCESS /am_i_writable HTTP/1.1
Host: example.com
X-Spock-mode: 2

HTTP/1.1 200 OK
Content-Length: 0

```

this will check if /am_i_writable is writable (W_OK = 2)

SYMLINK
-------

FUSE hook: symlink

X-Spock headers used: X-Spock-target

Expected status: 201 Created on success

Create a new symlink pointing to the X-Spock-target header value

```
SYMLINK /i_am_a_link HTTP/1.1
Host: example.com
X-Spock-target: /opt/foobar

HTTP/1.1 201 Created
Content-Length: 0

```

will create the symlink 'i_am_a_link' pointing to '/opt/foobar'

READLINK
--------

FUSE hook: readlink

X-Spock headers used: none

Expected status: 200 OK on success

Returns the target of a symlink as the HTTP body.

RMDIR
-----

FUSE hook: rmdir

X-Spock headers used: none

Expected status: 200 OK on success

Remove a directory (must be empty)

MKDIR
-----

FUSE hook: mdir

X-Spock headers used: X-Spock-mode

Expected status: 201 Created

Create a new directory

LINK
----

FUSE hook: link

X-Spock headers used: X-Spock-target

Expected status: 201 Created on success

Create a new hardlink pointing to the X-Spock-target header value


RENAME
------

FUSE hook: rename

X-Spock headers used: X-Spock-target

Expected status: 200 OK on success

Rename the object specified in X-Spock-target header with the resource name

raw HTTP example

```
RENAME /foobar/deimos HTTP/1.1
Host: example.com
X-Spock-target: /foobar/kratos

HTTP/1.1 200 OK
Content-Length: 0

```

this renames /foobar/kratos to /foobar/deimos


FALLOCATE
---------

(Currently Linux only)

FUSE hook: fallocate

X-Spock headers used: X-Spock-mode

Standard headers used: Range

Expected status: 200 OK on success

This method physically "pre-allocates" blocks for the specified file. It is generally used for performance reasons and it is currently a Linux-only call.


STATFS
------

curl example:

```sh
$ curl -X STATFS -D /dev/stdout http://host:port/
HTTP/1.1 200 OK
X-Spock-bsize: 4096
X-Spock-frsize: 4096
X-Spock-blocks: 5006245
X-Spock-bfree: 306563
X-Spock-bavail: 79344
X-Spock-files: 1286144
X-Spock-ffree: 499225
X-Spock-favail: 499225
X-Spock-fsid: 16569270516359368890
X-Spock-flag: 4096
X-Spock-namemax: 255
Content-Length: 0

```

LISTXATTR
---------

(Currently unsupported on FreeBSD)

FUSE hook: listxattr

X-Spock headers used: X-Spock-size

Expected status: 200 OK on success

Returns the list of extended attributes names for a resource. The format is the same of READDIR (each name separated by a newline).

The output size must fit into X-Spock-size value. If X-Spock-size is zero, the header will be returned in the response too with the size required for the full list.

raw HTTP example

```
LISTXATTR /foobar HTTP/1.1
Host: example.com
X-Spock-size: 4096

HTTP/1.1 200 OK
Content-Length: 18

user.foo
user.bar
```

the request specify a X-Spock-size of 4k so the output (that is 18 bytes) will fit without problems.

```
LISTXATTR /foobar HTTP/1.1
Host: example.com
X-Spock-size: 0

HTTP/1.1 200 OK
Content-Length: 0
X-Spock-size: 18

```

this time the X-Spock-size is 0, so the response has an empty body and a X-Spock-size of 18 (that is the number of bytes required for listxattr() output)

GETXATTR
--------

(Currently unsupported on FreeBSD)

SETXATTR
--------

(Currently unsupported on FreeBSD)

REMOVEXATTR
-----------

(Currently unsupported on FreeBSD)

UTIMENS
-------

FUSE hook: utimens

X-Spock headers used: X-Spock-atime, X-Spock-mtime

Expected status: 200 OK on success

Update access and modification times of an object

raw HTTP example:

```
UTIMENS /foobar/deimos HTTP/1.1
Host: example.com
X-Spock-atime: 1
X-Spock-mtime: 1

HTTP/1.1 200 OK
Content-Length: 0

```

this set atime and mtime of /foobar/deimos to the first second of 1 Jan 1970


POST
----

FUSE hook: create()

X-Spock headers used: X-Spock-mode

Expected status: 201 Created on success

Special behaviour: every file must be created with owner write privilege, otherwise the following use will not work:

```c
; creating a file with only read privileges, but the file descriptor has write support
int fd = open("path", O_CREAT|O_RDWR|O_EXCL, 0444);
// this will fail in spockfs as there are no write privileges on the file
write(fd, ...);
```


PUT
---

FUSE hook: write()

X-Spock headers used: none

Standard headers used: Content-Range

Expected status: 200 OK on success

Suggestions: you can use lseek()+write() or directly pwrite() in server implementations.

Write a chunk of data to the specified file. offset and size to write are specified by the Content-Range, the data to write are the body of the request.

raw HTTP example

```
PUT /enterprise HTTP/1.1
Host: example.com
Content-Range: bytes=100-104
Content-Length: 5

spockHTTP/1.1 200 OK
Content-Length: 0

```

this will write the string 'spock' at bytes 100, 101, 102, 103 and 104 of the enterprise file.


GET
---

FUSE hook: read()

X-Spock headers used: none

Standard headers used: Range

Expected status: 206 Partial Content or 200 OK on success

Read a chunk from a file offset. The Range header specifies offset and end of the part to read.

raw HTTP example

```
GET /enterprise HTTP/1.1
Host: example.com
Range: bytes=100-104

HTTP/1.1 206 Partial Content
Content-Length: 5

spock
```

this returns bytes 100, 101, 102, 103 and 104 previously written by the PUT example

DELETE
------

FUSE hook: unlink()

X-Spock headers used: none

Expected status: 200 OK on success

This is REST compliant, so nothing special here

raw HTTP example

```
DELETE /enterprise HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
```

Todo
----

Try to find a solution for locking.

Define an api for the POLL feature (if we find some useful usage for it)

Define an api for the IOCTL feature.

Investigate gzip compression implications.

Suggestions on how to use Keep-alive


Performance
===========

HTTP is obviously pretty verbose (at the packet level) so comparing SpockFS with NFS or 9P is unfair. WebDAV should be a better candidate for a comparison and SpockFS is obviously way simpler and faster. In addition to this, modern HTTP parsing techiques are super-optimized (thanks to projects like nginx).

FUSE is often considered suboptimal from a performance point of view, but allowed fast development of a working implementations with lot of interesting features (at this stage, reliability is way more important than high-performance).

The reference/official client and server are heavily optimized (and more work can be done in this area) so you should have a pretty comfortable experience when working with your shell over a SpockFS mount.


The reference FUSE client
=========================

To build the 'official' FUSE client (based on libcurl) just clone the repository and run 'make'.

Finally run the resulting `spockfs` binary:

```sh
./spockfs <url> <mountpoint>
```

as an example to mount an url under /mnt/foobar

```sh
./spockfs https://foo:bar@example.com/mydisk /mnt/foobar
```

The client supports interruptions (you can interrupt stuck filesystem requests in the middle), dns caching (every result is cached for 60 seconds) and is fully thread-safe. Every operation has a 30 seconds timeout, after which EIO is returned. High-availability is easy affordable as every operation is stateless, and in case of a malfunctioning server the filesystem will return back to fully operational mode as soon as the server is back (in the mean time EIO is returned). The default 60 seconds TTL for dns cache allows easy 'failover' of nodes.


The reference server implementation (uWSGI plugin)
==================================================

The server-side official implementation is a uWSGI plugin (if you do not know what uWSGI is, simply consider it as the application server to run the spockfs server logic). In a LAN you can run uWSGI as standalone (with --http-socket option) while if you plan the expose the server on a public network you'd better to put uWSGI behind nginx, apache or the uWSGI http router.

Albeit the plugin supports non-blocking/coroutine modes, they do not apply well to a storage server (disk i/o on Linux cannot be made 100% non-blocking) so a multithreaded/multiprocess (or both) approach is the best way to configure uWSGI.

If you already use uWSGI you can build the plugin in one shot with:

```sh
uwsgi --build-plugin https://github.com/unbit/spockfs
```

this will result with the spockfs_plugin.so in the current directory.

Instead, if you do not use/have uWSGI you can build a single binary with the spockfs plugin embedded in it, with a single command again (just be sure to have gcc, python and libpcre to build it):

```sh
curl https://uwsgi.it/install.sh | UWSGI_EMBED_PLUGINS="spockfs=https://github.com/unbit/spockfs" bash -s nolang /tmp/spockfs-server
```

this will result in the /tmp/spockfs-server binary (that is a uWSGI server + spockfs plugin)

Now you can run the server. If you choosen the plugin approach (the first described):

```sh
uwsgi --plugin 0:spockfs --http-socket :9090 --threads 8 --spockfs-mount /=/var/www
```

this will bind uWSGI to http port 9090 with 8 threads ready to answer for spockfs requests that will be mapped to /var/www (read: a /foo request will map to /var/www/foo)

If you have built the /tmp/spockfs-server binary the syntax will be a little shorter:

```sh
/tmp/spockfs-server --http-socket :9090 --threads 8 --spockfs-mount /=/var/www
```

This is enough to run a LAN server, for more informations and examples check the spockfs uWSGI plugin documentation here: https://github.com/unbit/spockfs/tree/master/uwsgi/README.md


Project Status
==============

Version 0.1 of the SPECS is complete, the 0.2 version should fix the items listed in the SPECS Todo section.

Both the reference/official server and client fully support the 0.1 SPECS and run on Linux, FreeBSD ans OSX

Why ?
=====

Unbit is already developing an high performance 9p network filesystem server (https://github.com/unbit/9spock), but during development we came up with this really simple (and working) implementation. You can build a server really fast and over battle-tested technologies (nginx, apache, uWSGI ...) and web frameworks.

Finally, after having worked/developed the https://github.com/unbit/davvy project (a WebDAV/CalDAV/CardDAV django implementation) i came up with the conclusion that i will never touch again any WebDAV-related thing. Seriously.

Why 'Spock' ?
=============

Why not ? Instead of using cryptozoological animals or musicians names (or acronyms, like my company generally does) this time i want to make a tribute to the best figure of The United Federation of Planets 
