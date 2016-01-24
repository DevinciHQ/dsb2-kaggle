#!/usr/bin/env python

import argparse
import array
import csv
import dsb2
import numpy as np
import re
import skimage.transform
import struct
import sys
import tensorflow as tf

parser = argparse.ArgumentParser(description='DSB2 Neural Net Model')
parser.add_argument('--validation-output', metavar='validation_output', type=str, nargs='?',
                    help='path of validation output',
                    default=None)
args = parser.parse_args()

# Matches frame 1 in each study.
TRAINING_PATH_FILTER = re.compile(r'train/([0-9]+)/study/4ch_[0-9]+/IM-[0-9]+-(0+[1-9][0-9]*)(-0002)?\.dcm')

IMAGE_SIZE = 64

OUTPUT_COUNT = 2

LEARNING_RATE = 0.0000001

BATCH_COUNT = 400

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

# Given a row from the column file, read the stored image into the
# `images` dictionary.
def LoadTrainingInstance(row):
  global images

  m = TRAINING_PATH_FILTER.search(row[0]).groups()
  study = int(m[0])
  frame = int(m[1]) - 1

  assert row[1] == 'uint16', row[1]

  width = struct.unpack('<I', row[2])[0]
  height = struct.unpack('<I', row[3])[0]

  pixels = np.fromstring(row[4], dtype=np.uint16, count=width * height)

  # Reinterpret 1D array as 2D image.
  pixels = pixels.reshape([width, height])

  # Resize to standard size.
  pixels = skimage.transform.resize(pixels, [IMAGE_SIZE, IMAGE_SIZE])

  if not study in images:
    images[study] = np.zeros(shape=(30, IMAGE_SIZE, IMAGE_SIZE), dtype=np.float32)

  images[study][frame, :, :] = pixels

dsb2.ColumnFile_select(
    'data/dicoms.col',
    [0L, 1L, 2L, 3L, 4L],
    [(0L, lambda s: TRAINING_PATH_FILTER.search(s))],
    LoadTrainingInstance)

# Standardize the image data (set mean = 0, and stddev = 1).
for key, pixels in images.iteritems():
  # First subtract the mean image from all 30 images.  This should make parts
  # that aren't moving less prominent.
  pixels -= np.mean(pixels, axis=0)

  stddev = np.std(pixels)
  assert stddev > 0, stddev
  mean = np.mean(pixels)
  images[key] = (pixels - mean) / stddev

np_images = np.ndarray(
        shape=(len(images), 30, IMAGE_SIZE, IMAGE_SIZE), dtype=np.float32)
np_labels = np.ndarray(shape=(len(images), OUTPUT_COUNT), dtype=np.float32)

idx = 0

for study, study_labels in ReadTrainingLabels().iteritems():
  if study not in images:
    continue
  np_images[idx, :, :] = images[study]
  np_labels[idx, :] = study_labels
  idx += 1

# Split dataset into training and validation.
np.random.seed(1234)
permutation = np.random.permutation(np_labels.shape[0])
np_images = np_images[permutation, :, :]
np_labels = np_labels[permutation, :]

# Per-fold validation losses.
validation_losses = []

predictions = np.ndarray(shape=(len(np_labels), 2))

for fold in range(0, FOLD_COUNT):
  sample_count = len(images)
  range_begin = int(fold * sample_count / FOLD_COUNT)
  range_end = int((fold + 1) * sample_count / FOLD_COUNT)

  validation_dataset = np_images[range_begin:range_end, :, :]
  validation_labels = np_labels[range_begin:range_end, :]
  train_dataset = np.concatenate([np_images[:range_begin, :, :], np_images[range_end:, :, :]])
  train_labels = np.concatenate([np_labels[:range_begin, :], np_labels[range_end:, :]])

  training_means = np.mean(train_labels, axis=0)

  # Build the graph
  graph = tf.Graph()
  with graph.as_default():
    tf.set_random_seed(1234)

    tf_train_images = tf.constant(train_dataset)
    tf_train_labels = tf.constant(train_labels)

    # Fully connected layers need 2D input.
    tf_flat_images = tf.reshape(tf_train_images, [-1, 30 * IMAGE_SIZE * IMAGE_SIZE])

    # First fully connected layer.
    l0_weights = tf.Variable(tf.truncated_normal([30 * IMAGE_SIZE * IMAGE_SIZE, 128]))
    l0_biases = tf.Variable(tf.zeros([128]))
    l0_output = tf.nn.relu(tf.matmul(tf.nn.dropout(tf_flat_images, 0.2), l0_weights) + l0_biases)

    # Second fully connected layer.
    l1_weights = tf.Variable(tf.truncated_normal([128, OUTPUT_COUNT]))
    l1_biases = tf.Variable(tf.zeros([OUTPUT_COUNT]))
    l1_output = tf.matmul(l0_output, l1_weights) + l1_biases

    # Add means
    final_bias = tf.constant(training_means)
    final_output = l1_output + final_bias

    # L2 loss
    training_loss = tf.reduce_sum(tf.pow(final_output - tf_train_labels, 2)) / (2 * len(train_labels))

    optimizer = tf.train.GradientDescentOptimizer(LEARNING_RATE).minimize(training_loss)

    # The same operations repeated for validation purposes.
    tf_validation_images = tf.constant(validation_dataset)
    tf_validation_labels = tf.constant(validation_labels)
    tf_flat_validation_images = tf.reshape(tf_validation_images, [-1, 30 * IMAGE_SIZE * IMAGE_SIZE])
    validation_l0_output = tf.nn.relu(tf.matmul(tf_flat_validation_images, l0_weights) + l0_biases)
    validation_l1_output = tf.matmul(validation_l0_output, l1_weights) + l1_biases
    validation_final_output = validation_l1_output + final_bias
    validation_loss = tf.reduce_sum(tf.pow(validation_final_output - tf_validation_labels, 2)) / (2 * len(validation_labels))

  # Train and evaluate
  with tf.Session(graph=graph) as session:
    tf.initialize_all_variables().run()

    for i in range(0, BATCH_COUNT):
      if 0 == (i % 10):
        print(i, training_loss.eval(), validation_loss.eval())
      optimizer.run()

    validation_losses.append(validation_loss.eval())

    print('Fold %s (size=%s):' % (fold, range_end - range_begin))
    print('  Training loss:   %8.2f' % training_loss.eval())
    print('  Validation loss: %8.2f' % validation_losses[-1])

    predictions[range_begin:range_end, :] = validation_final_output.eval()

print('Aggregate validation loss: mean=%.2f stddev=%.2f' % (np.mean(validation_losses), np.std(validation_losses)))

if args.validation_output is not None:
  with open(args.validation_output, 'w') as output:
    for prediction, actual in zip(predictions, np_labels):
      output.write('%.2f %.2f %.2f %.2f\n' % (prediction[0], prediction[1], actual[0], actual[1]))

# Aggregate validation loss: mean=2674.70 stddev=1429.00
