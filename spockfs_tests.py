import unittest
import os
import shutil
import stat
import time
import xattr

FS_DIR = '/tmp/.spockfs_testdir'

# clear the fs
for item in os.listdir(FS_DIR):
    path = os.path.join(FS_DIR, item)
    if os.path.isdir(path):
        shutil.rmtree(path)
    else:
        os.remove(path)

class SpockFS(unittest.TestCase):

    def setUp(self):
        self.testpath = FS_DIR

    def test_mkdir(self):
        path = os.path.join(self.testpath, 'foobar')
        self.assertIsNone(os.mkdir(path))
        self.assertRaises(OSError, os.mkdir, path)
        self.assertIsNone(os.rmdir(path))
        self.assertRaises(OSError, os.rmdir, path)

    def test_stat(self):
        path = os.path.join(self.testpath, 'tinyfile')
        with open(path, 'w') as f:
            f.write('hello')
        self.assertIsNone(os.chmod(path, 0))
        s = os.stat(path)
        self.assertEqual(s.st_size, 5)
        self.assertIsNone(os.remove(path))

    def test_unlink(self):
        path = os.path.join(self.testpath, 'notfound')
        self.assertRaises(OSError, os.remove, path)
        with open(path, 'w') as f:
            f.write('i do not exist')
        self.assertIsNone(os.remove(path))
        self.assertFalse(os.path.exists(path))

    def test_rmdir(self):
        path = os.path.join(self.testpath, 'destroyme')
        self.assertIsNone(os.mkdir(path))
        self.assertTrue(os.path.exists(path))
        self.assertIsNone(os.rmdir(path))
        self.assertFalse(os.path.exists(path))

    def test_move(self):
        path = os.path.join(self.testpath, 'movehere')
	self.assertIsNone(os.mkdir(path))
        path0 = os.path.join(self.testpath, 'item001')
        with open(path0, 'w') as f:
            f.write('moveme')
        path1 = os.path.join(self.testpath, 'movehere', 'item0002')
        self.assertIsNone(os.rename(path0, path1))
        with open(path1, 'r') as f:
            self.assertEqual(f.read(), 'moveme') 

    def test_bigfile(self):
        path = os.path.join(self.testpath, 'bigfile')
        with open(path, 'w') as f:
            f.write('spock' * 179 * 1024)
        with open(path, 'r') as f:
            self.assertEqual(f.read(), 'spock' * 179 * 1024)

    def test_bigfile_with_random(self):
        path = os.path.join(self.testpath, 'bigfile2')
        blob = os.urandom(1024 * 1024)
        with open(path, 'w') as f:
            f.write(blob)
        with open(path, 'r') as f:
            self.assertEqual(f.read(), blob)
        self.assertIsNone(os.remove(path))
        self.assertFalse(os.path.exists(path))

    def test_symlink(self):
        path = os.path.join(self.testpath, 'linkme')
        with open(path, 'w') as f:
            f.write('linked')
        path2 = os.path.join(self.testpath, 'iamalink')
        self.assertIsNone(os.symlink(path, path2))
        self.assertEqual(os.readlink(path2), path)
        s = os.lstat(path2)
        self.assertTrue(stat.S_ISLNK(s.st_mode))
        self.assertIsNone(os.remove(path2))
        self.assertTrue(os.path.exists(path))
        self.assertFalse(os.path.exists(path2))

    def test_link(self):
        path = os.path.join(self.testpath, 'fastcopy')
        with open(path, 'w') as f:
            f.write('copyme')
        path0 = os.path.join(self.testpath, 'linkdir0')
        self.assertIsNone(os.mkdir(path0))
        path1 = os.path.join(path0, 'linkdir1')
        self.assertIsNone(os.mkdir(path1))
        path2 = os.path.join(path1, 'linkdir2')
        self.assertIsNone(os.mkdir(path2))
        path3 = os.path.join(path2, 'linked')
        self.assertIsNone(os.link(path, path3))
        with open(path3, 'r') as f:
            self.assertEqual(f.read(), 'copyme')

    def test_truncate(self):
        path = os.path.join(self.testpath, 'resizeme')
        with open(path, 'w') as f:
            os.ftruncate(f.fileno(), 179)
        s = os.stat(path)
        self.assertEqual(s.st_size, 179)

    def test_utimens(self):
        path = os.path.join(self.testpath, 'touch')
        with open(path, 'w') as f:
            os.ftruncate(f.fileno(), 1)
        now = 179
        self.assertIsNone(os.utime(path, (now, now)))
        s = os.stat(path)
        self.assertEqual(s[7], 179)
        self.assertEqual(s[8], 179)
         

    def test_xattr(self):
        path = os.path.join(self.testpath, 'keyvalue')
        with open(path, 'w') as f:
            os.ftruncate(f.fileno(), 1)
        x = xattr.xattr(path)
        x['user.spock_key'] = '179'
        x['user.spock_key2'] = '276'
        self.assertEqual(x['user.spock_key'], '179')
        self.assertEqual(x['user.spock_key2'], '276')
        del(x['user.spock_key2'])
        self.assertTrue('user.spock_key2' not in x)
        x['user.spock_key2'] = '276'
        self.assertTrue('user.spock_key' in x)
        self.assertTrue('user.spock_key2' in x)
        self.assertFalse('user.spock_key3' in x)
 

    def test_readdir(self):
        path = os.path.join(self.testpath, 'scanme')
        self.assertIsNone(os.mkdir(path))
        path0 = os.path.join(path, 'file000')
        path1 = os.path.join(path, 'file001')
	with open(path0, 'w') as f:
            f.write('0')
	with open(path1, 'w') as f:
            f.write('1')
        items = ['.', '..', 'file000', 'file001']
        ldir = ['.', '..'] + os.listdir(path)
        ldir.sort()
        self.assertEqual(items, ldir)

    def test_mknod(self):
        path = os.path.join(self.testpath, 'the_fifo_of_spock')
        self.assertIsNone(os.mknod(path, stat.S_IFIFO))
        s = os.stat(path)
        self.assertTrue(stat.S_ISFIFO(s.st_mode))

    def test_statfs(self):
        s = os.statvfs(self.testpath)
        self.assertTrue(s.f_bsize > 0)

    def test_access(self):
        path = os.path.join(self.testpath, 'forbidden')
        with open(path, 'w') as f:
            f.write('x')
        self.assertIsNone(os.chmod(path, 0))
        self.assertFalse(os.access(path, os.W_OK))
        self.assertFalse(os.access(path, os.R_OK))
        self.assertIsNone(os.chmod(path, stat.S_IRUSR))
        self.assertTrue(os.access(path, os.R_OK))
         


if __name__ == '__main__':
    unittest.main()
