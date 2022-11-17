sh ./autogen.sh
./configure
cp Makefile.cqs Makefile
make -j 4
# export LD_LIBRARY_PATH=/root/com/wiredtiger/.libs
# export LIBRARY_PATH=/root/com/wiredtiger/.libs
# -g3 包括宏定义, 注意查看makefile -g3 -O0
rm -f /tmp/wt_demo.o /tmp/wt_demo
gcc -g3 -O0 -I. -o /tmp/wt_demo.o -c wt_demo.c
gcc -g3 -O0 /tmp/wt_demo.o -o /tmp/wt_demo -L/root/com/wiredtiger/.libs -lwiredtiger
echo "build /tmp/wt_demo ok"
