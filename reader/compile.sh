g++ -O3 -march=native -mtune=native -ffast-math \
    -floop-parallelize-all -floop-nest-optimize \
    -floop-interchange -DNDEBUG measure.cpp \
    -std=gnu++26 \
    -flto \
    `g++ --help=optimize|cut -d " " -f 3|grep -v "="|grep -- -f|grep -v flive-patching|xargs`
