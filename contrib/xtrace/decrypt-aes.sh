#! /bin/bash

if [ $# != 1 ]
then
	exec >&2
	echo "usage: decrypt-aes pass:<PASSWORD>"
	echo
	echo "To decrypt a Diablo encrypted X-Trace header, run this program, specifying"
	echo "the password used for encryption.  Feed the entire X-Trace: line, including"
	echo "the X-Trace: header token, to this program, and it will return the"
	echo "original data."
	exit 1
fi

XT=$(sed -ne 's/^X-Trace: //p')
C=$(echo "$XT" | sed -e 's/^\(.*,\|\)\(DX\|\)C=\([^,]\+\).*$/\3/')
if [ "$C" != "" ]
then
	D=$(echo "$C" | openssl enc -aes-256-cbc -d -a -pass "$1" || exit 1)
	echo
	echo "**>> $C <<**"
	echo "==>> $D <<=="
	exit 0
fi

exit 1

