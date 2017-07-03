# Simple HTTP downloader for [Ecwid](https://github.com/Ecwid/new-job/blob/master/Console-downloader.md)
# Build
Is required compiler C++14 

    git clone https://github.com/dvetutnev/Ecwid-Console-downloader.git
    cd Ecwid-Console-downloader
    git submodule update --init
    mkdir build && cd build
    cmake ..
    cmake --build .
    ctest

