spockfs
=======

SpockFS is an HTTP based network filesystem

It is built upon plain HTTP methods and headers (no XML, no XML and no XML) and supports all of the FUSE posix-related hooks (yes you can manage symlinks too).

This page is mainly for Specs, if you only want to download a server and a client skip here: https://github.com/unbit/spockfs/blob/master/README.md#the-reference-fuse-client

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
* X-Spock-nlink (the number of links)
* X-Spock-blocks (the number of blocks)
* X-Spock-dev (the device id)
* X-Spock-ino (the inode number)
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


Finally this two "standard" headers are used:

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

The X-Spock-mode value (octal: 010600) is built as `stat.S_IFIFO | stat.S_IWUSR|stat.S_IRUSR` (so a fifo with 600 permissions). The dev value is left as 0 as it is not used.

OPEN
----

FUSE hook: open

X-Spock headers used: X-Spock-flag

Expected status: 200 OK on success


CHMOD
-----

CHOWN
-----

TRUNCATE
--------

ACCESS
------

SYMLINK
-------

READLINK
--------

RMDIR
-----

MKDIR
-----

LINK
----

RENAME
------

FALLOCATE
---------

STATFS
------

LISTXATTR
---------

GETXATTR
--------

SETXATTR
--------

REMOVEXATTR
-----------

UTIMENS
-------

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


GET
---

FUSE hook: read()

X-Spock headers used: none

Standard headers used: Range

Expected status: 200 OK on success

DELETE
------

FUSE hook: unlink()

X-Spock headers used: none

Expected status: 200 OK on success





The reference FUSE client
=========================

The reference server implementation (uWSGI plugin)
==================================================

Why ?
=====

Unbit is already developing an high performance 9p network filesystem server (https://github.com/unbit/9spock), but during development we came up with this really simple (and working) implementation. You can build a server really fast and over battle-tested technologies (nginx, apache, uWSGI ...) and web frameworks.

Finally, after having worked/developed the https://github.com/unbit/davvy project (a WebDAV/CalDAV/CardDAV django implementation) i came up with the conclusion that i will never touch again any WebDAV-related thing. Seriously.
