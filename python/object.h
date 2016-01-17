#ifndef PYTHON_OBJECT_H_
#define PYTHON_OBJECT_H_ 1

#include <utility>

#include <Python.h>

namespace ev_python {

class ScopedObject {
 public:
  ScopedObject() {}

  ScopedObject(PyObject* object) : object_(object) {}

  ScopedObject(ScopedObject&) = delete;

  ScopedObject(ScopedObject&& rhs) { std::swap(object_, rhs.object_); }

  ~ScopedObject() {
    if (object_) Py_DECREF(object_);
  }

  explicit operator bool() const { return object_ != nullptr; }

  bool operator==(std::nullptr_t) const { return object_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return object_ != nullptr; }

  ScopedObject& operator=(ScopedObject&) = delete;

  ScopedObject& operator=(ScopedObject&& rhs) {
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
