#!/usr/bin/env python

import argparse
import bayes_opt
import csv
import dsb2
import numpy
import re
import sys
import xgboost as xgb

parser = argparse.ArgumentParser(description='DSB2 XGBoost Model')
parser.add_argument('--validation-output', metavar='validation_output', type=str, nargs='?',
                    help='path of validation output',
                    default=None)
parser.add_argument('--prediction-output', metavar='prediction_output', type=str, nargs='?',
                    help='path of prediction output',
                    default=None)
args = parser.parse_args()

# Matches frame 1 in each study.
TRAINING_PATH_FILTER = re.compile(r'[^/]+/([0-9]+)/study/2ch_[0-9]+/.*-0001\.dcm')

FOLD_COUNT = 10

# Reads the contents of data/train.csv into a dictionary.
def ReadTrainingLabels():
  reader = csv.reader(open('data/train.csv', 'r'))
  header = reader.next()

  result = {}

  for row in reader:
    result[int(row[0])] = [float(row[1]), float(row[2])]

  return result

# Maps from training instance ("study") to 2D images.
images = {}

#(0010, 0040) Patient's Sex CS: 'M'
#(0010, 1010) Patient's Age AS: '050Y'

# Given a row from the column file, read the stored image into the
# `images` dictionary.
def LoadTrainingInstance(row):
  global images

  study = int(TRAINING_PATH_FILTER.search(row[0]).groups()[0])

  data = []

  # Sex
  data.append(1 if row[0x00100040] == 'M' else 0)

  # Age
  age = row[0x00101010]
  unit = age[-1]
  assert unit in ('Y', 'M', 'W'), unit
  age = int(age[:-1].lstrip('0'))
  if unit == 'Y':
    age *= 52
  elif unit == 'W':
    age *= 0.25
  data.append(age)

  images[study] = data

dsb2.ColumnFile_select(
    'data/dicoms.col',
    [0L, 0x00100040L, 0x00101010L],
    [(0L, lambda s: TRAINING_PATH_FILTER.search(s))],
    LoadTrainingInstance)

X = []
y0 = []
y1 = []

training_studies = set()

for study, study_labels in ReadTrainingLabels().iteritems():
  X.append(images[study])
  y0.append(study_labels[0])
  y1.append(study_labels[1])
  training_studies.add(study)

X_validate = []
study_validate = []

for study, data in images.iteritems():
  if study in training_studies:
    continue
  X_validate.append(data)
  study_validate.append(study)

if args.validation_output is not None:
  validation_output = open(args.validation_output, 'w')
else:
  validation_output = None

def GetXGBRegressor():
  return xgb.XGBRegressor(learning_rate=0.0275, max_depth=3, min_child_weight=0.6, n_estimators=100, seed=1)

#XGBRegressor(base_score=0.5, colsample_bytree=1, gamma=0, learning_rate=0.1,
#       max_delta_step=0, max_depth=3, min_child_weight=1, missing=None,
#       n_estimators=100, nthread=-1, objective='reg:linear', seed=0,
#       silent=True, subsample=1)

y0_errors = []
y1_errors = []

for fold in range(0, FOLD_COUNT):
  sample_count = len(X)
  range_begin = int(fold * sample_count / FOLD_COUNT)
  range_end = int((fold + 1) * sample_count / FOLD_COUNT)

  X_test = X[range_begin:range_end]
  y0_test = y0[range_begin:range_end]
  y1_test = y1[range_begin:range_end]

  X_train = X[:range_begin] + X[range_end:]
  y0_train = y0[:range_begin] + y0[range_end:]
  y1_train = y1[:range_begin] + y1[range_end:]

  y0_model = GetXGBRegressor().fit(X_train, y0_train)
  y1_model = GetXGBRegressor().fit(X_train, y1_train)

  for values in zip(y0_model.predict(X_test), y1_model.predict(X_test), y0_test, y1_test):
    y0_errors.append(abs(values[2] - values[0]))
    y1_errors.append(abs(values[3] - values[1]))
    if validation_output is not None:
      validation_output.write('%.2f %.2f %.2f %.2f\n' % values)

  validation_output.flush()

sys.stderr.write('Standard deviation: %.3f %.3f\n' % (numpy.std(y0_errors), numpy.std(y1_errors)))

if args.prediction_output is not None:
  prediction_output = open(args.prediction_output, 'w')

  y0_model = GetXGBRegressor().fit(X, y0)
  y1_model = GetXGBRegressor().fit(X, y1)

  for values in zip(study_validate, y0_model.predict(X_validate), y1_model.predict(X_validate)):
    prediction_output.write('%s\t%s\t%s\n' % values)

  prediction_output.close()
