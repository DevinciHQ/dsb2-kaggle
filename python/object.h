#ifndef PYTHON_OBJECT_H_
#define PYTHON_OBJECT_H_ 1

#include <utility>

#include <Python.h>

namespace ev {

class ScopedPyObject {
 public:
  ScopedPyObject() {}

  ScopedPyObject(PyObject* object) : object_(object) {}

  ScopedPyObject(ScopedPyObject&) = delete;

  ScopedPyObject(ScopedPyObject&& rhs) { std::swap(object_, rhs.object_); }

  ~ScopedPyObject() {
    if (object_) Py_DECREF(object_);
  }

  explicit operator bool() const {
    return object_ != nullptr;
  }

  ScopedPyObject& operator=(ScopedPyObject&) = delete;

  ScopedPyObject& operator=(ScopedPyObject&& rhs) {
    if (object_) {
      Py_DECREF(object_);
      object_ = nullptr;
    }

    std::swap(object_, rhs.object_);

    return *this;
  }

  PyObject* get() { return object_; }

  PyObject* release() {
    auto result = object_;
    object_ = nullptr;
    return result;
  }

 private:
  PyObject* object_ = nullptr;
};

}  // namespace ev

#endif  // !PYTHON_OBJECT_H_
