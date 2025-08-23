#!/bin/bash

# This script installs dependencies in a folder. I prefer this to fetch_content because it builds faster and needs less storage

export DEPS=~/deps

cd ~/src
# https://github.com/Dav1dde/glad

# git clone https://github.com/glfw/glfw
# git clone https://github.com/ocornut/imgui
# git clone https://github.com/epezent/implot
# git clone https://github.com/taskflow/taskflow

export CXXFLAGS="-O3 -march=native -floop-parallelize-all -floop-nest-optimize -floop-interchange"
export CFLAGS=$CXXFLAGS
#glfw imgui implot

# glfw is also installed system wide
# imgui and implot don't come with cmake install instructions
for i in glfw taskflow
do
    cd ~/src/$i
    mkdir build_release
    cd build_release
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$DEPS
    ninja 2>&1 | tee build.log
    ninja install 2>&1 | tee install.log
done
