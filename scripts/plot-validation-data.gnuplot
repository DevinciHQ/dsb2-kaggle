#!/usr/bin/env gnuplot

# 888 is the pixel width of the GitHub Markdown viewer.
set terminal pngcairo size 888,600

set output output_path
set grid
set title input_path
set xlabel "Predicted"
set ylabel "Actual"

plot input_path using 1:3 with points pointtype 7 title "End-systolic", \
     input_path using 2:4 with points pointtype 7 title "End-diastolic"
