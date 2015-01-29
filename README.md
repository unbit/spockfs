spockfs
=======

SpockFS is an HTTP based network filesystem

It is built upon plain HTTP methods and headers (no XML, no XML and no XML) and supports all of the FUSE posix-related hooks (yes you can manage symlinks too).

This page is mainly for Specs, if you only want to download a server and a client (for Linux, FreeBSD and OSX) jump here: https://github.com/unbit/spockfs/blob/master/README.md#the-reference-fuse-client

SPECS 0.2
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

A group of headers are required to manage filesystem-related informations or special operations. All headers are prefixed with `Spock-`.

The following ones are for stat()-related operations:

* Spock-mode (for mode_t values and similar)
* Spock-uid (for uid)
* Spock-gid (for gid)
* Spock-size (for specifying sizes)
* Spock-mtime (the modification time in unix time)
* Spock-atime (the access time in unix time)
* Spock-ctime (the creation time in unix time)
* Spock-nlink (the number of links of an object)
* Spock-blocks (the number of blocks os an object)
* Spock-dev (the device id)
* Spock-ino (the inode number, unused by default in FUSE)
* Spock-flag (generic flag, used by open() too)
* Spock-target (generic string used for symlink values, rename operations and for the names of extended attributes)

The following ones are for statvfs() calls, they map 1:1 with the stavfs struct, and you will use them only if you want to implement the STATFS method in your server/client:

* Spock-bsize
* Spock-frsize
* Spock-bfree
* Spock-bavail
* Spock-files
* Spock-ffree
* Spock-favail
* Spock-fsid
* Spock-namemax


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

"xattr" flags

|POSIX|int|hex|oct|
|-----|---|---|---|
|XATTR_CREATE|1|0x1|01|
|XATTR_REPLACE|2|0x2|02|


READDIR
-------

FUSE hook: readdir()

Spock headers used: none

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

Spock headers used: Spock-mode, Spock-uid, Spock-gid, Spock-size, Spock-mtime, Spock-atime, Spock-ctime, Spock-nlink, Spock-blocks, Spock-dev, Spock-ino

Expected status: 200 OK on success

This method returns stat() attributes of an object. In POSIX it would be an lstat() call.

raw HTTP example

```
GETATTR /foobar HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 0
Spock-mode: 17407
Spock-uid: 1000
Spock-gid: 1000
Spock-size: 374
Spock-mtime: 1420481543
Spock-atime: 1420481542
Spock-ctime: 1420481543
Spock-nlink: 11
Spock-blocks: 1
Spock-dev: 16777224
Spock-ino: 106280423

```

the values of the headers map 1:1 with the POSIX `struct stat` fields

curl example:

```sh
$ curl -X GETATTR -D /dev/stdout http://host:port/
HTTP/1.1 200 OK
Spock-mode: 16877
Spock-uid: 1000
Spock-gid: 1000
Spock-size: 4096
Spock-mtime: 1420489499
Spock-atime: 1420499434
Spock-ctime: 1420489499
Spock-nlink: 5
Spock-blocks: 8
Spock-dev: 2049
Spock-ino: 459840
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
        headers.append(('Spock-mode', str(st.st_mode))
        headers.append(('Spock-uid', str(st.st_uid))
        headers.append(('Spock-gid', str(st.st_gid))
        headers.append(('Spock-size', str(st.st_size))
        headers.append(('Spock-mtime', str(st.st_mtime))
        headers.append(('Spock-atime', str(st.st_atime))
        headers.append(('Spock-ctime', str(st.st_ctime))
        headers.append(('Spock-nlink', str(st.st_nlink))
        headers.append(('Spock-blocks', str(st.st_blocks))
        headers.append(('Spock-dev', str(st.st_dev))
        headers.append(('Spock-ino', str(st.st_ino))
        start_response('200 OK', headers)
        return []
```

MKNOD
-----

FUSE hook: mknod()

Spock headers used: Spock-mode, Spock-dev

Expected status: 201 Created on success

This is the mknod() POSIX function, you can use it for creating fifos, devices and so on (well, even regular files ...)


raw HTTP example:

```
MKNOD /foobar/fifo HTTP/1.1
Host: example.com
Spock-mode: 4480
Spock-dev: 0

HTTP/1.1 201 Created
Content-Length: 0

```

The Spock-mode value (octal: 010600) is built as `S_IFIFO|S_IRUSR|S_IWUSR` (so a fifo with 600 permissions). The dev value is left as 0 as it is not used.

curl example:

```sh
$ curl -D /dev/stdout -H "Spock-mode: 4480" -H "Spock-dev: 0" -X MKNOD http://host:port/foobar/fifo
HTTP/1.1 201 Created
Content-Length: 0
```

OPEN
----

FUSE hook: open

Spock headers used: Spock-flag

Expected status: 200 OK on success

This is only a "check" for permissions on a file as all of the spockfs operations are stateless.

raw HTTP example

```
OPEN /a_file HTTP/1.1
Host: example.com
Spock-flag: 1

HTTP/1.1 200 OK
Content-Length: 0

```

'1' is the POSIX flag for O_WRONLY so the previous requests checks for writability of '/a_file' resource


CHMOD
-----

FUSE hook: chmod

Spock headers used: Spock-mode

Expected status: 200 OK on success

Change permissions on the specified resource. Spock-mode is the stat() mode field.

raw HTTP example

```
CHMOD /writable_for_all HTTP/1.1
Host: example.com
Spock-mode: 438

HTTP/1.1 200 OK
Content-Length: 0

```

438 is given by (256 | 128 | 32 | 16 | 4 | 2) octal 0666 or `S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH`

CHOWN
-----

FUSE hook: chown

Spock headers used: Spock-uid, Spock-gid

Expected status: 200 OK on success

Note: this will probably never success if you run your server over a true filesystem as unprivileged user

raw HTTP example:

```
CHOWN /foobar HTTP/1.1
Host: example.com
Spock-uid: 1000
Spock-gid: 1000

HTTP/1.1 200 OK
Content-Length: 0

```

Note: always use numeric uid/gid

TRUNCATE
--------

FUSE hook: truncate

Spock headers used: Spock-size

Expected status: 200 OK on success

truncate/resize a file to size specified by Spock-size header

raw HTTP example

```
TRUNCATE /resizeme HTTP/1.1
Host: example.com
Spock-size: 100

HTTP/1.1 200 OK
Content-Length: 0

```

will resize the resource /resizeme to 100 bytes

curl example:

```sh
$ curl -D /dev/stdout -H "Spock-size: 100" -X TRUNCATE http://host:port/resizeme
HTTP/1.1 200 OK
Content-Length: 0

```

ACCESS
------

FUSE hook: access

Spock headers used: Spock-mode

Expected status: 200 OK on success

Very similar to open from a low-level point of view. Albeit the header is called mode, it does not take the stat() mode value but R_OK, W_OK, X_OK flags (the 'mode' name is used in respect to POSIX definition)

raw HTTP example

```
ACCESS /am_i_writable HTTP/1.1
Host: example.com
Spock-mode: 2

HTTP/1.1 200 OK
Content-Length: 0

```

this will check if /am_i_writable is writable (W_OK = 2)

SYMLINK
-------

FUSE hook: symlink

Spock headers used: Spock-target

Expected status: 201 Created on success

Create a new symlink pointing to the Spock-target header value

```
SYMLINK /i_am_a_link HTTP/1.1
Host: example.com
Spock-target: /opt/foobar

HTTP/1.1 201 Created
Content-Length: 0

```

will create the symlink 'i_am_a_link' pointing to '/opt/foobar'

Note: the target object can be non-existent, this is not a problem

READLINK
--------

FUSE hook: readlink

Spock headers used: none

Expected status: 200 OK on success

Returns the target of a symlink as the HTTP body.

raw HTTP example:

```
READLINK /a_link HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 4

/opt
```

RMDIR
-----

FUSE hook: rmdir

Spock headers used: none

Expected status: 200 OK on success

Remove a directory (must be empty)

raw HTTP example

```
RMDIR /foobar HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 0

```

MKDIR
-----

FUSE hook: mkdir

Spock headers used: Spock-mode

Expected status: 201 Created

Create a new directory, Spock-mode is the stat() mode (like CHMOD)

raw HTTP example

```
MKDIR /foobar/test HTTP/1.1
Host: example.com
Spock-mode: 448

HTTP/1.1 201 Created
Content-Length: 0

```

Spock-mode is (256 | 128 | 64) octal (0700) or `S_IRUSR|S_IWUSR|S_IXUSR`


LINK
----

FUSE hook: link

Spock headers used: Spock-target

Expected status: 201 Created on success

Create a new hardlink pointing to the Spock-target header value


```
LINK /i_am_a_hardlink HTTP/1.1
Host: example.com
Spock-target: /opt/foobar

HTTP/1.1 201 Created
Content-Length: 0

```

Note: the target object must exists


RENAME
------

FUSE hook: rename

Spock headers used: Spock-target

Expected status: 200 OK on success

Rename the object specified in Spock-target header with the resource name

raw HTTP example

```
RENAME /foobar/deimos HTTP/1.1
Host: example.com
Spock-target: /foobar/kratos

HTTP/1.1 200 OK
Content-Length: 0

```

this renames /foobar/kratos to /foobar/deimos


FALLOCATE
---------

(Currently Linux only)

FUSE hook: fallocate

Spock headers used: Spock-mode

Standard headers used: Range

Expected status: 200 OK on success

This method physically "pre-allocates" blocks for the specified file. It is generally used for performance reasons and it is currently a Linux-only call. Spock-mode should be set to 0 as portability issues must be investigated.

raw HTTP example

```
FALLOCATE /bigfile HTTP/1.1
Host: example.com
Range: bytes=400-500
Spock-mode: 0

HTTP/1.1 200 OK
Content-Length: 0

```

will allocate disk space from byte 400 to 500 of the /bifgile resource

STATFS
------

FUSE hook: statfs

Spock headers used: none

Expected status: 200 OK on success

curl example:

```sh
$ curl -X STATFS -D /dev/stdout http://host:port/
HTTP/1.1 200 OK
Spock-bsize: 4096
Spock-frsize: 4096
Spock-blocks: 5006245
Spock-bfree: 306563
Spock-bavail: 79344
Spock-files: 1286144
Spock-ffree: 499225
Spock-favail: 499225
Spock-fsid: 16569270516359368890
Spock-flag: 4096
Spock-namemax: 255
Content-Length: 0

```

LISTXATTR
---------

(Currently unsupported on FreeBSD)

FUSE hook: listxattr

Spock headers used: Spock-size

Expected status: 200 OK on success

Returns the list of extended attributes names for a resource. The format is the same of READDIR (each name separated by a newline).

The output size must fit into Spock-size value. If Spock-size is zero, the header will be returned in the response too with the size required for the full list.

raw HTTP example

```
LISTXATTR /foobar HTTP/1.1
Host: example.com
Spock-size: 4096

HTTP/1.1 200 OK
Content-Length: 18

user.foo
user.bar
```

the request specify a Spock-size of 4k so the output (that is 18 bytes) will fit without problems.

```
LISTXATTR /foobar HTTP/1.1
Host: example.com
Spock-size: 0

HTTP/1.1 200 OK
Content-Length: 0
Spock-size: 18

```

this time the Spock-size is 0, so the response has an empty body and a Spock-size of 18 (that is the number of bytes required for listxattr() output)


Note: on Linux, user-governed extended attributes must be prefixed with `user.`

GETXATTR
--------

(Currently unsupported on FreeBSD)

FUSE hook: getxattr

Spock headers used: Spock-size, Spock-target

Expected status: 200 OK on success

Get the value of the extended attribute named like the Spock-target value and returns it as the response body. Like LISTXATTR Spock-size set the maximum allowed size of the response, passing it as 0 will return the required size.

raw HTTP example

```
GETXATTR /foobar HTTP/1.1
Host: example.com
Spock-size: 5
Spock-targer: user.foo

HTTP/1.1 200 OK
Content-Length: 5

hello
```

Note: on Linux, user-governed extended attributes must be prefixed with `user.`

SETXATTR
--------

(Currently unsupported on FreeBSD)

FUSE hook: setxattr

Spock headers used: Spock-flag, Spock-target

Expected status: 200 OK on success

Note: you may expect a 201 response, but technically 201 is used when a 'resource' is created (in plain HTTP, WebDAV and its properties management is another beast). In this case we 'only' created an attribute.

Set/create/change the extended atributes named as the Spock-target with the specified value (the value is the body of the request). 

Spock-flag can be 0 (create or modify the object), 1 (XATTR_CREATE, fails if the attribute already exists) or 2 (XATTR_REPLACE, fails if the attribute does not exist)

raw HTTP example

```
SETXATTR /foobar HTTP/1.1
Host: example.com
Spock-target: user.foo
Spock-flag: 0
Content-Length: 5

helloHTTP/1.1 200 OK
Content-Length: 0

```

will create/modify the user.foo attributes of te the /foobar resource with the value 'hello'


Note: on Linux, user-governed extended attributes must be prefixed with `user.`

REMOVEXATTR
-----------

(Currently unsupported on FreeBSD)

FUSE hook: removexattr

Spock headers used: Spock-target

Expected status: 200 OK on success

Remove the extended attribute named as the Spock-target value from the resource

raw HTTP example

```
REMOVEXATTR /foobar HTTP/1.1
Host: example.com
Spock-target: user.foo

HTTP/1.1 200 OK
Content-Length: 0

```

Note: on Linux, user-governed extended attributes must be prefixed with `user.`

UTIMENS
-------

FUSE hook: utimens

Spock headers used: Spock-atime, Spock-mtime

Expected status: 200 OK on success

Update access and modification times of an object

raw HTTP example:

```
UTIMENS /foobar/deimos HTTP/1.1
Host: example.com
Spock-atime: 1
Spock-mtime: 1

HTTP/1.1 200 OK
Content-Length: 0

```

this set atime and mtime of /foobar/deimos to the first second of 1 Jan 1970


POST
----

FUSE hook: create

Spock headers used: Spock-mode

Expected status: 201 Created on success

Special behaviour: every file must be created with owner write privilege, otherwise the following use will not work:

```c
; creating a file with only read privileges, but the file descriptor has write support
int fd = open("path", O_CREAT|O_RDWR|O_EXCL, 0444);
// this would fail in spockfs if there are no write privileges on the file
write(fd, ...);
```

raw HTTP example

```
POST /new_file HTTP/1.1
Host: example.com
Spock-mode: 448

HTTP/1.1 201 Created
Content-Length: 0

```

Spock-mode is stat() mode (the same as the one used by MKDIR)

PUT
---

FUSE hook: write

Spock headers used: none

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

FUSE hook: read

Spock headers used: none

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

FUSE hook: unlink

Spock headers used: none

Expected status: 200 OK on success

This is REST compliant, so nothing special here

raw HTTP example

```
DELETE /enterprise HTTP/1.1
Host: example.com

HTTP/1.1 200 OK
Content-Length: 0
```

curl example:

```sh
$ curl -D /dev/stdout -X DELETE http://host:port/enterprise
```

Credits/Contributors
--------------------

* Roberto De Ioris
* Adriano Di Luzio
* Nate Abele

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

The server-side official implementation is a uWSGI plugin (if you do not know what uWSGI is, simply consider it as the application server to run the spockfs server logic). In a LAN you can run uWSGI as standalone (with --http-socket option) while if you plan to expose the server on a public network you'd better to put uWSGI behind nginx, apache or the uWSGI http router.

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

Now you can run the server. If you choose the plugin approach (the first described):

```sh
uwsgi --plugin 0:spockfs --http-socket :9090 --threads 8 --spockfs-mount /=/var/www
```

this will bind uWSGI to http port 9090 with 8 threads ready to answer for spockfs requests that will be mapped to /var/www (read: a /foo request will map to /var/www/foo)

if you get an error about not finding the spockfs plugin pass its absolute path in this way:

```sh
uwsgi --plugin 0:/path/of/spockfs_plugin.so --http-socket :9090 --threads 8 --spockfs-mount /=/var/www
```

If you have built the /tmp/spockfs-server binary the syntax will be a little shorter:

```sh
/tmp/spockfs-server --http-socket :9090 --threads 8 --spockfs-mount /=/var/www
```

This is enough to run a LAN server, for more informations and examples check the spockfs uWSGI plugin documentation here: https://github.com/unbit/spockfs/tree/master/uwsgi/README.md

Testing
=======

The `spockfs_tests.py` script is included in the sources. It expects a filesystem mounted under /tmp/.spockfs_testdir (ensure the dir mounted under it is empty, as the test will CLEAR it before running !!!). Once the filesystem is mounted just run it:

```sh
python spockfs_tests.py
```

the script requires the python xattr module (does not work On FreeBSD)

Project Status
==============

Version 0.1 of the SPECS is complete, the 0.2 version should fix the items listed in the SPECS Todo section.

Both the reference/official server and client fully support the 0.1 SPECS and run on Linux, FreeBSD ans OSX

Why ?
=====

Unbit is already developing an high performance 9p network filesystem server (https://github.com/unbit/9spock), but during development we came up with this really simple (and working) implementation. You can build a server really fast and over battle-tested technologies (nginx, apache, uWSGI ...) and web stacks (PSGI, WSGI, Rack ...).


Why 'Spock' ?
=============

Why not ? Instead of using cryptozoological animals or musicians names (or acronyms, like my company generally does) this time we want to make a tribute to the best figure of The United Federation of Planets 
