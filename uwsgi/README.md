The SpockFS uWSGI plugin
========================

This is a plugin (with official modifier 179, try to guess why ;) ) for the uWSGI server allowing it to expose directories via the SpockFS protocol.

A single instance of uWSGI can manage multiple namespaces (named "mountpoints"), so HTTP requests for /foo (and below) could map to /var/www while requests for /bar (and below) could map to /var/site/test and so on.

To build the plugin you can simply run:

```sh
uwsgi --build-plugin https://github.com/unbit/spockfs
```

or if you want a single binary with the plugin embedded (ensure to have a c compiler, python and libpcre):

```sh
curl https://uwsgi.it/install.sh | UWSGI_EMBED_PLUGINS="spockfs=https://github.com/unbit/spockfs" bash -s nolang /tmp/spockfs-server
```

(change /tmp/spockfs-server to whatever you want, this will be the name of the server)

Plugin options
==============

The following options are exposed by the plugin

* --spockfs-mount <mountpoint>=<path> (mount <path> under <mountpoint>)
* --spockfs-ro-mount <mountpoint>=<path> (mount <path> under <mountpoint> in readonly mode)
* --spockfs-xattr-limit <size> (set the maximum size of xattr values, default 64k)


Serving directories
===================

Concurrency
===========

Placing behind nginx
====================

Playing with internal routing
=============================
