#include <uwsgi.h>

#include <sys/statvfs.h>

extern struct uwsgi_server uwsgi;

struct uwsgi_plugin spockfs_plugin;

static struct spockfs {
	struct uwsgi_string_list *mountpoints;
	struct uwsgi_string_list *ro_mountpoints;
	uint64_t xattr_limit;
} spockfs;

static struct uwsgi_option spockfs_options[] = {
	{"spockfs-mount", required_argument, 0, "serves a directory via spockfs under the specified mountpoint, syntax: <mountpoint>=<path>", uwsgi_opt_add_string_list, &spockfs.mountpoints, 0},
	{"spockfs-ro-mount", required_argument, 0, "serves a directory via spockfs under the specified mountpoint in readonly, syntax: <mountpoint>=<path>", uwsgi_opt_add_string_list, &spockfs.ro_mountpoints, 0},
	{"spockfs-readonly-mount", required_argument, 0, "serves a directory via spockfs under the specified mountpoint in readonly, syntax: <mountpoint>=<path>", uwsgi_opt_add_string_list, &spockfs.ro_mountpoints, 0},
	{"spockfs-xattr-limit", required_argument, 0, "set the max size for spockfs xattr operations (default 64k)", uwsgi_opt_set_64bit, &spockfs.xattr_limit, 0},
	UWSGI_END_OF_OPTIONS
};

static int spockfs_build_path(char *path, struct wsgi_request *wsgi_req, char *item, uint16_t item_len) {
	char *base = (char *) uwsgi_apps[wsgi_req->app_id].interpreter;
	size_t base_len = (size_t) uwsgi_apps[wsgi_req->app_id].callable;

	// first check for size
	if (base_len + item_len > PATH_MAX) return -1;
	// then check for invalid combinations (initial slash is already checked) /../ /.. /./
	if (uwsgi_contains_n(item, item_len, "/../", 4)) return -1;
	if (uwsgi_contains_n(item, item_len, "/./", 3)) return -1;
	// ends with a dot ? (check for final /. and /..
	if (item[item_len-1] == '.') {
		if (item_len > 1) {
			// ./
			if (item[item_len-2] == '/') return -1;
			// ..
			if (item[item_len-2] == '.') {
				if (item_len > 2) {
					// /..
					if (item[item_len-3] == '/') return -1;
				}
			}
		}
	}

	memcpy(path, base, base_len);
	memcpy(path + base_len, item, item_len);
	// final 0
	path[base_len+item_len] = 0;
	return 0;
}

static void spockfs_errno(struct wsgi_request *wsgi_req) {
	switch(errno) {
        	case ENOENT:
                	uwsgi_404(wsgi_req);
                        break;
                case EACCES:
                case EPERM:
                	uwsgi_403(wsgi_req);
                        break;
		case ENOSYS:
		case EOPNOTSUPP:
			uwsgi_405(wsgi_req);
                        break;
                case EEXIST:
                	uwsgi_response_prepare_headers(wsgi_req, "409 Conflict", 12);
			break;
                case ENOTEMPTY:
                	uwsgi_response_prepare_headers(wsgi_req, "412 Precondition Failed", 23);
			break;
                case ERANGE:
                	uwsgi_response_prepare_headers(wsgi_req, "413 Request Entity Too Large", 28);
			break;
                case ENODATA:
                	uwsgi_response_prepare_headers(wsgi_req, "415 Unsupported Media Type", 26);
			break;
                default:
                	uwsgi_500(wsgi_req);
	}
}

static int spockfs_response_add_header_num(struct wsgi_request *wsgi_req, char *key, uint16_t kl, uint64_t n) {
        char buf[sizeof(UMAX64_STR)+1];
        int ret = snprintf(buf, sizeof(UMAX64_STR)+1, "%llu", (unsigned long long) n);
        if (ret <= 0 || ret >= (int) (sizeof(UMAX64_STR)+1)) {
                wsgi_req->write_errors++;
                return -1;
        }
        return uwsgi_response_add_header(wsgi_req, key, kl, buf, ret);
}


/*
	here we could have used the uwsgi_file_serve api function, but it sets a gazillion
	of response headers useless for spockfs
*/
static int spockfs_get(struct wsgi_request *wsgi_req, char *path) {

	int fd = open(path, O_RDONLY);
        if (fd < 0) {
		spockfs_errno(wsgi_req);
		goto end;
        }

	struct stat st;
	if (fstat(fd, &st)) {
		spockfs_errno(wsgi_req);
		goto end;
	}

	if (!S_ISREG(st.st_mode)) {
		errno = EACCES;
		spockfs_errno(wsgi_req);
                goto end;
	}

	size_t fsize = st.st_size;
	// security check
	if (wsgi_req->range_from > fsize) {
		wsgi_req->range_from = 0;
		wsgi_req->range_to = 0;
	}
	if (wsgi_req->range_to) {
		fsize = (size_t) ((wsgi_req->range_to-wsgi_req->range_from)+1);
		if (fsize + wsgi_req->range_from > (size_t) (st.st_size)) {
			fsize = st.st_size - wsgi_req->range_from;
		}
		if (uwsgi_response_prepare_headers(wsgi_req, "206 Partial Content", 19)) goto end;
		if (uwsgi_response_add_content_range(wsgi_req, wsgi_req->range_from, wsgi_req->range_to, st.st_size)) goto end;
	}
	else {
		if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
	}
	if (uwsgi_response_add_content_length(wsgi_req, fsize)) goto end;
	// fd will be automatically closed
	uwsgi_response_sendfile_do(wsgi_req, fd, wsgi_req->range_from, fsize);
end:
        return UWSGI_OK;
}

static int spockfs_access(struct wsgi_request *wsgi_req, char *path) {

        uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
        if (!mode) goto end;

        if (access(path, uwsgi_str_num(mode, mode_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
        uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}


static int spockfs_fallocate(struct wsgi_request *wsgi_req, char *path) {

	uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
        if (!mode) goto end2;

        int fd = open(path, O_WRONLY);
        if (fd < 0) {
                spockfs_errno(wsgi_req);
                goto end2;
        }

	if (fallocate(fd, uwsgi_str_num(mode, mode_len), wsgi_req->range_from, (wsgi_req->range_to-wsgi_req->range_from)+1)) {
                spockfs_errno(wsgi_req);
                goto end;
        }

	uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);
end:
	close(fd);
end2:
        return UWSGI_OK;
}

/*
	unfortunately uWSGI does not expose an api for content-range.
	The lucky thing is that we only need the first part of the range string
*/
static int spockfs_put(struct wsgi_request *wsgi_req, char *path) {
        int fd = open(path, O_WRONLY);
        if (fd < 0) {
		spockfs_errno(wsgi_req);
                goto end2;
        }

	uint16_t content_range_len = 0;
	char *content_range = uwsgi_get_var(wsgi_req, "HTTP_CONTENT_RANGE", 18, &content_range_len);
	if (!content_range) goto end;

	if (uwsgi_starts_with(content_range, content_range_len, "bytes=", 6)) {
		goto end;
	}

	char *minus = memchr(content_range+6, '-', content_range_len-6);
	if (!minus) goto end;

	if (lseek(fd, uwsgi_str_num(content_range+6, minus-(content_range+6)), SEEK_SET) < 0) {
		spockfs_errno(wsgi_req);
                goto end;
	}

	size_t remains = wsgi_req->post_cl;
        while(remains > 0) {
                ssize_t body_len = 0;
                char *body =  uwsgi_request_body_read(wsgi_req, UMIN(remains, 32768) , &body_len);
                if (!body || body == uwsgi.empty) break;
                if (write(fd, body, body_len) != body_len) goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);
end:
	close(fd);
end2:
        return UWSGI_OK;
}

static int spockfs_mkdir(struct wsgi_request *wsgi_req, char *path) {

        uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
	if (!mode) goto end;

        if (mkdir(path, uwsgi_str_num(mode, mode_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "201 Created", 11);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_post(struct wsgi_request *wsgi_req, char *path) {

        uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
        if (!mode) goto end;

	// ensure owner has write permissions
        int fd = creat(path, uwsgi_str_num(mode, mode_len) | S_IWUSR);
	if (fd < 0) {
                spockfs_errno(wsgi_req);
                goto end;
        }
	close(fd);

        uwsgi_response_prepare_headers(wsgi_req, "201 Created", 11);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_listxattr(struct wsgi_request *wsgi_req, char *path) {
	char *buf = NULL;

        uint16_t size_len = 0;
        char *size = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_SIZE", 17, &size_len);
        if (!size) goto end;

	size_t mem = uwsgi_str_num(size, size_len);

	// special condition: get the size of the required buffer
	if (mem == 0) {
		ssize_t rlen = llistxattr(path, NULL, 0);
        	if (rlen < 0) {
                	spockfs_errno(wsgi_req);
                	goto end;
        	}
		if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
		if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;
		spockfs_response_add_header_num(wsgi_req, "X-Spock-size", 12, rlen);
		goto end;
	}

	if (mem > spockfs.xattr_limit) {
		errno = ERANGE;
                spockfs_errno(wsgi_req);
		goto end;
	}

	buf = uwsgi_malloc(mem);

	ssize_t rlen = llistxattr(path, buf, mem);
	if (rlen < 0) {
		spockfs_errno(wsgi_req);
                goto end;
	}

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
	if (uwsgi_response_add_content_length(wsgi_req, rlen)) goto end;

	ssize_t i;
	for(i=0;i<rlen;i++) {
		if (buf[i] == 0) {
			buf[i] = '\n';
		}
	}

	uwsgi_response_write_body_do(wsgi_req, buf, rlen);

end:
	if (buf) free(buf);
        return UWSGI_OK;
}

static int spockfs_getxattr(struct wsgi_request *wsgi_req, char *path) {
        char *buf = NULL;
	char *name = NULL;


	uint16_t target_len = 0;
        char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
        if (!target) goto end;

	name = uwsgi_concat2n(target, target_len, "", 0);

        uint16_t size_len = 0;
        char *size = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_SIZE", 17, &size_len);
        if (!size) goto end;

        size_t mem = uwsgi_str_num(size, size_len);

        // special condition: get the size of the required buffer
        if (mem == 0) {
                ssize_t rlen = lgetxattr(path, name, NULL, 0);
                if (rlen < 0) {
                        spockfs_errno(wsgi_req);
                        goto end;
                }
                if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
                if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;
                spockfs_response_add_header_num(wsgi_req, "X-Spock-size", 12, rlen);
                goto end;
        }

        // use an option or this
        if (mem > spockfs.xattr_limit) {
		errno = ERANGE;
                spockfs_errno(wsgi_req);
                goto end;
        }

        buf = uwsgi_malloc(mem);

        ssize_t rlen = lgetxattr(path, name, buf, mem);
        if (rlen < 0) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
        if (uwsgi_response_add_content_length(wsgi_req, rlen)) goto end;

        uwsgi_response_write_body_do(wsgi_req, buf, rlen);

end:
	if (name) free(name);
        if (buf) free(buf);
        return UWSGI_OK;
}

static int spockfs_utimens(struct wsgi_request *wsgi_req, char *path) {

	int fd = open(path, O_WRONLY);
	if (fd < 0) {
		spockfs_errno(wsgi_req);
                goto end2;		
	}

	uint16_t atime_len = 0;
        char *atime = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_ATIME", 18, &atime_len);
        if (!atime) goto end;

	uint16_t mtime_len = 0;
        char *mtime = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MTIME", 18, &mtime_len);
        if (!mtime) goto end;

	struct timespec tv[2];
	tv[0].tv_sec = uwsgi_str_num(atime, atime_len);
	tv[0].tv_nsec = 0;	
	tv[1].tv_sec = uwsgi_str_num(mtime, mtime_len);
	tv[1].tv_nsec = 0;	

	if (futimens(fd, tv)) {
		spockfs_errno(wsgi_req);
                goto end;
	}
	
	if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
        if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;

end:
	close(fd);
end2:
        return UWSGI_OK;

}

static int spockfs_setxattr(struct wsgi_request *wsgi_req, char *path) {
        char *buf = NULL;
        char *name = NULL;


        uint16_t target_len = 0;
        char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
        if (!target) goto end;

        name = uwsgi_concat2n(target, target_len, "", 0);

        uint16_t flag_len = 0;
        char *flag = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_FLAG", 17, &flag_len);
        if (!flag) goto end;

	if (wsgi_req->post_cl > spockfs.xattr_limit) {
		errno = ERANGE;
		spockfs_errno(wsgi_req);
		goto end;
	}

	ssize_t body_len = 0;
	char *body = uwsgi_request_body_read(wsgi_req, wsgi_req->post_cl , &body_len);

        if (lsetxattr(path, name, body, body_len, uwsgi_str_num(flag, flag_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
        if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;

end:
        if (name) free(name);
        if (buf) free(buf);
        return UWSGI_OK;
}

static int spockfs_removexattr(struct wsgi_request *wsgi_req, char *path) {
        char *name = NULL;

        uint16_t target_len = 0;
        char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
        if (!target) goto end;

        name = uwsgi_concat2n(target, target_len, "", 0);

        if (lremovexattr(path, name)) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
        if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;

end:
        if (name) free(name);
        return UWSGI_OK;
}

static int spockfs_open(struct wsgi_request *wsgi_req, char *path) {

        uint16_t flag_len = 0;
        char *flag = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_FLAG", 17, &flag_len);
        if (!flag) goto end;

	int fd = open(path, uwsgi_str_num(flag, flag_len));
	if (fd < 0) {
		spockfs_errno(wsgi_req);
                goto end;
	}
	close(fd);

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
        if (uwsgi_response_add_content_length(wsgi_req, 0)) goto end;

end:
        return UWSGI_OK;
}



static int spockfs_truncate(struct wsgi_request *wsgi_req, char *path) {

        uint16_t size_len = 0;
        char *size = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_SIZE", 17, &size_len);
        if (!size) goto end;

        if (truncate(path, uwsgi_str_num(size, size_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
        uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_chmod(struct wsgi_request *wsgi_req, char *path) {

        uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
        if (!mode) goto end;

        if (chmod(path, uwsgi_str_num(mode, mode_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
        uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_mknod(struct wsgi_request *wsgi_req, char *path) {

        uint16_t mode_len = 0;
        char *mode = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_MODE", 17, &mode_len);
        if (!mode) goto end;

        uint16_t dev_len = 0;
        char *dev = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_DEV", 16, &dev_len);
        if (!dev) goto end;

        if (mknod(path, uwsgi_str_num(mode, mode_len), uwsgi_str_num(dev, dev_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "201 Created", 11);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}


static int spockfs_chown(struct wsgi_request *wsgi_req, char *path) {

        uint16_t uid_len = 0;
        char *uid = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_UID", 16, &uid_len);
        if (!uid) goto end;

	uint16_t gid_len = 0;
        char *gid = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_GID", 16, &gid_len);
        if (!gid) goto end;

        if (chown(path, uwsgi_str_num(uid, uid_len), uwsgi_str_num(gid, gid_len))) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_rename(struct wsgi_request *wsgi_req, char *path) {
        char path2[PATH_MAX+1];

        uint16_t target_len = 0;
        char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
        if (!target) goto end;

	if (spockfs_build_path(path2, wsgi_req, target, target_len)) {
		errno = ENOENT;
		spockfs_errno(wsgi_req);
		goto end;	
	}

        if (rename(path2, path)) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_link(struct wsgi_request *wsgi_req, char *path) {
	char path2[PATH_MAX+1];

        uint16_t target_len = 0;
        char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
        if (!target) goto end;

	if (spockfs_build_path(path2, wsgi_req, target, target_len)) {
                errno = ENOENT;
                spockfs_errno(wsgi_req);
                goto end;
        }

        if (link(path2, path)) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "201 Created", 11);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_symlink(struct wsgi_request *wsgi_req, char *path) {
	char *path2 = NULL;

	uint16_t target_len = 0;
	char *target = uwsgi_get_var(wsgi_req, "HTTP_X_SPOCK_TARGET", 19, &target_len);
	if (!target) goto end;

	path2 = uwsgi_concat2n(target, target_len, "", 0);

	if (symlink(path2, path)) {
		spockfs_errno(wsgi_req);
		goto end;
	}

	uwsgi_response_prepare_headers(wsgi_req, "201 Created", 11);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
	if (path2) free(path2);
        return UWSGI_OK;
}

static int spockfs_delete(struct wsgi_request *wsgi_req, char *path) {

	if (unlink(path)) {
		spockfs_errno(wsgi_req);
                goto end;
	}

	uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
	return UWSGI_OK;
}

static int spockfs_rmdir(struct wsgi_request *wsgi_req, char *path) {

        if (rmdir(path)) {
                spockfs_errno(wsgi_req);
                goto end;
        }

        uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6);
	uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_readlink(struct wsgi_request *wsgi_req, char *path) {

	struct stat st;
	if (lstat(path, &st)) {
		spockfs_errno(wsgi_req);
		goto end;
	}

	char *link = uwsgi_malloc(st.st_size);
	ssize_t rlen = readlink(path, link, st.st_size);
	if (rlen < 0) {
		free(link);
		spockfs_errno(wsgi_req);
                goto end;
	}

        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) {
		free(link);
		goto end;
	}

	if (uwsgi_response_add_content_length(wsgi_req, rlen)) {
		free(link);
		goto end;
	}

	uwsgi_response_write_body_do(wsgi_req, link, rlen);
	free(link);

end:
        return UWSGI_OK;
}


static int spockfs_getattr(struct wsgi_request *wsgi_req, char *path) {
	struct stat st;
	if (lstat(path, &st)) {
		spockfs_errno(wsgi_req);
		goto end;
	}
	if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;

	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-mode", 12, st.st_mode)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-uid", 11, st.st_uid)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-gid", 11, st.st_gid)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-size", 12, st.st_size)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-mtime", 13, st.st_mtime)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-atime", 13, st.st_atime)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-ctime", 13, st.st_ctime)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-nlink", 13, st.st_nlink)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-blocks", 14, st.st_blocks)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-dev", 11, st.st_dev)) goto end; 
	if (spockfs_response_add_header_num(wsgi_req, "X-Spock-ino", 11, st.st_ino)) goto end; 

	uwsgi_response_add_content_length(wsgi_req, 0);

end:
	return UWSGI_OK;
}

static int spockfs_statfs(struct wsgi_request *wsgi_req, char *path) {
        struct statvfs st;
        if (statvfs(path, &st)) {
                spockfs_errno(wsgi_req);
                goto end;
        }
        if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;

        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-bsize", 13, st.f_bsize)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-frsize", 14, st.f_frsize)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-blocks", 14, st.f_blocks)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-bfree", 13, st.f_bfree)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-bavail", 14, st.f_bavail)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-files", 13, st.f_files)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-ffree", 13, st.f_ffree)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-favail", 14, st.f_favail)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-fsid", 12, st.f_fsid)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-flag", 12, st.f_flag)) goto end;
        if (spockfs_response_add_header_num(wsgi_req, "X-Spock-namemax", 15, st.f_namemax)) goto end;

        uwsgi_response_add_content_length(wsgi_req, 0);

end:
        return UWSGI_OK;
}

static int spockfs_readdir(struct wsgi_request *wsgi_req, char *path) {
	int headers_sent = 0;
	struct uwsgi_buffer *ub = uwsgi_buffer_new(uwsgi.page_size);
	DIR *d = opendir(path);
	if (!d) {
		spockfs_errno(wsgi_req);
		goto end;
	}
	struct dirent de, *result;
	for(;;) {
		int ret = readdir_r(d, &de, &result);
		if (ret != 0) {
			spockfs_errno(wsgi_req);
			goto end;
		}
		if (!headers_sent) {
			if (uwsgi_response_prepare_headers(wsgi_req, "200 OK", 6)) goto end;
			headers_sent = 1;
		}
		if (!result) break;
		if (uwsgi_buffer_append(ub, de.d_name, strlen(de.d_name))) goto end;
		if (uwsgi_buffer_append(ub, "\n", 1)) goto end;
	}

	if (uwsgi_response_add_content_length(wsgi_req, ub->pos)) goto end;

	uwsgi_response_write_body_do(wsgi_req, ub->buf, ub->pos);
end:
	uwsgi_buffer_destroy(ub);
	if (d) closedir(d);
	return UWSGI_OK;
}

static int spockfs_request(struct wsgi_request *wsgi_req) {

	char path[PATH_MAX+1];

	if (uwsgi_parse_vars(wsgi_req)) {
                return -1;
        }

        if (wsgi_req->path_info_len == 0) {
                uwsgi_403(wsgi_req);
                return UWSGI_OK;
        }

	if (wsgi_req->path_info[0] != '/') {
                uwsgi_403(wsgi_req);
                return UWSGI_OK;
	}

	wsgi_req->app_id = uwsgi_get_app_id(wsgi_req, wsgi_req->appid, wsgi_req->appid_len, spockfs_plugin.modifier1);
	if (wsgi_req->app_id == -1 && !uwsgi.no_default_app && uwsgi.default_app > -1) {
		if (uwsgi_apps[uwsgi.default_app].modifier1 == spockfs_plugin.modifier1) {
			wsgi_req->app_id = uwsgi.default_app;
		}
	}

	if (wsgi_req->app_id == -1) {
		uwsgi_404(wsgi_req);
		return UWSGI_OK;
	}

	if (spockfs_build_path(path, wsgi_req, wsgi_req->path_info, wsgi_req->path_info_len)) {
		uwsgi_404(wsgi_req);
                return UWSGI_OK;
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "GETATTR", 7)) {
		return spockfs_getattr(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "ACCESS", 6)) {
		return spockfs_access(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "OPEN", 4)) {
		return spockfs_open(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "GET", 3)) {
		return spockfs_get(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "PUT", 3)) {
		return spockfs_put(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "POST", 4)) {
		return spockfs_post(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "MKNOD", 5)) {
		return spockfs_mknod(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "LINK", 4)) {
		return spockfs_link(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "RENAME", 6)) {
		return spockfs_rename(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "READDIR", 7)) {
		return spockfs_readdir(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "READDIR", 7)) {
		return spockfs_readdir(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "SYMLINK", 7)) {
		return spockfs_symlink(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "READLINK", 8)) {
		return spockfs_readlink(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "DELETE", 6)) {
		return spockfs_delete(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "MKDIR", 5)) {
		return spockfs_mkdir(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "RMDIR", 5)) {
		return spockfs_rmdir(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "CHMOD", 5)) {
		return spockfs_chmod(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "CHOWN", 5)) {
		return spockfs_chown(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "TRUNCATE", 8)) {
		return spockfs_truncate(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "FALLOCATE", 9)) {
		return spockfs_fallocate(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "STATFS", 6)) {
		return spockfs_statfs(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "LISTXATTR", 9)) {
		return spockfs_listxattr(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "GETXATTR", 8)) {
		return spockfs_getxattr(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "SETXATTR", 8)) {
		return spockfs_setxattr(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "REMOVEXATTR", 11)) {
		return spockfs_removexattr(wsgi_req, path);
	}

	if (!uwsgi_strncmp(wsgi_req->method, wsgi_req->method_len, "UTIMENS", 7)) {
		return spockfs_utimens(wsgi_req, path);
	}

	
	uwsgi_405(wsgi_req);

	return UWSGI_OK;
};

static int spockfs_init() {
	uwsgi.honour_range = 1;
	if (!spockfs.xattr_limit) spockfs.xattr_limit = 65536;
	return 0;
}

static void spockfs_apps() {
	struct uwsgi_string_list *usl = NULL;
	uwsgi_foreach(usl, spockfs.mountpoints) {
		char *equal = strchr(usl->value, '=');
		if (!equal || equal == (usl->value+usl->len)-1) {
			uwsgi_log("invalid spockfs mount syntax, must be <mountpoint>=<path>\n");
			exit(1);
		}
		// pretty useless, just for statistics
		time_t now = uwsgi_now();
		int id = uwsgi_apps_cnt;
		struct uwsgi_app *ua = uwsgi_add_app(id, spockfs_plugin.modifier1, usl->value, equal-usl->value, equal+1, (void *) usl->len - ((equal-usl->value)+1));
		if (!ua) {
			uwsgi_log("[spockfs] unable to mount %.*s\n", equal-usl->value, usl->value);
			exit(1);
		}

		ua->started_at = now;
		ua->startup_time = uwsgi_now() - now;
		uwsgi_log("SpockFS app/mountpoint %d (%.*s) loaded at %p for directory %.*s\n", id, equal-usl->value, usl->value, ua, usl->len - ((equal-usl->value)+1), equal+1);
	}
}

struct uwsgi_plugin spockfs_plugin = {
	.name = "spockfs",
	.modifier1 = 179,
	.options = spockfs_options,
	.init_apps = spockfs_apps,
	.request = spockfs_request,
	.after_request = log_request,
	.init = spockfs_init,
};
