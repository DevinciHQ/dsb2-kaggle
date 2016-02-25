#!/usr/bin/env python

import sys
import scipy.stats

result = {}

def Distribution(mean, stddev):
  result = []
  dist = scipy.stats.norm(loc=mean, scale=stddev)
  for i in range(0, 600):
    result.append(dist.cdf(i))
  return result


for path in sys.argv[1:]:
  with open(path, 'r') as f:
    for line in f:
      row = line.split('\t')
      study = int(row[0])
      y0 = float(row[1])
      y1 = float(row[2])
      y0_stddev = float(row[3])
      y1_stddev = float(row[4])

      dist_y0 = Distribution(y0, y0_stddev)
      dist_y1 = Distribution(y1, y1_stddev)

      if study not in result:
        result[study] = []
      result[study].append((dist_y0, dist_y1))

sys.stdout.write('Id')
for i in range(0, 600):
  sys.stdout.write(',P%u' % i)
sys.stdout.write('\n')

def FormatNumber(n):
  if n < 0.001:
    return '0'
  elif n > 0.999:
    return '1'
  else:
    return '%.3f' % n

for study in sorted(result.keys()):
  p = result[study]
  sys.stdout.write('%u_Diastole' % study)
  for i in range(0, 600):
    sys.stdout.write(',%s' % FormatNumber(p[0][0][i]))
  sys.stdout.write('\n')
  sys.stdout.write('%u_Systole' % study)
  for i in range(0, 600):
    sys.stdout.write(',%s' % FormatNumber(p[0][1][i]))
  sys.stdout.write('\n')
