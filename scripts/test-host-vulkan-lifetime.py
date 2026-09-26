#!/usr/bin/env python3
"""Test actual guard against loader failure, concurrent probes and unload order."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-vulkan-lifetime-') as tmp:
    p = Path(tmp)
    (p/'loader.cpp').write_text(r'''
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
void note(const char* event) { FILE* f=fopen(getenv("GUARD_TRACE"),"a"); if(!f) abort(); fprintf(f,"%s\n",event); fclose(f); }
__attribute__((constructor)) void loaded() { note("load"); }
__attribute__((destructor)) void unloaded() { note("unload"); }
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* i,const VkAllocationCallbacks*,VkInstance* out) {
 note("create"); if(i->enabledLayerCount || i->enabledExtensionCount || i->pApplicationInfo->apiVersion != VK_API_VERSION_1_0) abort();
 if(getenv("GUARD_FAIL")) return VK_ERROR_INITIALIZATION_FAILED;
 *out=reinterpret_cast<VkInstance>(1); return VK_SUCCESS;
}
#ifndef MISSING_DESTROY
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance i,const VkAllocationCallbacks*) {
 if(i != reinterpret_cast<VkInstance>(1)) abort(); note("destroy");
}
#endif
''')
    (p/'test.cpp').write_text(r'''
#include "host/linux/vulkan-driver-lifetime.h"
#include <thread>
#include <vector>
#include <cstdlib>
int main(int argc,char**) {
 const bool expected=argc==1;
 std::vector<std::thread> threads;
 for(int i=0;i<24;i++)threads.emplace_back([expected]{for(int j=0;j<100;j++)if(deskport::vulkan::keep_drivers_loaded()!=expected)abort();});
 for(auto& t:threads)t.join();
}
''')
    cxx = os.environ.get('CXX', 'c++')
    subprocess.run([cxx,'-std=c++17','-g','-fsanitize=address,undefined','-pthread','-I'+str(root),str(p/'test.cpp'),'-ldl','-o',str(p/'test')],check=True)
    for mode, expected in [('success',['load','create','destroy','unload']), ('failure',['load','create','unload']), ('missing-symbol',['load','unload'])]:
        args=[cxx,'-shared','-fPIC',str(p/'loader.cpp'),'-o',str(p/'libvulkan.so.1')]
        if mode=='missing-symbol': args.append('-DMISSING_DESTROY')
        subprocess.run(args,check=True)
        trace=p/(mode+'.log')
        env=dict(os.environ,LD_LIBRARY_PATH=str(p),GUARD_TRACE=str(trace))
        if mode=='failure': env['GUARD_FAIL']='1'
        else: env.pop('GUARD_FAIL',None)
        subprocess.run([str(p/'test')]+([] if mode=='success' else ['expect-failure']),env=env,check=True,timeout=30)
        assert trace.read_text().splitlines()==expected,(mode,trace.read_text())
print('Vulkan lifetime: concurrent probes, initialization failure, missing symbol and instance-before-loader teardown passed (ASan/UBSan).')
