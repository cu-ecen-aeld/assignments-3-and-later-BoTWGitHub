#!/bin/sh

#Accepts the following arguments: 
#the first argument is a full path to a file (including filename) on the filesystem, referred to below as writefile; 
#the second argument is a text string which will be written within this file, referred to below as writestr

#Exits with value 1 error and print statements if any of the arguments above were not specified

#Creates a new file with name and path writefile with content writestr, 
#overwriting any existing file and creating the path if it doesn’t exist. 
#Exits with value 1 and error print statement if the file could not be created.

if [ "$#" -ne 2 ]
then
    echo "require only 2 arguments"
    exit 1
fi

writefile=$1
writestr=$2
mkdir -p $(dirname $writefile)

touch $writefile
if [ ! -f $writefile ]
then
    echo "touch $writefile failed"
    exit 1
fi

echo $writestr > $writefile
