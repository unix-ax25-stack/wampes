#!/bin/sh

# on NeXTstep3.3 there's no uname, but wampes' makefiles depend on uname.
# the easiest way is to link this little program to every subdirectory
# under /tcp/* as uname.  - 980206 dl9sau

chmod 755 ./uname-next

cd ..
ln -fs NeXT/uname uname
for file in `ls -F1 | grep / `
do
  cd $file
  ln -fs ../NeXT/uname-next uname
  cd ..
done
