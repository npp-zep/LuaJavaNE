.PHONY: all clean c java jar test repl

all: c java jar
@echo "Build complete. Run './luaj.sh' to start."

c:
@mkdir -p build
@cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$$(nproc)

java:
@mkdir -p out
@javac -cp lib/jline.jar -d out src/*.java

jar: java
@jar cvf luajava.jar -C out . > /dev/null

test: all
@java -Dluajava.library.path=$$PWD/build/luajava.so -cp out:lib/jline.jar com.luajava.TestLuaJava

repl: all
@./luaj.sh

clean:
@rm -rf build out luajava.jar
@echo "Cleaned."
