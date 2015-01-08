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

We use the .ini configuration format (you are free to use whatever uWSGI supports), save it as spockfs.ini

```ini
[uwsgi]
; load the spockfs plugin in slot 0, useless if you have built it into the binary
plugin = 0:spockfs

; bind to tcp port 9090
http-socket = :9090

; spawn 8 threads
threads = 8

; mount /var/www as /
spockfs-mount = /=var/www

; mount /var/spool as /spool
spockfs-mount = /spool=/var/spool

; mount /opt as /opt in readonly mode

spockfs-ro-mount = /opt=/opt
```

run the server:

```sh
uwsgi spockfs.ini
```

or if you have used the installer:

```sh
/tmp/spockfs-server spockfs.ini
```

Ensure to not run the server as root, eventually you can drop privileges with the uid and gid options:

```ini
[uwsgi]
; load the spockfs plugin in slot 0, useless if you have built it into the binary
plugin = 0:spockfs

; bind to tcp port 9090
http-socket = :9090

; spawn 8 threads
threads = 8

; mount /var/www as /
spockfs-mount = /=var/www

; mount /var/spool as /spool
spockfs-mount = /spool=/var/spool

; mount /opt as /opt in readonly mode

spockfs-ro-mount = /opt=/opt

;drop privileges
uid = www-data
gid = www-daya
```

Concurrency
===========

Multiprocess and multithreaded modes are highly suggested (instead of async).

In this example we spawn 2 processes with 2 threads each, all governed by a master (consider it as an embedded monitor that will automatically respawn dead processes and will monitor the server status)

```ini
[uwsgi]
; load the spockfs plugin in slot 0, useless if you have built it into the binary
plugin = 0:spockfs

; bind to tcp port 9090
http-socket = :9090

; run the master process
master = true
; spawn 2 processes
processes = 2
; spawn 2 threads for each process
threads = 8

; mount /var/www as /
spockfs-mount = /=var/www

; mount /var/spool as /spool
spockfs-mount = /spool=/var/spool

; mount /opt as /opt in readonly mode

spockfs-ro-mount = /opt=/opt

;drop privileges
uid = www-data
gid = www-data
```

uWSGI gives you tons of metrics and monitoring tools. Consider enabling the stats server (to use tools like uwsgitop)


```ini
[uwsgi]
; load the spockfs plugin in slot 0, useless if you have built it into the binary
plugin = 0:spockfs

; bind to tcp port 9090
http-socket = :9090

; run the master process
master = true
; spawn 2 processes
processes = 2
; spawn 2 threads for each process
threads = 8

; mount /var/www as /
spockfs-mount = /=var/www

; mount /var/spool as /spool
spockfs-mount = /spool=/var/spool

; mount /opt as /opt in readonly mode

spockfs-ro-mount = /opt=/opt

;drop privileges
uid = www-data
gid = www-data

; bind the stats server on 127.0.0.1:9091
stats = 127.0.0.1:9091
```

telnet/nc to 127.0.0.1:9091 will give you a lot of infos (in json format). The https://github.com/unbit/uwsgitop tool will give you a top-like interface for the stats.

Placing behind nginx
====================

If you plan to use the SpockFS server in a LAN, using --http-socket uWSGI option will be more than enough (and you will get really good performance). Instead, when wanting to expose it over internet you should proxy it behind a full-featured webserver. uWSGI has its HTTP proxy embedded (the so called 'http-router'), but (as its name implies) it is only a 'router' without any kind of logic (expect for mapping domain names to specific backends and for doing load balancing).

Nginx instead has tons of advanced features that makes it a perfect candidate for the job. In addition to this it supports out of the box the 'uwsgi protocol' (that is a faster way to pass informations between the proxy and the backend).

Playing with internal routing
=============================
