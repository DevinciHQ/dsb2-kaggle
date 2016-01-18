#include "python/columnfile.h"

#include <memory>

#include "base/columnfile.h"
#include "base/file.h"
#include "python/object.h"
#include "python/string.h"

namespace {

struct PythonError {};

ev_python::ScopedObject ObjectForRow(
    const std::vector<std::pair<uint32_t, ev::StringRefOrNull>>& row) {
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

  return result;
}

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

      return ObjectForRow(row).release();
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
    return new ColumnFileReaderImpl(ev::ColumnFileReader::FileDescriptorInput(
        ev::OpenFile(path, O_RDONLY)));
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error opening column file: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}

PyObject* ColumnFile_select(PyObject* path, PyObject* fields, PyObject* filters,
                            PyObject* callback) {
  try {
    KJ_REQUIRE(PyCallable_Check(callback));

    ev::ColumnFileSelect select(ev::ColumnFileReader(
        ev::OpenFile(ev_python::GetString(path).c_str(), O_RDONLY)));

    ev_python::ScopedObject field_iterator(PyObject_GetIter(fields));
    if (!field_iterator) return nullptr;

    for (;;) {
      ev_python::ScopedObject item(PyIter_Next(field_iterator.get()));
      if (!item) break;

      KJ_REQUIRE(PyLong_Check(item.get()));
      select.AddSelection(PyLong_AsLong(item.get()));
    }

    ev_python::ScopedObject filter_iterator(PyObject_GetIter(filters));
    if (!filter_iterator) return nullptr;

    for (;;) {
      ev_python::ScopedObject item(PyIter_Next(filter_iterator.get()));
      if (!item) break;

      KJ_REQUIRE(PyTuple_Check(item.get()));
      KJ_REQUIRE(2 == PyTuple_GET_SIZE(item.get()),
                 PyTuple_GET_SIZE(item.get()));

      const auto field_index = PyTuple_GetItem(item.get(), 0);
      KJ_REQUIRE(PyLong_Check(field_index));

      const auto filter_function = PyTuple_GetItem(item.get(), 1);
      KJ_REQUIRE(PyCallable_Check(filter_function));

      select.AddFilter(
          PyLong_AsLong(field_index),
          [filter_function](const ev::StringRefOrNull& value) mutable {
            ev_python::ScopedObject arg;

            if (value.IsNull()) {
              Py_INCREF(Py_None);
              arg.reset(Py_None);
            } else {
              const auto str = value.StringRef();
              arg.reset(PyUnicode_FromStringAndSize(str.data(), str.size()));
            }

            ev_python::ScopedObject result(PyObject_CallFunctionObjArgs(
                filter_function, arg.get(), nullptr));

            if (!result) throw PythonError();

            return PyObject_IsTrue(result.get());
          });
    }

    ev::concurrency::RegionPool region_pool(1, 2048);

    select.Execute(
        region_pool,
        [callback](
            const std::vector<std::pair<uint32_t, ev::StringRefOrNull>>& row) {
          auto arg = ObjectForRow(row);
          ev_python::ScopedObject result(PyObject_CallFunctionObjArgs(
              callback, arg.get(), nullptr));
          if (!result) throw PythonError();
        });

    Py_RETURN_NONE;
  } catch (PythonError) {
    // Exception already set.
    return nullptr;
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "ColumnFile_select failed: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}
