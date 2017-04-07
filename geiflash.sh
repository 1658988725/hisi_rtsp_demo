#!/bin/sh

Partion="/dev/mtdblock0 /dev/mtdblock1 /dev/mtdblock2"
IMGFILE=16M.bin
if [ -e $IMGFILE ]; then
    echo "Delete the old Image file"
    rm $IMGFILE
fi
        
for child in $Partion
do
    if [ -e $child ]; then 
        echo "Append Partion $child"
        cat $child >> $IMGFILE
    fi
done
                                
if [ -e $IMGFILE ]; then
    echo "Check the Iamge file"
    echo `ls -alh $IMGFILE`
fi
