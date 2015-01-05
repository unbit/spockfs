spockfs
=======

SpockFS is an HTTP based network filesystem

It is built upon plain HTTP methods and headers (no XML, no XML and no XML) and supports all of the FUSE posix-related hooks (yes you can manage symlinks too). 

Specs 0.1
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


The reference FUSE client
=========================

The reference server implementation (uWSGI plugin)
==================================================

Why ?
=====

Unbit is already developing an high performance 9p network filesystem server (https://github.com/unbit/9spock), but during development we came up with this really simple (and working) implementation. You can build a server really fast and over battle-tested technologies (nginx, apache, uWSGI ...) and web frameworks.

Finally, after having worked/developed the https://github.com/unbit/davvy project (a WebDAV/CalDAV/CardDAV django implementation) i came up with the conclusion that i will never touch again any WebDAV-related thing. Seriously.
