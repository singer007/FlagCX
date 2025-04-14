import os
import sys
from setuptools import setup
from torch.utils import cpp_extension

adaptor_flag = "-DUSE_NVIDIA_ADAPTOR"
if '--adaptor' in sys.argv:
    arg_index = sys.argv.index('--adaptor')
    sys.argv.remove("--adaptor")
    if arg_index < len(sys.argv):
        assert sys.argv[arg_index] in ["nvidia", "iluvatar_corex", "cambricon", "enflame"], f"Invalid adaptor: {adaptor_flag}"
        print(f"Using {sys.argv[arg_index]} adaptor")
        if sys.argv[arg_index] == "iluvatar_corex":
            adaptor_flag = "-DUSE_ILUVATAR_COREX_ADAPTOR"
        elif sys.argv[arg_index] == "cambricon":
            adaptor_flag = "-DUSE_CAMBRICON_ADAPTOR"
        elif sys.argv[arg_index] == "enflame":
            adaptor_flag = "-DUSE_ENFLAME_ADAPTOR"
    else:
        print("No adaptor provided after '--adaptor'. Using default nvidia adaptor")
    sys.argv.remove(sys.argv[arg_index])

sources = ["src/backend_flagcx.cpp"]
include_dirs = [
    f"{os.path.dirname(os.path.abspath(__file__))}/include",
    f"{os.path.dirname(os.path.abspath(__file__))}/../../flagcx/include",
]

library_dirs = [
    f"{os.path.dirname(os.path.abspath(__file__))}/../../build/lib",
]

libs = ["flagcx"]

if adaptor_flag == "-DUSE_NVIDIA_ADAPTOR":
    include_dirs += ["/usr/local/cuda/include"]
    library_dirs += ["/usr/local/cuda/lib64"]
    libs += ["cuda", "cudart", "c10_cuda", "torch_cuda"]
elif adaptor_flag == "-DUSE_ILUVATAR_COREX_ADAPTOR":
    include_dirs += ["/usr/local/corex/include"]
    library_dirs += ["/usr/local/corex/lib64"]
    libs += ["cuda", "cudart", "c10_cuda", "torch_cuda"]
elif adaptor_flag == "-DUSE_CAMBRICON_ADAPTOR":
    import torch_mlu
    neuware_home_path=os.getenv("NEUWARE_HOME")
    pytorch_home_path=os.getenv("PYTORCH_HOME")
    torch_mlu_home = pytorch_home_path.split("pytorch")[0]+"torch_mlu"
    torch_mlu_include_dir = os.path.join(torch_mlu_home, "torch_mlu/csrc")
    torch_mlu_path = torch_mlu.__file__.split("__init__")[0]
    torch_mlu_lib_dir = os.path.join(torch_mlu_path, "csrc/lib/")
    include_dirs += [f"{neuware_home_path}/include", torch_mlu_include_dir]
    library_dirs += [f"{neuware_home_path}/lib64", torch_mlu_lib_dir]
    libs += ["cnrt", "cncl", "torch_mlu"]
elif adaptor_flag == "-DUSE_ENFLAME_ADAPTOR":
    import torch_gcu
    torch_gcu_home = torch_gcu.__file__.split("__init__")[0]
    torch_gcu_include_dir = os.path.join(torch_gcu_home, "include")
    torch_gcu_lib_dir = os.path.join(torch_gcu_home, "lib")
    include_dirs += ["/opt/tops/include", torch_gcu_include_dir]
    library_dirs += ["/opt/tops/lib", torch_gcu_lib_dir]
    libs += ["topsrt", "torch_gcu"]

module = cpp_extension.CppExtension(
    name='flagcx',
    sources=sources,
    include_dirs=include_dirs,
    extra_compile_args={
        'cxx': [adaptor_flag]
    },
    extra_link_args=["-Wl,-rpath,"+f"{os.path.dirname(os.path.abspath(__file__))}/../../build/lib"],
    library_dirs=library_dirs,
    libraries=libs,
)

setup(
    name="flagcx",
    version="0.1.0",
    ext_modules=[module],
    cmdclass={'build_ext': cpp_extension.BuildExtension}
)