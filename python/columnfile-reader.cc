#include "python/columnfile.h"

#include <memory>

#include "base/columnfile.h"
#include "base/file.h"
#include "python/object.h"

namespace {

class ColumnFileReaderImpl : public ColumnFileReader {
 public:
  ColumnFileReaderImpl(ev::ColumnFileReader reader)
      : reader_(std::move(reader)) {}

  ~ColumnFileReaderImpl() override {}

  bool end() override { return reader_.End(); }

  PyObject* set_column_filter(PyObject* columns) override {
    try {
      ev_python::ScopedObject iterator(PyObject_GetIter(columns));
      if (!iterator) return nullptr;

      std::vector<uint32_t> columns;

      for (;;) {
        ev_python::ScopedObject item(PyIter_Next(iterator.get()));
        if (!item) break;

        if (PyLong_Check(item.get())) {
          auto tmp = PyLong_AsLong(item.get());
          if (tmp == -1 && PyErr_Occurred()) return nullptr;
          columns.emplace_back(tmp);
#if PY_MAJOR_VERSION < 3
        } else if (PyInt_Check(item.get())) {
          auto tmp = PyInt_AsLong(item.get());
          if (tmp == -1 && PyErr_Occurred()) return nullptr;
          columns.emplace_back(tmp);
#endif
        } else {
          KJ_FAIL_REQUIRE("Unexpected type while looking for integer");
        }
      }

      reader_.SetColumnFilter(columns.begin(), columns.end());
    } catch (kj::Exception e) {
      PyErr_Format(PyExc_RuntimeError, "set_column_filter() failed: %s:%d: %s",
                   e.getFile(), e.getLine(), e.getDescription().cStr());
      return nullptr;
    }

    Py_RETURN_NONE;
  }

  PyObject* get_row() override {
    try {
      const auto row = reader_.GetRow();
      if (row.empty()) Py_RETURN_NONE;

      ev_python::ScopedObject result(PyDict_New());

      for (const auto& kv : row) {
        ev_python::ScopedObject key(PyLong_FromLong(kv.first));

        if (kv.second.IsNull()) {
          PyDict_SetItem(result.get(), key.release(), Py_None);
        } else {
          const auto& str = kv.second.StringRef();

          ev_python::ScopedObject value(
              PyBytes_FromStringAndSize(str.data(), str.size()));

          PyDict_SetItem(result.get(), key.get(), value.get());
        }
      }

      return result.release();
    } catch (kj::Exception e) {
      PyErr_Format(PyExc_RuntimeError, "get_row() failed: %s:%d: %s",
                   e.getFile(), e.getLine(), e.getDescription().cStr());

      return nullptr;
    }
  }

  PyObject* size() override {
    try {
      return PyLong_FromLong(reader_.Size());
    } catch (kj::Exception e) {
      PyErr_Format(PyExc_RuntimeError, "size() failed: %s:%d: %s", e.getFile(),
                   e.getLine(), e.getDescription().cStr());

      return nullptr;
    }
  }

  PyObject* offset() override {
    try {
      return PyLong_FromLong(reader_.Offset());
    } catch (kj::Exception e) {
      PyErr_Format(PyExc_RuntimeError, "size() failed: %s:%d: %s", e.getFile(),
                   e.getLine(), e.getDescription().cStr());

      return nullptr;
    }
  }

 private:
  ev::ColumnFileReader reader_;
};

}  // namespace

ColumnFileReader::~ColumnFileReader() {}

ColumnFileReader* ColumnFileReader::open(const char* path) {
  try {
    return new ColumnFileReaderImpl(
        ev::ColumnFileReader::FileDescriptorInput(
            ev::OpenFile(path, O_RDONLY)));
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error opening column file: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}
