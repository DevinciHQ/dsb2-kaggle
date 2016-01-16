#ifndef PYTHON_COLUMNFILE_H_
#define PYTHON_COLUMNFILE_H_ 1

#include <Python.h>

class ColumnFile {
 public:
  // Creates a new table, or replaces an existing one.
  static ColumnFile* create(const char* path);

  static ColumnFile* append(const char* path);

  static ColumnFile* create_from_fd(int fd);

  virtual ~ColumnFile();

  virtual PyObject* save_double_as_float() = 0;

  virtual PyObject* set_flush_interval(PyObject *interval) = 0;

  // Inserts a complete row into the column file.
  virtual PyObject* add_row(PyObject* row) = 0;

  virtual PyObject* flush() = 0;

  virtual PyObject* finish() = 0;
};

#endif  // !PYTHON_LEVELDB_TABLE_
