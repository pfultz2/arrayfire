/*******************************************************
 * Copyright (c) 2015, ArrayFire
 * All rights reserved.
 *
 * This file is distributed under 3-clause BSD license.
 * The complete license agreement can be obtained at:
 * http://arrayfire.com/licenses/BSD-3-Clause
 ********************************************************/

#include "symbol_manager.hpp"
#include <algorithm>
#include <string>

using std::string;
using std::replace;

static const string LIB_AF_BKND_NAME[NUM_BACKENDS] = {"cpu", "cuda", "opencl"};
#if defined(OS_WIN)
static const string LIB_AF_BKND_PREFIX = "af";
static const string LIB_AF_BKND_SUFFIX = ".dll";
#define RTLD_LAZY 0
#else
static const string LIB_AF_BKND_PREFIX = "libaf";
static const string LIB_AF_BKND_SUFFIX = ".so";
#endif

static const string LIB_AF_ENVARS[NUM_ENV_VARS] = {"AF_PATH", "AF_BUILD_PATH"};
static const string LIB_AF_RPATHS[NUM_ENV_VARS] = {"/lib/", "/src/backend/"};
static const bool LIB_AF_RPATH_SUFFIX[NUM_ENV_VARS] = {false, true};

inline string getBkndLibName(const int backend_index)
{
    int i = backend_index >=0 && backend_index<NUM_BACKENDS ? backend_index : 0;
    return LIB_AF_BKND_PREFIX + LIB_AF_BKND_NAME[i] + LIB_AF_BKND_SUFFIX;
}

inline std::string getEnvVar(const std::string &key)
{
#if defined(OS_WIN)
    DWORD bufSize = 32767; // limit according to GetEnvironment Variable documentation
    string retVal;
    retVal.resize(bufSize);
    bufSize = GetEnvironmentVariable(key.c_str(), &retVal[0], bufSize);
    if (!bufSize) {
        return string("");
    } else {
        retVal.resize(bufSize);
        return retVal;
    }
#else
    char * str = getenv(key.c_str());
    return str==NULL ? string("") : string(str);
#endif
}

/*flag parameter is not used on windows platform */
LibHandle openDynLibrary(const int bknd_idx, int flag=RTLD_LAZY)
{
    string bkndName = getBkndLibName(bknd_idx);
#if defined(OS_WIN)
    HMODULE retVal = LoadLibrary(bkndName.c_str());
#else
    LibHandle retVal = dlopen(bkndName.c_str(), flag);
#endif
    // default search path is the colon separated list of
    // paths stored in the environment variable
    // LD_LIBRARY_PATH(Linux/Unix) or PATH(windows)
    // in the event that dlopen returns NULL, search for the lib
    // ub hard coded paths based on the environment variables
    // defined in the constant string array LIB_AF_PATHS
    if (retVal == NULL) {
        for (int i=0; i<NUM_ENV_VARS; ++i) {
            string abs_path = getEnvVar(LIB_AF_ENVARS[i])
                                 + LIB_AF_RPATHS[i]
                                 + (LIB_AF_RPATH_SUFFIX[i] ? LIB_AF_BKND_NAME[bknd_idx]+"/" : "")
                                 + bkndName;
#if defined(OS_WIN)
            replace(abs_path.begin(), abs_path.end(), '/', '\\');
            retVal = LoadLibrary(abs_path.c_str());
#else
            retVal = dlopen(abs_path.c_str(), flag);
#endif
            if (retVal!=NULL) {
                // if the current absolute path based dlopen
                // search is a success, then abandon search
                // and proceed for compute
                break;
            }
        }
    }
    return retVal;
}

void closeDynLibrary(LibHandle handle)
{
#if defined(OS_WIN)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}

AFSymbolManager& AFSymbolManager::getInstance()
{
    static AFSymbolManager symbolManager;
    return symbolManager;
}

AFSymbolManager::AFSymbolManager()
    : activeHandle(NULL), defaultHandle(NULL), numBackends(0)
{
    for(int i=0; i<NUM_BACKENDS; ++i) {
        bkndHandles[i] = openDynLibrary(i);
        if (bkndHandles[i]) {
            activeHandle = bkndHandles[i];
            numBackends++;
        }
    }
    // Keep a copy of default order handle
    // inorder to use it in ::setBackend when
    // the user passes AF_BACKEND_DEFAULT
    defaultHandle = activeHandle;
}

AFSymbolManager::~AFSymbolManager()
{
    for(int i=0; i<NUM_BACKENDS; ++i) {
        if (bkndHandles[i]) {
            closeDynLibrary(bkndHandles[i]);
        }
    }
}

unsigned AFSymbolManager::getBackendCount()
{
    return numBackends;
}

af_err AFSymbolManager::setBackend(af::Backend bknd)
{
    if (bknd==AF_BACKEND_DEFAULT) {
        if (defaultHandle) {
            activeHandle = defaultHandle;
            return AF_SUCCESS;
        } else
            return AF_ERR_LOAD_LIB;
    }
    unsigned idx = bknd - 1;
    if(bkndHandles[idx]) {
        activeHandle = bkndHandles[idx];
        return AF_SUCCESS;
    } else {
        return AF_ERR_LOAD_LIB;
    }
}
