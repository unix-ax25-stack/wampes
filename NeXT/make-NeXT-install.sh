#! /bin/sh

PATH=$PATH:.

cd /tcp
echo please read /tcp/NeXT/README.NeXT, too.

cd NeXT
sh ./configure_helper.sh

cd ..
bash -c make

