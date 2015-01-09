#define FUSE_USE_VERSION 26
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <pthread.h>

#define spockfs_check(x) if (sh_rr->code != x) {\
                		ret = spockfs_errno(sh_rr->code);\
                		goto end;\
        		}

#define spockfs_check2(x, y) if (sh_rr->code != x && sh_rr->code != y) {\
                                ret = spockfs_errno(sh_rr->code);\
                                goto end;\
                        }

#define spockfs_init() int ret = -EIO; \
			struct spockfs_http_rr *sh_rr = calloc(sizeof(struct spockfs_http_rr), 1);\
			if (!sh_rr) {\
				return -ENOMEM;\
			}

#define spockfs_init2() spockfs_init();\
			struct curl_slist *headers = NULL
			

#define spockfs_free() if (sh_rr->buf) free(sh_rr->buf);\
        		free(sh_rr);\
        		return ret

#define spockfs_free2() if (headers) curl_slist_free_all(headers);\
			spockfs_free()

#define spockfs_run(x, y) if ((ret = spockfs_http(x, path, sh_rr, y))) goto end

#define spockfs_header_num(x, y) headers = spockfs_add_header_num(headers, x, y);\
        if (!headers) {\
                ret = -ENOMEM;\
                goto end;\
        }

#define spockfs_header_range(x, y, z) headers = spockfs_add_header_range(headers, x, y, z);\
        if (!headers) {\
                ret = -ENOMEM;\
                goto end;\
        }

#define spockfs_header_target(x) headers = spockfs_add_header_target(headers, x);\
        if (!headers) {\
                ret = -ENOMEM;\
                goto end;\
        }

static struct spockfs_config {
	char *http_url;
	size_t http_url_len;
	CURLSH *dns_cache;
	pthread_mutex_t dns_lock;
} spockfs_config;

struct spockfs_http_rr {
	char *buf;
	size_t len;

	const char *body;
	size_t body_len;

	long code;

	mode_t x_spock_mode;
	uid_t x_spock_uid;
	gid_t x_spock_gid;
	dev_t x_spock_dev;
	ino_t x_spock_ino;
	uint64_t x_spock_size;
	time_t x_spock_atime;
	time_t x_spock_mtime;
	time_t x_spock_ctime;
	uint64_t x_spock_nlink;
	uint64_t x_spock_blocks;
	uint64_t x_spock_flag;

	uint64_t x_spock_bsize;
	uint64_t x_spock_frsize;
	uint64_t x_spock_bfree;
	uint64_t x_spock_bavail;
	uint64_t x_spock_files;
	uint64_t x_spock_ffree;
	uint64_t x_spock_favail;
	uint64_t x_spock_fsid;
	uint64_t x_spock_namemax;
};


static struct curl_slist *spockfs_add_header_num(struct curl_slist *headers, char *name, int64_t value) {
	char header[64];
        int ret = snprintf(header, 64, "X-Spock-%s: %lld", name, (long long) value);
        if (ret <= 0 || ret > 64) {
		if (headers) curl_slist_free_all(headers);
                return NULL;
        } 
        struct curl_slist *ret_headers = curl_slist_append(headers, header);
	// free old headers in case of error
	if (!ret_headers) {
		if (headers) curl_slist_free_all(headers);
	}
	return ret_headers;
}

static struct curl_slist *spockfs_add_header_range(struct curl_slist *headers, char *name, uint64_t from, uint64_t to) {
	char header[64];
        int ret = snprintf(header, 64, "%s: bytes=%llu-%llu", name, (unsigned long long) from, (unsigned long long) to);
        if (ret <= 0 || ret > 64) {
		if (headers) curl_slist_free_all(headers);
                return NULL;
        }
        struct curl_slist *ret_headers = curl_slist_append(headers, header);
        // free old headers in case of error
        if (!ret_headers) {
                if (headers) curl_slist_free_all(headers);
        }
        return ret_headers;
}

static struct curl_slist *spockfs_add_header_target(struct curl_slist *headers, const char *target) {
	size_t target_len = strlen(target);
	char *header = malloc(17 + target_len);
	if (!header) {
		if (headers) curl_slist_free_all(headers);
		return NULL;
	}
	memcpy(header, "X-Spock-target: ", 16);
	memcpy(header + 16, target, target_len);
	header[16 + target_len] = 0;
        struct curl_slist *ret_headers = curl_slist_append(headers, header);
	free(header);
        // free old headers in case of error
        if (!ret_headers) {
                if (headers) curl_slist_free_all(headers);
        }
        return ret_headers;
}

static int64_t spockfs_get_header_num(char *s, size_t s_len, char *header, size_t header_len) {
	if (s_len < header_len) return -1;
	if (strncasecmp(s, header, header_len)) return -1;
	if (s_len - header_len == 0) return -1;
	char *backslash_r = memchr(s + header_len, '\r', s_len - (header_len + 1)); 
	*backslash_r = 0;
	return strtoll(s + header_len, NULL, 10);
}

size_t spockfs_http_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
	struct spockfs_http_rr *sh_rr = (struct spockfs_http_rr *) userdata;
	size_t len = size * nmemb;
	if (!sh_rr->buf) {
		sh_rr->buf = malloc(len);
	}
	else {
		char *tmp = realloc(sh_rr->buf, sh_rr->len + len);
		if (!tmp) return -1;
		sh_rr->buf = tmp;
	}
	memcpy(sh_rr->buf + sh_rr->len, ptr, len);
	sh_rr->len += len;
	return len;
}

size_t spockfs_http_headers(char *ptr, size_t size, size_t nmemb, void *userdata) {
        struct spockfs_http_rr *sh_rr = (struct spockfs_http_rr *) userdata;
        size_t len = size * nmemb;
	int64_t value = -1;
	if ((value = spockfs_get_header_num(ptr, len, "X-Spock-size: ", 14)) >= 0) {
		sh_rr->x_spock_size = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-mode: ", 14)) >= 0) {
		sh_rr->x_spock_mode = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-uid: ", 13)) >= 0) {
		sh_rr->x_spock_uid = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-gid: ", 13)) >= 0) {
		sh_rr->x_spock_gid = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-mtime: ", 15)) >= 0) {
		sh_rr->x_spock_mtime = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-atime: ", 15)) >= 0) {
		sh_rr->x_spock_atime = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-ctime: ", 15)) >= 0) {
		sh_rr->x_spock_ctime = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-nlink: ", 15)) >= 0) {
		sh_rr->x_spock_nlink = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-blocks: ", 16)) >= 0) {
		sh_rr->x_spock_blocks = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-dev: ", 13)) >= 0) {
		sh_rr->x_spock_dev = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-ino: ", 13)) >= 0) {
		sh_rr->x_spock_ino = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-bsize: ", 15)) >= 0) {
		sh_rr->x_spock_bsize = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-frsize: ", 16)) >= 0) {
		sh_rr->x_spock_frsize = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-bfree: ", 15)) >= 0) {
		sh_rr->x_spock_bfree = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-bavail: ", 16)) >= 0) {
		sh_rr->x_spock_bavail = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-files: ", 15)) >= 0) {
		sh_rr->x_spock_files = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-ffree: ", 15)) >= 0) {
		sh_rr->x_spock_ffree = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-favail: ", 16)) >= 0) {
		sh_rr->x_spock_favail = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-fsid: ", 14)) >= 0) {
		sh_rr->x_spock_fsid = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-flag: ", 14)) >= 0) {
		sh_rr->x_spock_flag = value;
	} 
	else if ((value = spockfs_get_header_num(ptr, len, "X-Spock-namemax: ", 17)) >= 0) {
		sh_rr->x_spock_namemax = value;
	} 
        return len;
}

static char *spockfs_prepare_url(const char *base, size_t base_len, const char *path) {
	size_t path_len = strlen(path);
	// use two additional bytes for final \0 and for corner-case snprintf
	size_t url_len = base_len + (path_len * 3) + 2;
	char *url = malloc(url_len);
	if (!url) return NULL;
	memcpy(url, base, base_len);
	char *ptr = url + base_len;
	size_t i;
	for(i=0;i<path_len;i++) {
		char c = path[i];
		if ( (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c =='~' || c == '/') {
			*ptr++= c;
		}
		else {
			int ret = snprintf(ptr, url_len - (ptr - url), "%%%02X", (uint8_t) c);
			if (ret != 3) {
				free(url);
				return NULL;
			}
			ptr+=3;
		}
	}

	// final \0
	*ptr = 0;

	return url;
}

#ifdef CURLOPT_XFERINFOFUNCTION
static int spockfs_interrupted(void *clientp, curl_off_t dltotal,  curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
#else
static int spockfs_interrupted(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
#endif
	if (fuse_interrupted()) return -1;
	return 0;
}

static int spockfs_http(const char *method, const char *path, struct spockfs_http_rr *sh_rr, struct curl_slist *headers) {
	int ret = -EIO;
	if (!sh_rr) return ret;

	char *url = spockfs_prepare_url(spockfs_config.http_url, spockfs_config.http_url_len, path);
	if (!url) {
		return -ENOMEM;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		free(url);
		return -ENOMEM;
	}

#ifdef CURLOPT_XFERINFOFUNCTION
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, spockfs_interrupted);
#else
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, spockfs_interrupted);
#endif
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_SHARE, spockfs_config.dns_cache);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	if (sh_rr->body && sh_rr->body_len) {
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, sh_rr->body);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, sh_rr->body_len);
		headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
		headers = curl_slist_append(headers, "Expect: ");
	}
	if (headers) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, spockfs_http_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, sh_rr);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, spockfs_http_headers);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, sh_rr);
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		goto end;
	}
#ifdef CURLINFO_RESPONSE_CODE
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &sh_rr->code);
#else
	curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &sh_rr->code);
#endif
	ret = 0;
end:
	free(url);
	curl_easy_cleanup(curl);
	return ret;
}

static int spockfs_errno(long code) {
	switch(code) {
		case 403:
			return -EACCES;
		case 404:
			return -ENOENT;
		case 405:
			return -ENOSYS;
		case 409:
			return -EEXIST;
		case 412:
			return -ENOTEMPTY;
		case 413:
			return -ERANGE;
		case 415:
#ifdef ENODATA
			return -ENODATA;
#endif
#ifdef ENOATTR
			return -ENOATTR;
#endif
			break;
		default:
			break;
	}

	return -EIO;
}

static int spockfs_getattr(const char *path, struct stat *st) {

	spockfs_init();

	spockfs_run("GETATTR", NULL);

	spockfs_check(200);

        ret = 0;
	st->st_rdev = 0;
	st->st_blksize = 0;
	st->st_ino = sh_rr->x_spock_ino;
	st->st_dev = sh_rr->x_spock_dev;
	st->st_mode = sh_rr->x_spock_mode;
	st->st_uid = sh_rr->x_spock_uid;
	st->st_gid = sh_rr->x_spock_gid;
	st->st_size = sh_rr->x_spock_size;
	st->st_mtime = sh_rr->x_spock_mtime;
	st->st_atime = sh_rr->x_spock_atime;
	st->st_ctime = sh_rr->x_spock_ctime;
	st->st_nlink = sh_rr->x_spock_nlink;
	st->st_blocks = sh_rr->x_spock_blocks;

end:
	spockfs_free();
}

static int spockfs_statfs(const char *path, struct statvfs *vfs) {

	spockfs_init();
        
        spockfs_run("STATFS", NULL);

	spockfs_check(200);

        ret = 0;
        vfs->f_bsize = sh_rr->x_spock_bsize;
        vfs->f_frsize = sh_rr->x_spock_frsize;
        vfs->f_blocks = sh_rr->x_spock_blocks;
        vfs->f_bfree = sh_rr->x_spock_bfree;
        vfs->f_bavail = sh_rr->x_spock_bavail;
        vfs->f_files = sh_rr->x_spock_files;
        vfs->f_ffree = sh_rr->x_spock_ffree;
        vfs->f_favail = sh_rr->x_spock_favail;
        vfs->f_fsid = sh_rr->x_spock_fsid;
        vfs->f_flag = sh_rr->x_spock_flag;
        vfs->f_namemax = sh_rr->x_spock_namemax;

end:
	spockfs_free();
}

static int spockfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	spockfs_init();

        spockfs_run("READDIR", NULL);

        spockfs_check(200);

	// transform \n to \0
	size_t i, len = sh_rr->len;
	char *base = sh_rr->buf;
	for(i=0;i<len;i++) {
		if (sh_rr->buf[i] == '\n') {
			sh_rr->buf[i] = 0;
			filler(buf, base, NULL, 0);
			base = sh_rr->buf + i + 1;
			continue;
		}
	}
	ret = 0;
end:
	spockfs_free();
}

static int spockfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	spockfs_init2();

	spockfs_header_num("mode", mode);

	spockfs_run("POST", headers);

	spockfs_check(201);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_mknod(const char *path, mode_t mode, dev_t dev) {

	spockfs_init2();

	spockfs_header_num("mode", mode);

	spockfs_header_num("dev", dev);

        spockfs_run("MKNOD", headers);

	spockfs_check(201);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_open(const char *path, struct fuse_file_info *fi) {

	spockfs_init2();

        spockfs_header_num("flag", fi->flags);

	spockfs_run("OPEN", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_access(const char *path, int mode) {

	spockfs_init2();

	spockfs_header_num("mode", mode);

	spockfs_run("ACCESS", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	spockfs_init2();

        spockfs_header_range("Range", offset, ((offset+size)-1));

	spockfs_run("GET", headers);

	spockfs_check2(206, 200);

	if (sh_rr->len >= size) {
		memcpy(buf, sh_rr->buf, size);
	}
	else {
		memcpy(buf, sh_rr->buf, sh_rr->len);
		// fill with zero
		memset(buf + sh_rr->len, 0, size - sh_rr->len);
	}

        ret = size;
end:
	spockfs_free2();
}

static int spockfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	spockfs_init2();

        spockfs_header_range("Content-Range", offset, ((offset+size)-1));

	sh_rr->body = buf;
	sh_rr->body_len = size;

	spockfs_run("PUT", headers);

	spockfs_check(200);

        ret = size;
end:
	spockfs_free2();
}

static int spockfs_chmod(const char *path, mode_t mode) {

	spockfs_init2();

	spockfs_header_num("mode", mode);

	spockfs_run("CHMOD", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_chown(const char *path, uid_t uid, gid_t gid) {

	spockfs_init2();

        spockfs_header_num("uid", uid);
        spockfs_header_num("gid", gid);

	spockfs_run("CHOWN", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_listxattr(const char *path, char *buf, size_t len) {

	spockfs_init2();

        spockfs_header_num("size", len);	

	spockfs_run("LISTXATTR", headers);

	spockfs_check(200);

	// special case for 0 length
	if (len == 0) {
		return sh_rr->x_spock_size;
	}

	if (sh_rr->len > len) {
		ret = -ERANGE;
		goto end;
	}

	// transform \n to \0
	uint64_t i;
	for(i=0;i<sh_rr->len;i++) {
		if (sh_rr->buf[i] == '\n') {
			buf[i] = 0;
		}
		else {
			buf[i] = sh_rr->buf[i];
		}
	}

        ret = sh_rr->len;
end:
	spockfs_free2();
}

#ifndef __APPLE__
static int spockfs_getxattr(const char *path, const char *name, char *buf, size_t len) {
#else
static int spockfs_getxattr(const char *path, const char *name, char *buf, size_t len, uint32_t position) {
#endif

	spockfs_init2();

	spockfs_header_num("size", len);

	spockfs_header_target(name);

        spockfs_run("GETXATTR", headers);

	spockfs_check(200);

        // special case for 0 length
        if (len == 0) {
                return sh_rr->x_spock_size;
        }

        if (sh_rr->len > len) {
                ret = -ERANGE;
                goto end;
        }

	memcpy(buf, sh_rr->buf, sh_rr->len);

        ret = sh_rr->len;
end:
	spockfs_free2();
}

static int spockfs_removexattr(const char *path, const char *name) {

	spockfs_init2();

	spockfs_header_target(name);

	spockfs_run("REMOVEXATTR", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

#ifndef __APPLE__
static int spockfs_setxattr(const char *path, const char *name, const char *buf, size_t len, int flag) {
#else
static int spockfs_setxattr(const char *path, const char *name, const char *buf, size_t len, int flag, uint32_t position) {
#endif

	spockfs_init2();

	spockfs_header_num("flag", flag);

        spockfs_header_target(name);

	sh_rr->body = buf;
	sh_rr->body_len = len;

        spockfs_run("SETXATTR", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_truncate(const char *path, off_t n) {

	spockfs_init2();

	spockfs_header_num("size", n);

	spockfs_run("TRUNCATE", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_rename(const char *target, const char *path) {

	spockfs_init2();

	spockfs_header_target(target);

	spockfs_run("RENAME", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_link(const char *target, const char *path) {

	spockfs_init2();

        spockfs_header_target(target);

        spockfs_run("LINK", headers);

	spockfs_check(201);

        ret = 0;
end:
	spockfs_free2();
}

static int spockfs_symlink(const char *target, const char *path) {

	spockfs_init2();

        spockfs_header_target(target);

        spockfs_run("SYMLINK", headers);

	spockfs_check(201);

	ret = 0;

end:
	spockfs_free2();
}

static int spockfs_readlink(const char *path, char *buf, size_t len) {

	spockfs_init();

        spockfs_run("READLINK", NULL);

	spockfs_check(200);

	if (!sh_rr->buf) goto end;

	if (sh_rr->len >= len) {
		// truncate
		memcpy(buf, sh_rr->buf, len-1);	
		buf[len-1] = 0;
	}
	else {
		memcpy(buf, sh_rr->buf, sh_rr->len);
		buf[sh_rr->len] = 0;
	}

	ret = 0;
end:
	spockfs_free();
}

static int spockfs_unlink(const char *path) {

	spockfs_init();

        spockfs_run("DELETE", NULL);

	spockfs_check(200);

	ret = 0;
end:
	spockfs_free();
}

static int spockfs_rmdir(const char *path) {

	spockfs_init();

	spockfs_run("RMDIR", NULL);
        
	spockfs_check(200);
        
        ret = 0;
end:    
	spockfs_free();
}

static int spockfs_mkdir(const char *path, mode_t mode) {

	spockfs_init2();

	spockfs_header_num("mode", mode);

	spockfs_run("MKDIR", headers);

	spockfs_check(201);

        ret = 0;
end:
	spockfs_free2();
}

#ifndef __APPLE__
#if FUSE_MAJOR_VERSION > 2 || (FUSE_MAJOR_VERSION == 2 && FUSE_MINOR_VERSION >= 9)
static int spockfs_fallocate(const char *path, int mode, off_t offset, off_t size, struct fuse_file_info *fi) {
	
	spockfs_init2();

        spockfs_header_num("mode", mode);

	spockfs_header_range("Range", offset, (offset+size)-1);

	spockfs_run("FALLOCATE", headers);
	
	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();	
}
#endif
#endif

static int spockfs_utimens(const char *path, const struct timespec tv[2]) {

	spockfs_init2();

	spockfs_header_num("atime", tv[0].tv_sec);

	spockfs_header_num("mtime", tv[1].tv_sec);

	spockfs_run("UTIMENS", headers);

	spockfs_check(200);

        ret = 0;
end:
	spockfs_free2();
}

static struct fuse_operations spockfs_ops = {
	.readdir = spockfs_readdir,
	.getattr = spockfs_getattr,
	.create = spockfs_create,
	.mknod = spockfs_mknod,
	.open = spockfs_open,
	.chmod = spockfs_chmod,
	.chown = spockfs_chown,
	.truncate = spockfs_truncate,
	.write = spockfs_write,
	.read = spockfs_read,
	.access = spockfs_access,
	.symlink = spockfs_symlink,
	.readlink = spockfs_readlink,
	.unlink = spockfs_unlink,
	.rmdir = spockfs_rmdir,
	.mkdir = spockfs_mkdir,
	.link = spockfs_link,
	.rename = spockfs_rename,
#ifndef __APPLE__
#if FUSE_MAJOR_VERSION > 2 || (FUSE_MAJOR_VERSION == 2 && FUSE_MINOR_VERSION >= 9)
	.fallocate = spockfs_fallocate,
#endif
#endif
	.statfs = spockfs_statfs,
	.listxattr = spockfs_listxattr,
	.getxattr = spockfs_getxattr,
	.setxattr = spockfs_setxattr,
	.removexattr = spockfs_removexattr,
	.utimens = spockfs_utimens,
};

static int spockfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs) {
	if (key == FUSE_OPT_KEY_NONOPT) {
		if (!spockfs_config.http_url) {
			spockfs_config.http_url = strdup(arg);
			spockfs_config.http_url_len = strlen(spockfs_config.http_url);
			// strip final slashes
			while(spockfs_config.http_url_len) {
				size_t pos = spockfs_config.http_url_len-1;
				if (spockfs_config.http_url[pos] != '/') break;
				spockfs_config.http_url_len--;
			}
			return 0;
		}
	}
	return 1;
}

void spockfs_dns_lock(CURL *curl, curl_lock_data data, curl_lock_access access, void *userptr) {
	pthread_mutex_lock(&spockfs_config.dns_lock);
}

void spockfs_dns_unlock(CURL *curl, curl_lock_data data, void *userptr) {
	pthread_mutex_unlock(&spockfs_config.dns_lock);
}


int main(int argc, char *argv[]) {
	pthread_mutex_init(&spockfs_config.dns_lock, NULL);
	spockfs_config.dns_cache = curl_share_init();
	curl_share_setopt(spockfs_config.dns_cache, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	curl_share_setopt(spockfs_config.dns_cache, CURLSHOPT_LOCKFUNC, spockfs_dns_lock);
	curl_share_setopt(spockfs_config.dns_cache, CURLSHOPT_UNLOCKFUNC, spockfs_dns_unlock);
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	fuse_opt_parse(&args, NULL, NULL, spockfs_opt_proc);
	return fuse_main(args.argc, args.argv, &spockfs_ops, NULL);
}
