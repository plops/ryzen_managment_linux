export DEPS=~/deps

cd ~/src
# https://github.com/Dav1dde/glad

git clone https://github.com/glfw/glfw
git clone https://github.com/ocornut/imgui
git clone https://github.com/epezent/implot
git clone https://github.com/taskflow/taskflow

for i in glfw imgui implot taskflow
do
    cd ~/src/$i
    mkdir build_release
    cd build_release
    cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PATH=$DEPS
    ninja 2>&1 | tee build.log
    ninja install 2>&1 | tee install.log
done
