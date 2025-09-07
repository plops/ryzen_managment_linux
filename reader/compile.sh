# an attempt to use profile guided optimization

export CXXFLAGS="
-O3 -march=native -mtune=native -ffast-math \
    -floop-parallelize-all -floop-nest-optimize \
    -floop-interchange -DNDEBUG \
     measure.cpp eye_capturer.cpp eye_diagram.cpp pm_table_reader.cpp \
    -std=gnu++26 \
    -fprofile-update=atomic -pthread \
    -fprofile-generate=pgo \
"
#    -flto
g++ $CXXFLAGS \
    -o pm_measure_pgo
#    `g++ --help=optimize|cut -d " " -f 3|grep -v "="|grep -- -f|grep -v flive-patching|xargs`
./pm_measure_pgo -c 3
g++ $CXXFLAGS \
    -o pm_measure
