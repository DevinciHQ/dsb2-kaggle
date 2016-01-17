#include "python/columnfile.h"

#include <kj/debug.h>

#include "base/columnfile.h"
#include "python/object.h"

namespace {

class ColumnFileImpl : public ColumnFile {
 public:
  ColumnFileImpl(const char* path)
      : region_pool_(1, 1), column_file_writer_(path) {}

  ColumnFileImpl(kj::AutoCloseFd&& fd)
      : region_pool_(1, 1), column_file_writer_(std::move(fd)) {}

  ~ColumnFileImpl() noexcept {}

  PyObject* save_double_as_float() override {
    save_double_as_float_ = true;
    Py_RETURN_NONE;
  }

  PyObject* set_flush_interval(PyObject* interval) override {
    if (!PyLong_Check(interval)) {
      PyErr_Format(PyExc_RuntimeError, "Interval argument must be long");
      return nullptr;
    }

    flush_interval_ = PyLong_AsLong(interval);
    autoflush_ = true;

    Py_RETURN_NONE;
  }

  PyObject* add_row(PyObject* row) override;

  PyObject* flush() override;

  PyObject* finish() override;

 private:
  ev::concurrency::RegionPool region_pool_;

  ev::ColumnFileWriter column_file_writer_;

  bool save_double_as_float_ = false;

  size_t flush_interval_ = 0;
  bool autoflush_ = false;

  // Number of unflushed rows.
  size_t unflushed_ = 0;
};

PyObject* ColumnFileImpl::add_row(PyObject* row) {
  try {
    ev_python::ScopedObject iterator(PyObject_GetIter(row));
    if (!iterator.get()) return nullptr;

    auto region = region_pool_.GetRegion();

    std::vector<std::pair<uint32_t, ev::StringRefOrNull>> row_vec;

    for (uint32_t idx = 0;; ++idx) {
      ev_python::ScopedObject item(PyIter_Next(iterator.get()));
      if (!item.get()) break;

      if (PyBytes_Check(item.get())) {
        row_vec.emplace_back(idx, ev::StringRef(PyBytes_AS_STRING(item.get()),
                                                PyBytes_GET_SIZE(item.get())));
      } else if (PyUnicode_Check(item.get())) {
        ev_python::ScopedObject bytes(PyUnicode_AsUTF8String(item.get()));
        row_vec.emplace_back(
            idx, ev::StringRef(PyBytes_AS_STRING(bytes.get()),
                               PyBytes_GET_SIZE(bytes.get())).dup(region));
      } else if (item.get() == Py_None) {
        row_vec.emplace_back(idx, nullptr);
      } else if (PyFloat_Check(item.get())) {
        ev::StringRef buffer;

        if (!save_double_as_float_) {
          double v = PyFloat_AS_DOUBLE(item.get());
          buffer = ev::StringRef(reinterpret_cast<const char*>(&v), sizeof(v))
                       .dup(region);
        } else {
          float v = PyFloat_AS_DOUBLE(item.get());
          buffer = ev::StringRef(reinterpret_cast<const char*>(&v), sizeof(v))
                       .dup(region);
        }

        row_vec.emplace_back(idx, buffer);
      } else {
        KJ_FAIL_REQUIRE("Unsupported data type", idx);
      }
    }

    column_file_writer_.PutRow(row_vec);
    ++unflushed_;

    if (autoflush_ && unflushed_ >= flush_interval_) {
      column_file_writer_.Flush();
      unflushed_ = 0;
    }
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError,
                 "Error adding row to columnfile: %s:%d: %s", e.getFile(),
                 e.getLine(), e.getDescription().cStr());
    return nullptr;
  }

  Py_RETURN_NONE;
}

PyObject* ColumnFileImpl::flush() {
  try {
    column_file_writer_.Flush();
    unflushed_ = 0;
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error flushing columnfile: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
  Py_RETURN_NONE;
}

PyObject* ColumnFileImpl::finish() {
  try {
    column_file_writer_.Finalize();
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error finalizing columnfile: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
  Py_RETURN_NONE;
}

}  // namespace

ColumnFile::~ColumnFile() {}

ColumnFile* ColumnFile::create(const char* path) {
  try {
    return new ColumnFileImpl(path);
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error creating columnfile: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}

ColumnFile* ColumnFile::create_from_fd(int fd) {
  try {
    return new ColumnFileImpl(kj::AutoCloseFd(fd));
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error creating columnfile: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}
