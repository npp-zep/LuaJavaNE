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
	@cd out && jar xf ../lib/jline.jar > /dev/null 2>&1
	@echo "Main-Class: com.luajava.LuaJMain" > manifest.txt
	@jar cvfm luajava.jar manifest.txt -C out . > /dev/null
	@rm -rf out/META-INF/maven out/META-INF/native
	@rm -f manifest.txt

test: all
	@java -Dluajava.library.path=$$PWD/build/luajava.so -cp out:lib/jline.jar com.luajava.TestLuaJava

repl: all
	@./luaj.sh

clean:
	@rm -rf build out luajava.jar
	@echo "Cleaned."
