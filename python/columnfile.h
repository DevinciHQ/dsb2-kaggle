#ifndef PYTHON_COLUMNFILE_H_
#define PYTHON_COLUMNFILE_H_ 1

#include <Python.h>

class ColumnFile {
 public:
  // Creates a new table, or replaces an existing one.
  static ColumnFile* create(const char* path);

  static ColumnFile* create_from_fd(int fd);

  virtual ~ColumnFile();

  virtual PyObject* save_double_as_float() = 0;

  virtual PyObject* set_flush_interval(PyObject* interval) = 0;

  // Inserts a complete row into the column file.
  virtual PyObject* add_row(PyObject* row) = 0;

  virtual PyObject* flush() = 0;

  virtual PyObject* finish() = 0;
};

class ColumnFileReader {
 public:
  // Opens an existing table for reading.
  static ColumnFileReader* open(const char* path);

  virtual ~ColumnFileReader();

  virtual bool end() = 0;

  virtual PyObject* set_column_filter(PyObject* columns) = 0;

  virtual PyObject* get_row() = 0;

  virtual PyObject* size() = 0;

  virtual PyObject* offset() = 0;
};

PyObject* ColumnFile_select(PyObject* path, PyObject* fields, PyObject* filters,
                            PyObject* callback);

#endif  // !PYTHON_LEVELDB_TABLE_
