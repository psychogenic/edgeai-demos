#!/bin/bash
#
SRCDIR=$1
TARGDIR=$2
ENABLE_RANDOM_TARGET=""

if [ -z "$TARGDIR" ]
then
	echo "USAGE: $0 SOURCE_DIR TARGET_DIR"
	exit 1
fi
if [ -z "$SRCDIR" ]
then
	echo "USAGE: $0 SOURCE_DIR TARGET_DIR"
	exit 2
fi


if [ -e "$TARGDIR/right" ]
then
	echo "Copying to $TARGDIR"
else
	mkdir -p $TARGDIR/right
	mkdir -p $TARGDIR/left
fi



for i in jab lhook
do
	echo "COPYING $SRCDIR/left/${i}* "
	cp -a $SRCDIR/left/${i}* $TARGDIR/left
done

for i in cross rhook uppercut
do
	echo "COPYING $SRCDIR/right/${i}* "
	cp -a $SRCDIR/right/${i}* $TARGDIR/right
done

if [ -z "$ENABLE_RANDOM_TARGET" ]
then
	echo "NO random target"
else
	COUNT=0
	mkdir $TARGDIR/random
	for i in cross rhook uppercut
	do
		for bad_detect in $SRCDIR/left/${i}*
		do
			
			COUNT=$((COUNT + 1))
			basename=${bad_detect##*/}
			NAME=${basename/$i/xxxrandom}
			NAME=${NAME/.bin/$COUNT.bin}
			cp $bad_detect $TARGDIR/random/$NAME
		done
	done
	for i in jab lhook
	do
		for bad_detect in $SRCDIR/right/${i}*
		do
			COUNT=$((COUNT + 1))
			basename=${bad_detect##*/}
			NAME=${basename/$i/xxxrandom}
			NAME=${NAME/.bin/$COUNT.bin}
			cp $bad_detect $TARGDIR/random/$NAME
		done
	done
	echo "Found and copied $COUNT random triggers"
fi

if [ -e "trainidle" ]
then
	for i in 00down 00lowguard 00guard
	do
		cp -a trainidle/left/${i}* $TARGDIR/left
		cp -a trainidle/right/${i}* $TARGDIR/right
	done
fi

echo "Done"
