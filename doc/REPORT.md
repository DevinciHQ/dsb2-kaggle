Model Performance Report
========================

This file is intended to document the performance of the various models.
Typically, models are measured using 10-fold cross validation, and storing the
predictions made in each fold.

To regenerate the images in this report, run:

    make
    make -f Makefile.model clean
    make -f Makefile.model

# tflow-minimal.py

This is a single-layer network using only the first frame of each 2 chamber or
4 champer view.  This is equivalent to linear regression using the scaled pixel
data as explanatory variables.

## 2 Chamber View Model

![tflow-minimal validation](images/tflow-minimal-validation.png)

## 4 Chamber View Model

![tflow-minimal-4ch validation](images/tflow-minimal-4ch-validation.png)
