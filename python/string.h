#ifndef PYTHON_STRING_H_
#define PYTHON_STRING_H_ 1

#include <string>

#include <Python.h>

namespace ev_python {

inline std::string GetString(PyObject* value) {
  if (PyUnicode_Check(value)) {
    ev_python::ScopedObject bytes(
        PyUnicode_AsEncodedString(value, "ASCII", "strict"));
    KJ_REQUIRE(bytes != nullptr);
    return std::string(PyBytes_AS_STRING(bytes.get()),
                       PyBytes_GET_SIZE(bytes.get()));
  } else if (PyBytes_Check(value)) {
    return std::string(PyBytes_AS_STRING(value), PyBytes_GET_SIZE(value));
  } else {
    KJ_FAIL_REQUIRE("Expected string");
  }
}

}  // namespace ev_python

#endif  // !PYTHON_STRING_H_
