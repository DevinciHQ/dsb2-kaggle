#!/usr/bin/env python

import argparse
import dsb2
import dicom
import sys

parser = argparse.ArgumentParser(description='Binary document classifier')
parser.add_argument('inputs', metavar='inputs', type=str, nargs='+',
                    help='input paths')
parser.add_argument('--output-path', metavar='output_path', type=str, nargs='?',
                    help='output path',
                    default='data/dicoms.col')
args = parser.parse_args()

seen = set()

images = []

output = dsb2.ColumnFile_append(args.output_path)
output.set_flush_interval(100L)

for path in args.inputs:
  image = dicom.read_file(path)

  try:
    pixels = image.pixel_array
  except Exception as e:
    continue

  if len(pixels.shape) != 2:
    continue

  row = {
      0L: path,
      1L: str(pixels.dtype),
      2L: pixels.shape[0],
      3L: pixels.shape[1],
      4L: str(pixels.data),
      }

  for k in image.iterall():
    idx = (k.tag.group << 16) | k.tag.element
    if isinstance(k.value, basestring) or isinstance(k.value, long) or isinstance(k.value, int) or isinstance(k.value, float):
      row[idx] = k.value
    elif k.VR == 'DS':
      row[idx] = ','.join(map(lambda x: '%.19g' % x, k.value))
    else:
      if idx not in seen:
        seen.add(idx)
        print k
  output.add_row(row)

output.finish()
