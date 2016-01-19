import pandas
import re
import csv
from pathlib2 import Path
import dicom

data_dir = "../../data/"

# Load the patient Id, and systolic /dystolic volumes.
initial_data = pandas.read_csv(open(data_dir + '/train.csv', 'r'))

# Use the path globbing from python3
p = Path(data_dir)
img_files = list(p.glob('train/*/study/2ch*/IM-*-0001.dcm'))
img_files.sort()

# Flag for testing so we only load the first one.
stop = False

dicom_data = []

for i in img_files:

    ds = dicom.read_file(str(i))
    pxid = ds.PatientID

    # Create an "Id" column so we have a similar column to join on later.
    record = {'Id': int(pxid)}
    col_names = ds.dir()

     # Remove the PixelData as it's not needed here.
    for unwanted_col in ['PixelData'] :
        col_names.remove(unwanted_col);

    #It's a little bit of a PITA to get a dict of col: value from dicom objects.
    for c in col_names:
        ds_de = ds.data_element(c)
        if ds_de is not None:
            record[c] = ds_de.value

    # Append the record we created to the dicom_data
    dicom_data.append(record)

    print pxid

    if stop == True:
        break

# Create a pandas dataframe at the end.. (This was a LOT faster than updating the dataframe each time.)
df = pandas.DataFrame(dicom_data)

# Join the columns from both dataframes.
initial_data = initial_data.merge(df, on='Id', how='outer')
initial_data = initial_data.drop_duplicates(keep='first', subset='Id')


## DATA CLEANUP

# Set the Patient Age to an integer (drop the 'Y')
initial_data['PatientAge'] = initial_data['PatientAge'].apply(lambda x: int(x[:-1]))




## SAVE THE FILE

# 500 rows, creates a 1.5M file.
initial_data.to_pickle(data_dir + 'metadata.pickle')