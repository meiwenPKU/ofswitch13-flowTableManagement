cd lib/ofsoftswitch13
./boot.sh
./configure --enable-ns3-lib
make

cd ../../../../
sudo rm -r build/
./waf configure --enable-examples --enable-tests --enable-sudo
./waf
