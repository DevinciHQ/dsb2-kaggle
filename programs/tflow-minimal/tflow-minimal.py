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

validation_dataset = dataset[:100, :, :]
validation_labels = labels[:100, :]
dataset = dataset[100:, :, :]
labels = labels[100:, :]

# Build the graph
graph = tf.Graph()
with graph.as_default():
  tf_train_images = tf.constant(dataset)
  tf_train_labels = tf.constant(labels)

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

# Train
with tf.Session(graph=graph) as session:
  tf.initialize_all_variables().run()

  for i in range(0, 200):
    optimizer.run()
  print('Training loss:   %8.2f' % training_loss.eval())
  print('Validation loss: %8.2f' % validation_loss.eval())
