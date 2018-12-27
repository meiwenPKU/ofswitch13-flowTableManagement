cd lib/ofsoftswitch13
./boot.sh
./configure --enable-ns3-lib
make

cd ../../../../
sudo rm -r build/
sudo ./waf configure --enable-examples
#sudo ./waf configure
sudo ./waf
