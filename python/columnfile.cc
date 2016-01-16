#include "python/columnfile.h"

#include <kj/debug.h>

#include "base/columnfile.h"
#include "base/file.h"
#include "python/object.h"

namespace {

class ColumnFileImpl : public ColumnFile {
 public:
  ColumnFileImpl(const char* path) : column_file_writer_(path) {}

  ColumnFileImpl(kj::AutoCloseFd&& fd) : column_file_writer_(std::move(fd)) {}

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
  ev::ColumnFileWriter column_file_writer_;

  bool save_double_as_float_ = false;

  size_t flush_interval_ = 0;
  bool autoflush_ = false;

  // Number of unflushed rows.
  size_t unflushed_ = 0;
};

PyObject* ColumnFileImpl::add_row(PyObject* row) {
  try {
    std::vector<std::pair<uint32_t, ev::StringRefOrNull>> row_vec;
    std::vector<std::vector<char>> buffers;

    const auto add_item = [this, &row_vec, &buffers](uint32_t idx,
                                                     PyObject* value) {
      if (PyString_Check(value)) {
        row_vec.emplace_back(idx, ev::StringRef(PyString_AS_STRING(value),
                                                PyString_Size(value)));
      } else if (value == Py_None) {
        row_vec.emplace_back(idx, nullptr);
      } else if (PyInt_Check(value)) {
        std::vector<char> buffer;

        int v = PyInt_AsLong(value);
        buffer.resize(sizeof(v));
        memcpy(&buffer[0], &v, sizeof(v));

        buffers.emplace_back(std::move(buffer));
        row_vec.emplace_back(idx, ev::StringRef(buffers.back()));
      } else if (PyLong_Check(value)) {
        std::vector<char> buffer;

        auto v = PyLong_AsLong(value);
        buffer.resize(sizeof(v));
        memcpy(&buffer[0], &v, sizeof(v));

        buffers.emplace_back(std::move(buffer));
        row_vec.emplace_back(idx, ev::StringRef(buffers.back()));
      } else if (PyFloat_Check(value)) {
        std::vector<char> buffer;

        if (!save_double_as_float_) {
          double v = PyFloat_AS_DOUBLE(value);
          buffer.resize(sizeof(v));
          memcpy(&buffer[0], &v, sizeof(v));
        } else {
          float v = PyFloat_AS_DOUBLE(value);
          buffer.resize(sizeof(v));
          memcpy(&buffer[0], &v, sizeof(v));
        }

        buffers.emplace_back(std::move(buffer));
        row_vec.emplace_back(idx, ev::StringRef(buffers.back()));
      } else {
        KJ_FAIL_REQUIRE("Unsupported data type", idx);
      }
    };

    if (PyDict_Check(row)) {
      Py_ssize_t pos = 0;
      PyObject* key;
      PyObject* value;

      while (1 == PyDict_Next(row, &pos, &key, &value)) {
        long idx;
        if (PyLong_Check(key)) {
          idx = PyLong_AsLong(key);
        } else if (PyIter_Check(key)) {
          idx = PyInt_AsLong(key);
        } else {
          KJ_FAIL_REQUIRE("Unexpected key type");
        }

        KJ_REQUIRE(idx >= 0, idx);
        KJ_REQUIRE(idx <= std::numeric_limits<uint32_t>::max(), idx);

        add_item(idx, value);
      }

      std::sort(row_vec.begin(), row_vec.end(),
                [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
                });
    } else {
      ev::ScopedPyObject iterator(PyObject_GetIter(row));
      if (!iterator.get()) return nullptr;

      for (uint32_t idx = 0;; ++idx) {
        ev::ScopedPyObject item(PyIter_Next(iterator.get()));
        if (!item.get()) break;

        add_item(idx, item.get());
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
    auto file = ev::OpenFile(path, O_CREAT | O_RDWR | O_TRUNC | O_APPEND, 0666);
    return new ColumnFileImpl(std::move(file));
  } catch (kj::Exception e) {
    PyErr_Format(PyExc_RuntimeError, "Error creating columnfile: %s:%d: %s",
                 e.getFile(), e.getLine(), e.getDescription().cStr());
    return nullptr;
  }
}

ColumnFile* ColumnFile::append(const char* path) {
  try {
    auto file = ev::OpenFile(path, O_CREAT | O_RDWR | O_APPEND, 0666);
    return new ColumnFileImpl(std::move(file));
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
