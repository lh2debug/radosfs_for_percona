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

#include <boost/thread.hpp>
#include <cstdio>
#include <errno.h>
#include <limits>
#include <sstream>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

#include "BenchmarkMgr.hh"
#include "radosfscommon.h"

#define CONF_ENV_VAR "RADOSFS_BENCHMARK_CLUSTER_CONF"
#define CLUSTER_CONF_ARG "conf"
#define DEFAULT_NUM_THREADS 10
#define LINES_PER_HEADER 30
#define CREATE_IN_DIR_CONF_ARG "create-in-dir"
#define CREATE_IN_DIR_CONF_ARG_CHAR 'd'
#define BUFFER_SIZE_ARG "buffer-size"
#define BUFFER_SIZE_ARG_CHAR 's'
#define BUFFER_DIVISION_ARG "num-times"
#define BUFFER_DIVISION_CHAR 'n'
#define USER_ARG "user"
#define USER_ARG_CHAR 'u'
#define POOLS_CONF_ARG "pools"
#define POOLS_CONF_ARG_CHAR 'p'
#define DELETE_OBJS_ARG "delete-objects"
#define DELETE_OBJS_ARG_CHAR 'E'

static std::string commonPrefix;

typedef struct
{
  int threadId;
  BenchmarkMgr *benchmark;
  char *buffer;
  size_t bufferSize;
  size_t bufferDivision;
  std::vector<int> creationTimes;
  float minCreationTime;
  float maxCreationTime;
  bool shouldExit;
  bool exited;
} BenchmarkInfo;

void
createFiles(BenchmarkInfo *benchmarkInfo)
{
  int threadId = benchmarkInfo->threadId;
  BenchmarkMgr *benchmark = benchmarkInfo->benchmark;
  benchmarkInfo->minCreationTime = std::numeric_limits<float>::max();
  benchmarkInfo->maxCreationTime = .0;

  std::stringstream prefix;
  prefix << "/t-" << commonPrefix << "-" << threadId;

  if (benchmark->createInDir())
  {
    prefix << "/";

    radosfs::Dir dir(&benchmark->radosFs, prefix.str());
    int ret = dir.create();

    if (ret != 0)
    {
      fprintf(stderr, "\nProblem creating directory %s: %s ... "
              "Exiting thread %d\n",
              prefix.str().c_str(), strerror(ret), threadId);

      goto exitThread;
    }
  }
  else
  {
    prefix << "-";
  }

  for (int i = 0; !benchmarkInfo->shouldExit; i++)
  {
    std::stringstream stream;
    stream << prefix.str() << i;

    struct timespec timeBefore, timeAfter;

    clock_gettime(CLOCK_REALTIME, &timeBefore);

    radosfs::File file(&benchmark->radosFs,
                       stream.str(),
                       radosfs::File::MODE_WRITE);

    int ret = file.create();

    if (benchmarkInfo->buffer)
    {
      off_t offset = 0;
      const size_t bufferSize = benchmarkInfo->bufferSize;
      const size_t slice = bufferSize / benchmarkInfo->bufferDivision;

      for (offset = 0; offset + slice <= bufferSize; offset += slice)
      {
        file.write(benchmarkInfo->buffer, offset, slice);
      }

      file.sync();
    }

    clock_gettime(CLOCK_REALTIME, &timeAfter);

    if (ret == 0)
      benchmark->incFiles();
    else
    {
      fprintf(stderr, "Problem in thread %d: %s\n", threadId, strerror(ret));
      continue;
    }

    float diffTime = (float) (timeAfter.tv_sec - timeBefore.tv_sec);

    if (diffTime < benchmarkInfo->minCreationTime)
      benchmarkInfo->minCreationTime = diffTime;

    if (diffTime > benchmarkInfo->maxCreationTime)
      benchmarkInfo->maxCreationTime = diffTime;

  }

exitThread:

  benchmarkInfo->exited = true;
}

static void
showUsage(const char *name)
{
  fprintf(stderr, "Usage:\n%s DURATION [NUM_THREADS] [--%s=CLUSTER_CONF] "
          "[--%s=USER_NAME] [--%s] [--%s=SIZE [--%s=NUM]]\n"
          "\tDURATION     - duration of the benchmark in seconds "
          "(has to be > 0)\n"
          "\tNUM_THREADS  - number of concurrent threads\n"
          "\t--%s, -%c - path to the cluster's configuration file\n"
          "\t--%s, -%c - the user name to connect to the Ceph cluster\n"
          "\t--%s, -%c - make each thread work inside its own directory "
          "instead of /\n"
          "\t--%s, -%c - buffer size to be written into each file\n"
          "\t--%s, -%c - the number of writes it should take to write the buffer\n",
          name,
          CLUSTER_CONF_ARG,
          USER_ARG,
          CREATE_IN_DIR_CONF_ARG,
          BUFFER_SIZE_ARG,
          BUFFER_DIVISION_ARG,
          CLUSTER_CONF_ARG,
          CLUSTER_CONF_ARG[0],
          USER_ARG,
          USER_ARG_CHAR,
          CREATE_IN_DIR_CONF_ARG,
          CREATE_IN_DIR_CONF_ARG_CHAR,
          BUFFER_SIZE_ARG,
          BUFFER_SIZE_ARG_CHAR,
          BUFFER_DIVISION_ARG,
          BUFFER_DIVISION_CHAR);
}

static int
parseArguments(int argc, char **argv,
               std::string &confPath,
               std::string &user,
               std::vector<std::string> &pools,
               int *runTime,
               int *numThreads,
               bool *createInDir,
               size_t *bufferSize,
               size_t *bufferDivision,
               bool *deleteObjects)
{
  confPath = "";
  const char *confFromEnv(getenv(CONF_ENV_VAR));
  int workers = -1;
  int duration = 0;
  int bufSize = 0;
  int bufDiv = 1;
  *deleteObjects = false;

  if (confFromEnv != 0)
    confPath = confFromEnv;

  int optionIndex = 0;
  struct option options[] =
  {{CLUSTER_CONF_ARG, required_argument, 0, CLUSTER_CONF_ARG[0]},
   {USER_ARG, required_argument, 0, USER_ARG_CHAR},
   {CREATE_IN_DIR_CONF_ARG, no_argument, 0, CREATE_IN_DIR_CONF_ARG_CHAR},
   {BUFFER_SIZE_ARG, required_argument, 0, BUFFER_SIZE_ARG_CHAR},
   {BUFFER_DIVISION_ARG, required_argument, 0, BUFFER_DIVISION_CHAR},
   {POOLS_CONF_ARG, required_argument, 0, POOLS_CONF_ARG_CHAR},
   {DELETE_OBJS_ARG, required_argument, 0, DELETE_OBJS_ARG_CHAR},
   {0, 0, 0, 0}
  };

  int c;
  std::string args;

  for (int i = 0; options[i].name != 0; i++)
  {
    args += options[i].val;

    if (options[i].has_arg != no_argument)
      args += ":";
  }

  std::string poolsStr;

  while ((c = getopt_long(argc, argv, args.c_str(), options, &optionIndex)) != -1)
  {
    if (c == CLUSTER_CONF_ARG[0])
      confPath = optarg;
    else if (c == CREATE_IN_DIR_CONF_ARG_CHAR)
      *createInDir = true;
    else if (c == BUFFER_SIZE_ARG_CHAR)
      bufSize = atoi(optarg);
    else if (c == BUFFER_DIVISION_CHAR)
      bufDiv = atoi(optarg);
    else if (c == USER_ARG_CHAR)
      user = optarg;
    else if (c == POOLS_CONF_ARG_CHAR)
      poolsStr = optarg;
    else if (c == DELETE_OBJS_ARG_CHAR)
      *deleteObjects = strcmp(optarg, "yes") == 0;
  }

  if (!poolsStr.empty())
  {
    splitToVector(poolsStr, pools);
    if (pools.size() > 0 && pools.size() != 2)
    {
      fprintf(stderr, "Error parsing pools '%s'. Pools should be passed as: "
                      "MTD_POOL,DATA_POOL\n", poolsStr.c_str());
      exit(-EINVAL);
    }
  }

  if (confPath == "")
  {
    fprintf(stderr, "Error: Please specify the " CONF_ENV_VAR " environment "
            "variable or use the --" CLUSTER_CONF_ARG "=... argument.\n");

    return -1;
  }

  optionIndex = optind;

  if (optionIndex < argc)
    duration = atoi(argv[optionIndex]);

  if (duration <= 0)
  {
    fprintf(stderr, "Error: Please specify the duration of the benchmark\n");
    return -1;
  }

  if (bufSize < 0)
    bufSize = 0;

  if (bufDiv <= 0)
  {
    fprintf(stderr, "Error: The buffer needs to be written a positive number "
            "of times\n");
    return -1;
  }

  optionIndex++;

  if (optionIndex < argc)
    workers = atoi(argv[optionIndex]);

  *runTime = duration;

  if (workers <= 0)
    workers = DEFAULT_NUM_THREADS;

  *numThreads = workers;

  *bufferSize = bufSize;
  *bufferDivision = bufDiv;

  return 0;
}

int
main(int argc, char **argv)
{
  std::string confPath, user;
  int runTime, numThreads;
  bool createInDir, deleteObjects;
  size_t bufferSize, bufferDivision;
  std::vector<std::string> pools;

  runTime = numThreads = 0;
  createInDir = false;
  bufferSize = bufferDivision = 0;

  int ret = parseArguments(argc, argv, confPath, user, pools, &runTime,
                           &numThreads, &createInDir, &bufferSize,
                           &bufferDivision, &deleteObjects);

  if (ret != 0)
  {
    showUsage(argv[0]);
    return ret;
  }

  std::string mtdPool, dataPool;
  bool createPools = false;

  if (pools.size() == 2)
  {
    mtdPool = pools[0];
    dataPool = pools[1];
  }
  else
  {
    mtdPool = TEST_POOL_MTD;
    dataPool = TEST_POOL_DATA;
    createPools = true;
  }

  BenchmarkMgr benchmark(confPath.c_str(), user, mtdPool, dataPool, createPools,
                         bufferSize / 1000);
  benchmark.setupPools();

  fprintf(stdout, "\n*** RadosFs Benchmark ***\n\n"
          "Running on cluster configured by %s "
          "for %d seconds with %d threads %s...\n",
          confPath.c_str(),
          runTime,
          numThreads,
          (createInDir ? "(using their own directory)": "(all writing to / )"));

  benchmark.setCreateInDir(createInDir);
  benchmark.setDeleteObjects(deleteObjects);

  const int hostnameLength = 32;
  char hostname[hostnameLength];
  gethostname(hostname, hostnameLength);

  std::stringstream stream;
  stream << hostname << "-" << getpid();

  commonPrefix = stream.str();

  boost::thread *threads[numThreads];
  BenchmarkInfo *infos[numThreads];
  char *buffer = 0;

  if (bufferSize > 0)
    buffer = new char[bufferSize];

  int i;

  for(i = 0; i < numThreads; i++)
  {
    BenchmarkInfo *info = new BenchmarkInfo;
    info->benchmark = &benchmark;
    info->exited = false;
    info->shouldExit = false;
    info->threadId = i;
    info->buffer = buffer;
    info->bufferSize = bufferSize;
    info->bufferDivision = bufferDivision;

    infos[i] = info;

    threads[i] = new boost::thread(&createFiles, info);
  }

  time_t initialTime, currentTime;
  time(&initialTime);

  int countDown = runTime;
  float avgFilesPerSecond = .0;
  float avgFilesPerThread = .0;
  int currentNumFiles = 0;
  int numFiles = 0;

  while(countDown > 0)
  {
    time(&currentTime);

    if ((currentTime - initialTime) >= 1)
    {
      if (((runTime - countDown) % LINES_PER_HEADER) == 0)
      {
        fprintf(stdout, "\n%4s | %10s | %10s | %10s\n",
                "sec", "# files", "files/sec", "files/thread");
      }

      currentNumFiles = benchmark.numFiles();
      float totalCreated = currentNumFiles - numFiles;
      avgFilesPerSecond += totalCreated;
      avgFilesPerThread += totalCreated / numThreads;
      fprintf(stdout, "%4d | %10d | %10d | %8.2f\n",
              (runTime - countDown + 1),
              currentNumFiles,
              (int) totalCreated,
              totalCreated / numThreads);

      initialTime = currentTime;
      numFiles = currentNumFiles;
      countDown--;
    }
  }

  fprintf(stdout, "\nResult:\n\n");
  fprintf(stdout, "\tNumber of files:      %10d\n", currentNumFiles);
  fprintf(stdout, "\tAverage files/sec:    %10.2f\n", avgFilesPerSecond / runTime);
  fprintf(stdout, "\tAverage files/thread: %10.2f\n", avgFilesPerThread / runTime);

  float minCreationTime = std::numeric_limits<float>::max();
  float maxCreationTime = std::numeric_limits<float>::min();;

  for(i = 0; i < numThreads; i++)
  {
    infos[i]->shouldExit = true;
    threads[i]->join();

    if (infos[i]->minCreationTime < minCreationTime)
      minCreationTime = infos[i]->minCreationTime;

    if (infos[i]->maxCreationTime > maxCreationTime)
      maxCreationTime = infos[i]->maxCreationTime;

    if (ret != 0)
    {
      fprintf(stderr, "ERROR joining thread: %d : %s", ret, strerror(ret));
      return -1;
    }
  }

  fprintf(stdout, "\tMin creation time:    %10.2f sec\n", minCreationTime);
  fprintf(stdout, "\tMax creation time:    %10.2f sec\n", maxCreationTime);

  delete [] buffer;

  return 0;
}
