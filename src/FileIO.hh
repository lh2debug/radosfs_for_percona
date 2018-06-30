/*
 * Rados Filesystem - A filesystem library based in librados
 *
 * Copyright (C) 2014-2015 CERN, Switzerland
 *
 * Author: Joaquim Rocha <joaquim.rocha@cern.ch>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef RADOS_FS_FILE_IO_HH
#define RADOS_FS_FILE_IO_HH

#include <boost/chrono.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cstdlib>
#include <rados/librados.hpp>
#include <string>
#include <utility>
#include <vector>
#include <tr1/memory>

#include "Filesystem.hh"
#include "FileInlineBuffer.hh"
#include "AsyncOp.hh"
#include "radosfscommon.h"

#define FILE_CHUNK_LOCKER "file-chunk-locker"
#define FILE_CHUNK_LOCKER_COOKIE_WRITE "file-chunk-locker-cookie-write"
#define FILE_CHUNK_LOCKER_COOKIE_OTHER "file-chunk-locker-cookie-other"
#define FILE_CHUNK_LOCKER_TAG "file-chunk-locker-tag"
#define FILE_LOCK_DURATION 120 // seconds

RADOS_FS_BEGIN_NAMESPACE

class FileIO;

typedef std::tr1::shared_ptr<AsyncOp> AsyncOpSP;
typedef std::tr1::shared_ptr<FileIO> FileIOSP;

class FileReadDataImp : public FileReadData
{
public:
  FileReadDataImp(char *buff, off_t offset, size_t length, ssize_t *retValue=0);

  FileReadDataImp(const FileReadDataImp &otherFileReadData);

  FileReadDataImp(const FileReadData &readData);

  ~FileReadDataImp(void);

  void addReturnValue(int value);

  boost::shared_ptr<boost::shared_mutex> readOpMutex;
  librados::bufferlist *buffList;
  int opResult;
};

typedef boost::shared_ptr<FileReadDataImp> FileReadDataImpSP;

struct ReadOpArgs
{
  AsyncOpSP asyncOp;
  boost::shared_ptr<boost::shared_mutex> readOpMutex;
  boost::shared_ptr<ssize_t> inodeSize;
  FileIO *fileIO;
};

struct ReadInlineOpArgs : ReadOpArgs
{
  std::string fileBaseName;
  std::map<std::string, librados::bufferlist> omap;
  std::vector<FileReadDataImpSP> readData;
};

struct ReadChunkOpArgs : ReadOpArgs
{
  size_t fileChunk;
  std::vector<std::pair<FileReadDataImpSP, librados::bufferlist *> > readData;
};

struct OpsManager
{
  boost::mutex opsMutex;
  std::map<std::string, AsyncOpSP> mOperations;

  int sync(bool removeOps=true);
  int sync(const std::string &opId, bool lock=true, bool removeOps=true);
  void waitForLoneOps(void);
  void addOperation(AsyncOpSP op);
  bool hasRunningOps(void);
};

class FileIO
{
public:
  FileIO(Filesystem *radosFs,
         const PoolSP pool,
         const std::string &iNode,
         size_t chunkSize);

  FileIO(Filesystem *radosFs,
         const PoolSP pool,
         const std::string &iNode,
         const std::string &filePath,
         size_t chunkSize);

  ~FileIO();

  ssize_t read(char *buff, off_t offset, size_t blen);

  int read(const std::vector<FileReadData> &intervals,
           std::string *asyncOpId = 0, AsyncOpCallback callback = 0,
           void *arg = 0);

  int write(const char *buff, off_t offset, size_t blen, std::string *opId = 0,
            bool copyBuffer=false, AsyncOpCallback callback = 0, void *arg = 0);
  int writeSync(const char *buff, off_t offset, size_t blen);

  std::string inode(void) const { return mInode; }

  void setLazyRemoval(bool remove);
  bool lazyRemoval(void) const { return mLazyRemoval; }

  std::string getChunkPath(off_t offset) const;

  size_t chunkSize(void) const { return mChunkSize; }

  ssize_t getLastChunkIndexAndSize(uint64_t *size) const;

  ssize_t getLastChunkIndex(void) const;

  size_t getSize(void) const;

  int remove(void);

  int truncate(size_t newSize);

  void lockShared(const std::string &uuid);

  void lockExclusive(const std::string &uuid);

  int unlockShared(void);

  int unlockExclusive(void);

  int unlock(void);

  void manageIdleLock(double idleTimeout);

  static bool hasSingleClient(const FileIOSP &io);

  int sync(const std::string &opId) { return mOpManager.sync(opId); }

  PoolSP pool(void) const { return mPool; }

  void setInlineBuffer(const Stat *parentStat, const std::string path,
                       size_t bufferSize);

  FileInlineBuffer *inlineBuffer(void) const { return mInlineBuffer.get(); }

  void setHasBackLink(bool hasBacklink);

  bool hasBackLink(void);

  bool shouldSetBacklink(void) { return !hasBackLink() && !mPath.empty(); }

  void setPath(const std::string &path);

  void updateBackLink(const std::string *oldBackLink=0);

  bool hasRunningAsyncOps(void);

private:
  Filesystem *mRadosFs;
  const PoolSP mPool;
  const std::string mInode;
  std::string mPath;
  size_t mChunkSize;
  bool mLazyRemoval;
  std::vector<rados_completion_t> mCompletionList;
  boost::chrono::system_clock::time_point mLockStart;
  boost::chrono::system_clock::time_point mLockUpdated;
  boost::mutex mLockMutex;
  std::string mLocker;
  OpsManager mOpManager;
  boost::scoped_ptr<FileInlineBuffer> mInlineBuffer;
  std::string mInlineMemBuffer;
  boost::mutex mInlineMemBufferMutex;
  bool mHasBackLink;
  boost::mutex mHasBackLinkMutex;

  int verifyWriteParams(off_t offset, size_t length);
  int realWrite(char *buff, off_t offset, size_t blen, bool deleteBuffer,
                AsyncOpSP asyncOp);
  int setSizeIfBigger(size_t size, AsyncOpSP asyncOp);
  int setSize(size_t size);
  void setCompletionDebugMsg(librados::AioCompletion *completion,
                             const std::string &message);
  void syncAndResetLocker(AsyncOpSP op);
  void getInlineAndInodeReadData(const std::vector<FileReadData> &intervals,
                                 std::vector<FileReadDataImpSP> *dataInline,
                                 std::vector<FileReadDataImpSP> *dataInode);
  void getReadDataPerChunk(const std::vector<FileReadDataImpSP> &intervals,
                  std::map<size_t, std::vector<FileReadDataImpSP> > *inodeData);
  static void onReadCompleted(rados_completion_t comp, void *arg);
  static void onReadInlineBufferCompleted(rados_completion_t comp, void *arg);
  void separateReadData(const FileReadDataImpSP &readData,
                        FileReadDataImpSP &inlineData,
                        FileReadDataImpSP &inodeData) const;
  void vectorReadInlineBuffer(const std::vector<FileReadDataImpSP> &readData,
                              boost::shared_ptr<boost::shared_mutex> readOpMutex,
                              AsyncOpSP asyncOp,
                             boost::shared_ptr<ssize_t> inodeSize);
  void vectorReadChunk(size_t fileChunk,
                       const std::vector<FileReadDataImpSP> &readDataVector,
                       boost::shared_ptr<boost::shared_mutex> readOpMutex,
                       AsyncOpSP asyncOp,
                       boost::shared_ptr<ssize_t> inodeSize);
  void setAlignedChunkWriteOp(librados::ObjectWriteOperation &op,
                              const std::string &fileChunk,
                              const size_t offset,
                              const std::string &newContents);
  void unlockIfTimeIsOut(double idleTimeout);
};

RADOS_FS_END_NAMESPACE

#endif /* RADOS_FS_FILE_IO_HH */
