#if !defined CU_WRAPPER_H
#define CU_WRAPPER_H

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>


namespace cu {
  class Error : public std::exception {
    public:
      Error(CUresult result)
      :
        _result(result)
      {
      }

      virtual const char *what() const noexcept;

      operator CUresult () const
      {
	return _result;
      }

    private:
      CUresult _result;
  };


  inline void checkCudaCall(CUresult result)
  {
    if (result != CUDA_SUCCESS)
      throw Error(result);
  }


  inline void init(unsigned flags = 0)
  {
    checkCudaCall(cuInit(flags));
  }


  inline int driverGetVersion()
  {
    int version;
    checkCudaCall(cuDriverGetVersion(&version));
    return version;
  }

  inline void memcpyHtoD(CUdeviceptr dst, const void *src, size_t size)
  {
    checkCudaCall(cuMemcpyHtoD(dst, src, size));
  }

  class Context;
  class Stream;

  template <typename T> class Wrapper
  {
    public:
      // conversion to C-style T

      operator T () const
      {
	return _obj;
      }

      operator T ()
      {
	return _obj;
      }

      bool operator == (const Wrapper<T> &other)
      {
	return _obj == other._obj;
      }

      bool operator != (const Wrapper<T> &other)
      {
	return _obj != other._obj;
      }

    protected:
      Wrapper<T>()
      {
      }

      Wrapper<T>(const Wrapper<T> &other)
      :
	_obj(other._obj),
	manager(other.manager)
      {
      }

      Wrapper<T>(Wrapper<T> &&other)
      :
	_obj(other._obj),
	manager(std::move(other.manager))
      {
	other._obj = 0;
      }

      Wrapper<T>(T &obj)
      :
        _obj(obj)
      {
      }

      T _obj;
      std::shared_ptr<T> manager;
  };
  
  class Device : public Wrapper<CUdevice>
  {
    public:
      // Device Management

      Device(int ordinal)
      {
	checkCudaCall(cuDeviceGet(&_obj, ordinal));
      }

      int getAttribute(CUdevice_attribute attribute) const
      {
	int value;
	checkCudaCall(cuDeviceGetAttribute(&value, attribute, _obj));
	return value;
      }

      template <CUdevice_attribute attribute> int getAttribute() const
      {
	return getAttribute(attribute);
      }

      static int getCount()
      {
	int nrDevices;
	checkCudaCall(cuDeviceGetCount(&nrDevices));
	return nrDevices;
      }

      std::string getName() const
      {
	char name[64];
	checkCudaCall(cuDeviceGetName(name, sizeof name, _obj));
	return std::string(name);
      }

      size_t totalMem() const
      {
	size_t size;
	checkCudaCall(cuDeviceTotalMem(&size, _obj));
	return size;
      }


      // Primary Context Management

      std::pair<unsigned, bool> primaryCtxGetState() const
      {
	unsigned flags;
	int active;
	checkCudaCall(cuDevicePrimaryCtxGetState(_obj, &flags, &active));
	return std::pair<unsigned, bool>(flags, active);
      }

      // void primaryCtxRelease() not available; it is released on destruction of the Context returned by Device::primaryContextRetain()

      void primaryCtxReset()
      {
	checkCudaCall(cuDevicePrimaryCtxReset(_obj));
      }

      Context primaryCtxRetain(); // retain this context until the primary context can be released

      void primaryCtxSetFlags(unsigned flags)
      {
	checkCudaCall(cuDevicePrimaryCtxSetFlags(_obj, flags));
      }
  };


  class Context : public Wrapper<CUcontext>
  {
    public:
      // Context Management

      Context(int flags, Device &device)
      :
        _primaryContext(false)
      {
	checkCudaCall(cuCtxCreate(&_obj, flags, device));
	manager = std::shared_ptr<CUcontext>(new CUcontext(_obj), [] (CUcontext *ptr) { if (*ptr) cuCtxDestroy(*ptr); delete ptr; });
      }

      Context(CUcontext context)
      :
	Wrapper<CUcontext>(context),
	_primaryContext(false)
      {
      }

      unsigned getApiVersion() const
      {
	unsigned version;
	checkCudaCall(cuCtxGetApiVersion(_obj, &version));
	return version;
      }

      static CUfunc_cache getCacheConfig()
      {
	CUfunc_cache config;
	checkCudaCall(cuCtxGetCacheConfig(&config));
	return config;
      }

      static void setCacheConfig(CUfunc_cache config)
      {
	checkCudaCall(cuCtxSetCacheConfig(config));
      }

      static Context getCurrent()
      {
	CUcontext context;
	checkCudaCall(cuCtxGetCurrent(&context));
	return std::move(Context(context));
      }

      void setCurrent() const
      {
	checkCudaCall(cuCtxSetCurrent(_obj));
      }

      void pushCurrent()
      {
	checkCudaCall(cuCtxPushCurrent(_obj));
      }

      static Context popCurrent()
      {
	CUcontext context;
	checkCudaCall(cuCtxPopCurrent(&context));
	return Context(context);
      }

      void setSharedMemConfig(CUsharedconfig config)
      {
	checkCudaCall(cuCtxSetSharedMemConfig(config));
      }

      static Device getDevice()
      {
	CUdevice device;
	checkCudaCall(cuCtxGetDevice(&device));
	return Device(device); // FIXME: ~Device()
      }

      static size_t getLimit(CUlimit limit)
      {
	size_t value;
	checkCudaCall(cuCtxGetLimit(&value, limit));
	return value;
      }

      template <CUlimit limit> static size_t getLimit()
      {
	return getLimit(limit);
      }

      static void setLimit(CUlimit limit, size_t value)
      {
	checkCudaCall(cuCtxSetLimit(limit, value));
      }

      template <CUlimit limit> static void setLimit(size_t value)
      {
	setLimit(limit, value);
      }

      static void synchronize()
      {
	checkCudaCall(cuCtxSynchronize());
      }

    private:
      friend class Device;
      Context(CUcontext context, Device &device)
      :
	Wrapper<CUcontext>(context),
	_primaryContext(true)
      {
      }

      bool _primaryContext;
  };


  class HostMemory : public Wrapper<void *>
  {
    public:
      HostMemory(size_t size, int flags = 0)
      {
	checkCudaCall(cuMemHostAlloc(&_obj, size, flags));
	manager = std::shared_ptr<void *>(new (void *)(_obj), [] (void **ptr) { cuMemFreeHost(*ptr); delete ptr; });
      }

      template <typename T> operator T * ()
      {
	return static_cast<T *>(_obj);
      }
  };


  class DeviceMemory : public Wrapper<CUdeviceptr>
  {
    public:
      DeviceMemory(size_t size)
      {
	checkCudaCall(cuMemAlloc(&_obj, size));
	manager = std::shared_ptr<CUdeviceptr>(new CUdeviceptr(_obj), [] (CUdeviceptr *ptr) { cuMemFree(*ptr); delete ptr; });
      }

      DeviceMemory(CUdeviceptr ptr)
      :
        Wrapper(ptr)
      {
      }

      DeviceMemory(const HostMemory &hostMemory)
      {
	checkCudaCall(cuMemHostGetDevicePointer(&_obj, hostMemory, 0));
      }

      const void *parameter() const // used to construct parameter list for launchKernel();
      {
	return &_obj;
      }
  };


  class Array : public Wrapper<CUarray>
  {
    public:
      Array(unsigned width, CUarray_format format, unsigned numChannels)
      {
	Array(width, 0, format, numChannels);
	manager = std::shared_ptr<CUarray>(new CUarray(_obj), [] (CUarray *ptr) { cuArrayDestroy(*ptr); delete ptr; });
      }

      Array(unsigned width, unsigned height, CUarray_format format, unsigned numChannels)
      {
	CUDA_ARRAY_DESCRIPTOR descriptor;
	descriptor.Width       = width;
	descriptor.Height      = height;
	descriptor.Format      = format;
	descriptor.NumChannels = numChannels;
	checkCudaCall(cuArrayCreate(&_obj, &descriptor));
	manager = std::shared_ptr<CUarray>(new CUarray(_obj), [] (CUarray *ptr) { cuArrayDestroy(*ptr); delete ptr; });
      }

      Array(unsigned width, unsigned height, unsigned depth, CUarray_format format, unsigned numChannels)
      {
	CUDA_ARRAY3D_DESCRIPTOR descriptor;
	descriptor.Width       = width;
	descriptor.Height      = height;
	descriptor.Depth       = depth;
	descriptor.Format      = format;
	descriptor.NumChannels = numChannels;
	descriptor.Flags       = 0;
	checkCudaCall(cuArray3DCreate(&_obj, &descriptor));
	manager = std::shared_ptr<CUarray>(new CUarray(_obj), [] (CUarray *ptr) { cuArrayDestroy(*ptr); delete ptr; });
      }

      Array(CUarray &array)
      :
        Wrapper(array)
      {
      }
  };


  class Source
  {
    public:
      Source(const char *input_file_name)
      :
        input_file_name(input_file_name)
      {
      }

      void compile(const char *ptx_name, const char *compile_options = 0);

    private:
      const char *input_file_name;
  };


  class Module : public Wrapper<CUmodule>
  {
    public:
      Module(const char *file_name)
      {
#if defined TEGRA_QUIRKS // cuModuleLoad broken on Jetson TX1
	std::ifstream file(file_name);
	std::string program((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	checkCudaCall(cuModuleLoadData(&_obj, program.c_str()));
#else
	checkCudaCall(cuModuleLoad(&_obj, file_name));
#endif
	manager = std::shared_ptr<CUmodule>(new CUmodule(_obj), [] (CUmodule *ptr) { cuModuleUnload(*ptr); delete ptr; });
      }

      Module(const void *data)
      {
	checkCudaCall(cuModuleLoadData(&_obj, data));
	manager = std::shared_ptr<CUmodule>(new CUmodule(_obj), [] (CUmodule *ptr) { cuModuleUnload(*ptr); delete ptr; });
      }

      Module(CUmodule &module)
      :
        Wrapper(module)
      {
      }

#if 0
      TexRef getTexRef(const char *name) const
      {
	CUtexref texref;
	checkCudaCall(cuModuleGetTexRef(&texref, _obj, name));
	return TexRef(texref);
      }
#endif

      CUdeviceptr getGlobal(const char *name) const
      {
	CUdeviceptr deviceptr;
	checkCudaCall(cuModuleGetGlobal(&deviceptr, nullptr, _obj, name));
	return deviceptr;
      }
  };


  class Function : public Wrapper<CUfunction>
  {
    public:
      Function(const Module &module, const char *name)
      {
	checkCudaCall(cuModuleGetFunction(&_obj, module, name));
      }

      Function(CUfunction &function)
      :
	Wrapper(function)
      {
      }

      int getAttribute(CUfunction_attribute attribute)
      {
	int value;
	checkCudaCall(cuFuncGetAttribute(&value, attribute, _obj));
	return value;
      }

      void setCacheConfig(CUfunc_cache config)
      {
	checkCudaCall(cuFuncSetCacheConfig(_obj, config));
      }
  };


  class Event : public Wrapper<CUevent>
  {
    public:
      Event(int flags = CU_EVENT_DEFAULT)
      {
	checkCudaCall(cuEventCreate(&_obj, flags));
	manager = std::shared_ptr<CUevent>(new CUevent(_obj), [] (CUevent *ptr) { cuEventDestroy(*ptr); delete ptr; });
      }

      Event(CUevent &event)
      :
	Wrapper(event)
      {
      }

      float elapsedTime(const Event &start) const
      {
	float ms;
	checkCudaCall(cuEventElapsedTime(&ms, start, _obj));
	return ms;
      }

      void query() const
      {
	checkCudaCall(cuEventQuery(_obj)); // unsuccessful result throws cu::Error
      }

      void record()
      {
	checkCudaCall(cuEventRecord(_obj, 0));
      }

      void record(Stream &);

      void synchronize()
      {
	checkCudaCall(cuEventSynchronize(_obj));
      }
  };


  class Stream : public Wrapper<CUstream>
  {
    friend class Event;

    public:
      Stream(int flags = CU_STREAM_DEFAULT)
      {
	checkCudaCall(cuStreamCreate(&_obj, flags));
	manager = std::shared_ptr<CUstream>(new CUstream(_obj), [] (CUstream *ptr) { cuStreamDestroy(*ptr); delete ptr; });
      }

      Stream(CUstream stream)
      :
	Wrapper<CUstream>(stream)
      {
      }

      void memcpyHtoDAsync(CUdeviceptr devPtr, const void *hostPtr, size_t size)
      {
	checkCudaCall(cuMemcpyHtoDAsync(devPtr, hostPtr, size, _obj));
      }

      void memcpyDtoHAsync(void *hostPtr, CUdeviceptr devPtr, size_t size)
      {
	checkCudaCall(cuMemcpyDtoHAsync(hostPtr, devPtr, size, _obj));
      }

      void launchKernel(Function &function, unsigned gridX, unsigned gridY, unsigned gridZ, unsigned blockX, unsigned blockY, unsigned blockZ, unsigned sharedMemBytes, const std::vector<const void *> &parameters)
      {
	checkCudaCall(cuLaunchKernel(function, gridX, gridY, gridZ, blockX, blockY, blockZ, sharedMemBytes, _obj, const_cast<void **>(&parameters[0]), 0));
      }

#if CUDART_VERSION >= 9000
      void launchCooperativeKernel(Function &function, unsigned gridX, unsigned gridY, unsigned gridZ, unsigned blockX, unsigned blockY, unsigned blockZ, unsigned sharedMemBytes, const std::vector<const void *> &parameters)
      {
	checkCudaCall(cuLaunchCooperativeKernel(function, gridX, gridY, gridZ, blockX, blockY, blockZ, sharedMemBytes, _obj, const_cast<void **>(&parameters[0])));
      }
#endif

      void query()
      {
	checkCudaCall(cuStreamQuery(_obj)); // unsuccessful result throws cu::Error
      }

      void synchronize()
      {
	checkCudaCall(cuStreamSynchronize(_obj));
      }

      void wait(Event &event)
      {
	checkCudaCall(cuStreamWaitEvent(_obj, event, 0));
      }

      void addCallback(CUstreamCallback callback, void *userData, int flags = 0)
      {
	checkCudaCall(cuStreamAddCallback(_obj, callback, userData, flags));
      }

      void record(Event &event)
      {
	checkCudaCall(cuEventRecord(event, _obj));
      }

      void batchMemOp(unsigned count, CUstreamBatchMemOpParams *paramArray, unsigned flags)
      {
	checkCudaCall(cuStreamBatchMemOp(_obj, count, paramArray, flags));
      }

      void waitValue32(CUdeviceptr addr, cuuint32_t value, unsigned flags) const
      {
	checkCudaCall(cuStreamWaitValue32(_obj, addr, value, flags));
      }

      void writeValue32(CUdeviceptr addr, cuuint32_t value, unsigned flags)
      {
	checkCudaCall(cuStreamWriteValue32(_obj, addr, value, flags));
      }
  };

#if 0
  class Graph : public Wrapper<CUgraph>
  {
    public:
      class GraphNode : public Wrapper<CUgraphNode>
      {
      };

      class ExecKernelNode : public GraphNode
      {
      };

      class KernelNodeParams : public Wrapper<CUDA_KERNEL_NODE_PARAMS>
      {
	public:
	  KernelNodeParams(const Function &function,
			   unsigned gridDimX, unsigned gridDimY, unsigned gridDimZ,
			   unsigned blockDimX, unsigned blockDimY, unsigned blockDimZ,
			   unsigned sharedMemBytes,
			   const std::vector<const void *> &kernelParams)
	  {
	    _obj.func	   = function;
	    _obj.blockDimX = blockDimX;
	    _obj.blockDimY = blockDimY;
	    _obj.blockDimZ = blockDimZ;
	    _obj.gridDimX  = gridDimX;
	    _obj.gridDimY  = gridDimY;
	    _obj.gridDimZ  = gridDimZ;
	    _obj.sharedMemBytes = sharedMemBytes;
	    _obj.kernelParams = const_cast<void **>(kernelParams.data());
	    _obj.extra	   = nullptr;
	  }
      };

      class Exec : public Wrapper<CUgraphExec>
      {
	public:
	  void launch(Stream &stream)
	  {
	    checkCudaCall(cuGraphLaunch(_obj, stream));
	  }
      };

      Graph(unsigned flags = 0)
      {
	checkCudaCall(cuGraphCreate(&_obj, flags));
	manager = std::shared_ptr<CUgraphNode>(new CUgraphNode(_obj), [] (CUgraphNode *ptr) { cuGraphDestroy(*ptr); delete ptr; });
      }

      Graph(CUgraph &graph)
      :
	Wrapper(graph)
      {
      }

      ExecKernelNode addKernelNode(/* std::vector<GraphNode> dependencies, */ const KernelNodeParams &kernelArgs)
      {
	ExecKernelNode node;
	checkCudaCall(cuGraphAddKernelNode(& (CUgraphNode &) node, _obj, nullptr, 0, & (const CUDA_KERNEL_NODE_PARAMS &) kernelArgs));
	return node;
      }

      Exec instantiate()
      {
	Exec exec;
	checkCudaCall(cuGraphInstantiate(& (CUgraphExec &) exec, _obj, nullptr, nullptr, 0));
	return exec;
      }
  };
#endif


  inline void Event::record(Stream &stream)
  {
    checkCudaCall(cuEventRecord(_obj, stream._obj));
  }
}

#endif
