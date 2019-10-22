cmake_minimum_required(VERSION 2.8.12)
project(mlir_bindings)

add_subdirectory(pybind11)
pybind11_add_module(mlir_bindings pybind.cpp)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS} ../../include ../../../../../build/projects/mlir/include)
add_definitions(${LLVM_DEFINITIONS} -O3 -fPIC)
message(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs support core bitreader)

target_link_libraries(mlir_bindings PRIVATE ${llvm_libs}

#MLIRAffineOps
MLIREDSC
MLIRExecutionEngine
MLIRIR
MLIRJitRunner
#MLIRLoopOps
#MLIRSDBM
MLIRSupport
)

#python setup.py bdist_wheel
#pip install dist/mlir_bindings-0.0.1-cp37-cp37m-linux_x86_64.whl
