from __future__ import print_function
import argparse
import errno
import radosfs
import unittest
import rados
import sys
import uuid

confPath = ''
confUser = ''
POOLS_PREFIX = 'libradosfs-python-test'

class RadosFsTestBase(unittest.TestCase):

    metadataPool = POOLS_PREFIX + '-metadata-' + str(uuid.uuid1())
    dataPool = POOLS_PREFIX + '-data-' + str(uuid.uuid1())

    def __init__(self, *args, **kwargs):
        unittest.TestCase.__init__(self, *args, **kwargs)
        self.fs = radosfs.Filesystem()
        self.assertEqual(self.fs.init(confUser, confPath), 0)
        self.cluster = rados.Rados(conffile=confPath)
        self.cluster.connect()

    def createPools(self):
        self.cluster.create_pool(self.dataPool)
        self.cluster.create_pool(self.metadataPool)
        self.mtdPoolCtx = self.cluster.open_ioctx(self.metadataPool)
        self.dataPoolCtx = self.cluster.open_ioctx(self.dataPool)

    def removePools(self):
        self.mtdPoolCtx.close()
        self.dataPoolCtx.close()
        self.cluster.delete_pool(self.dataPool)
        self.cluster.delete_pool(self.metadataPool)

    def setUp(self):
        self.createPools()

    def tearDown(self):
        self.removePools()

    def addPools(self):
        self.assertEqual(self.fs.addMetadataPool(self.metadataPool, '/'), 0)
        self.assertEqual(self.fs.addDataPool(self.dataPool, '/'), 0)

    def test_poolsAdditionAndRemoval(self):
        self.addPools()
        self.assertEqual(self.fs.removeMetadataPool(self.metadataPool), 0)
        self.assertEqual(self.fs.removeDataPool(self.dataPool), 0)

    def test_fileCreation(self):
        self.addPools()
        file = radosfs.File(self.fs, '/my-file')
        self.assertFalse(file.exists())
        self.assertEqual(file.create(), 0)
        self.assertTrue(file.exists())
        self.assertEqual(file.create(), -errno.EEXIST)

    def test_fileRemoval(self):
        self.addPools()
        file = radosfs.File(self.fs, '/my-file')
        self.assertEqual(file.remove(), -errno.ENOENT)
        self.assertEqual(file.create(), 0)
        self.assertEqual(file.remove(), 0)
        self.assertEqual(file.remove(), -errno.ENOENT)

    def test_dirCreation(self):
        self.addPools()
        dir = radosfs.Dir(self.fs, '/my-dir')
        self.assertFalse(dir.exists())
        self.assertEqual(dir.create(), 0)
        self.assertTrue(dir.exists())
        self.assertEqual(dir.create(), -errno.EEXIST)

    def test_dirRemoval(self):
        self.addPools()
        subdir = radosfs.Dir(self.fs, '/dir/subdir')
        self.assertFalse(subdir.exists())
        self.assertEqual(subdir.create(-1, True), 0)
        self.assertTrue(subdir.exists())

        dir = radosfs.Dir(self.fs, '/dir/')
        self.assertTrue(dir.exists())
        self.assertEqual(dir.remove(), -errno.ENOTEMPTY)
        self.assertTrue(dir.exists())

        self.assertEqual(subdir.remove(), 0)
        self.assertFalse(subdir.exists())
        self.assertEqual(dir.remove(), 0)
        self.assertFalse(dir.exists())

def setupArguments():
    parser = argparse.ArgumentParser()
    # change the name of the 'optional arguments' label, although this should
    # be accomplished in a better way than setting a private var
    parser._optionals.title = 'arguments'
    parser.add_argument('-c', '--conf', help="path to the Ceph cluster's "
                        "configuration file", required=True)
    parser.add_argument('-u', '--user', help="the user name to connect to the "
                        "cluster")
    return parser

def help():
    print(sys.argv[0] + ' --conf=')

if __name__ == '__main__':
    parser = setupArguments()
    args, unknown = parser.parse_known_args()
    sys.argv = [sys.argv[0]] + unknown

    confPath = args.conf
    confUser = args.user if args.user else ''

    print('Running tests on "%s" with user "%s"' % (confPath, confUser))

    unittest.main()
