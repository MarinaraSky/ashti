#!/bin/bash

#This script will make a fresh compilation of my server
#and begin hosting it on its default port.
#Script with then find all servable files in the provided website
#directory and GET request each file found. 
#After each request is found it will diff that file against a known good file
#This testing isnt so much for proving functionality as to making sure everything
#is running as it should. It will demonstrate some functionality, but not all.
#Serving images and testing cgi's with query's will have to be done manually.
#Things knowngood
#serving .html, .txt, and non query cgi-scripts
#serving 500 on query scripts with invalid queries

make clean >/dev/null && make >/dev/null

FILEOUT=curTest
./bin/ashti www_root >/dev/null 2>&1 &
PORT=$UID

if [[ $UID -lt 1024 ]]
then
	PORT=9001
fi

for x in $(find www_root/www | grep -E "\.txt|html")
do
	file=${x#'www_root/www'}
	printf "GET ${file} HTTP/1.1" | nc localhost $PORT >> $FILEOUT
done

for y in $(find www_root/cgi-bin | grep -E "py")
do
	file2=${y#'www_root'}
	printf "GET ${file2} HTTP/1.1" | nc localhost $PORT >> $FILEOUT
done

diff <(grep -v "Date" $FILEOUT) <(grep -v "Date" knowngood) 

if [[ $? -eq 0 ]]
then
	echo "Tests passed"
else
	echo "Failed Tests"
fi

rm $FILEOUT

pkill -9 ashti >/dev/null 2>&1
