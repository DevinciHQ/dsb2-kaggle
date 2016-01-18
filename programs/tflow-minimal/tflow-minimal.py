#!/usr/bin/env python

import array
import csv
import dsb2
import numpy as np
import re
import skimage.transform
import struct
import tensorflow as tf

# Matches frame 1 in each study.
TRAINING_PATH_FILTER = re.compile(r'train/([0-9]+)/study/2ch_[0-9]+/.*-0001\.dcm')

IMAGE_SIZE = 64

OUTPUT_COUNT = 2

LEARNING_RATE = 0.001

BATCH_COUNT = 200

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
training_images = {}

# Given a row from the column file, read the stored image into the
# `training_images` dictionary.
def LoadTrainingInstance(row):
  global training_images

  study = int(TRAINING_PATH_FILTER.search(row[0]).groups()[0])

  assert row[1] == 'uint16', row[1]

  width = struct.unpack('<I', row[2])[0]
  height = struct.unpack('<I', row[3])[0]

  pixels = np.fromstring(row[4], dtype=np.uint16, count=width * height)

  # Standardize the image data (set mean = 0, and stddev = 1).
  stddev = np.std(pixels)
  assert stddev > 0, stddev
  pixels = (pixels - np.mean(pixels)) / stddev

  # Reinterpret 1D array as 2D image.
  pixels = pixels.reshape([width, height])

  # Resize to standard size.
  pixels = skimage.transform.resize(pixels, [IMAGE_SIZE, IMAGE_SIZE])

  training_images[study] = pixels

dsb2.ColumnFile_select(
    'data/dicoms.col',
    [0L, 1L, 2L, 3L, 4L],
    [(0L, lambda s: TRAINING_PATH_FILTER.search(s))],
    LoadTrainingInstance)

training_labels = ReadTrainingLabels()

assert len(training_labels) == len(training_images)

dataset = np.ndarray(
        shape=(len(training_images), IMAGE_SIZE, IMAGE_SIZE), dtype=np.float32)
labels = np.ndarray(shape=(len(training_labels), OUTPUT_COUNT), dtype=np.float32)

idx = 0

for study, study_labels in training_labels.iteritems():
  dataset[idx, :, :] = training_images[study]
  labels[idx, :] = study_labels
  idx += 1

# Split dataset into training and validation.
np.random.seed(1234)
permutation = np.random.permutation(labels.shape[0])
dataset = dataset[permutation, :, :]
labels = labels[permutation, :]

sample_count = len(training_labels)

# Per-fold validation losses.
validation_losses = []

for fold in range(0, FOLD_COUNT):
  range_begin = int(fold * sample_count / FOLD_COUNT)
  range_end = int((fold + 1) * sample_count / FOLD_COUNT)

  validation_dataset = dataset[range_begin:range_end, :, :]
  validation_labels = labels[range_begin:range_end, :]
  if range_begin > 0:
    train_dataset = np.concatenate([dataset[:range_begin, :, :], dataset[range_end:, :, :]])
    train_labels = np.concatenate([labels[:range_begin, :], labels[range_end:, :]])
  else:
    train_dataset = dataset[range_end:, :, :]
    train_labels = labels[range_end:, :]

  # Build the graph
  graph = tf.Graph()
  with graph.as_default():
    tf.set_random_seed(1234)

    tf_train_images = tf.constant(train_dataset)
    tf_train_labels = tf.constant(train_labels)

    # Fully connected layers need 2D input.
    tf_flat_images = tf.reshape(tf_train_images, [-1, IMAGE_SIZE * IMAGE_SIZE])

    # Add a fully connected layer
    l0_weights = tf.Variable(
        tf.truncated_normal([IMAGE_SIZE * IMAGE_SIZE, OUTPUT_COUNT]))
    l0_biases = tf.Variable(tf.zeros([OUTPUT_COUNT]))
    l0_output = tf.matmul(tf_flat_images, l0_weights) + l0_biases

    # L2 loss
    training_loss = tf.reduce_sum(tf.pow(l0_output - tf_train_labels, 2)) / (2 * len(training_labels))

    optimizer = tf.train.GradientDescentOptimizer(LEARNING_RATE).minimize(training_loss)

    # The same operations repeated for validation purposes.
    tf_validation_images = tf.constant(validation_dataset)
    tf_validation_labels = tf.constant(validation_labels)
    tf_flat_validation_images = tf.reshape(tf_validation_images, [-1, IMAGE_SIZE * IMAGE_SIZE])
    l0_validation_output = tf.matmul(tf_flat_validation_images, l0_weights) + l0_biases
    validation_loss = tf.reduce_sum(tf.pow(l0_validation_output - tf_validation_labels, 2)) / (2 * len(validation_labels))

  # Train and evaluate
  with tf.Session(graph=graph) as session:
    tf.initialize_all_variables().run()

    for i in range(0, BATCH_COUNT):
      optimizer.run()

    validation_losses.append(validation_loss.eval())

    print('Fold %s (size=%s):' % (fold, range_end - range_begin))
    print('  Training loss:   %8.2f' % training_loss.eval())
    print('  Validation loss: %8.2f' % validation_losses[-1])

print('Aggregate validation loss: mean=%.2f stddev=%.2f' % (np.mean(validation_losses), np.std(validation_losses)))
