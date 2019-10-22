#include "pybind11/pytypes.h"
#include "pybind11/pybind11.h"
#include <memory>

namespace py = pybind11;

template <typename T>
struct Mutable {
  Mutable() {
    ptr.reset(new T());
  }
  Mutable(T val) {
    ptr.reset(new T(val));
  }
  T value() {
    T* ip = (T*)ptr.get();
    return *ip;
  }
  void set_value(T new_val) {
    T* ip = (T*)ptr.get();
    *ip = new_val;
  }
  static py::buffer_info info(Mutable<T>& i) {
    return py::buffer_info(
            i.ptr.get(),                               /* Pointer to buffer */
            sizeof(T),                          /* Size of one scalar */
            py::format_descriptor<T>::format(), /* Python struct-style format descriptor */
            1,                                      /* Number of dimensions */
            { 1 },                 /* Buffer dimensions */
            { sizeof(T) }             /* Strides (in bytes) for each index */
        );
  } 
  std::shared_ptr<T> ptr;
};