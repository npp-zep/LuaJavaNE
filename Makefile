.PHONY: all clean test junit repl

# 路径配置
BUILD_DIR = build
OUT_DIR = out
LIB_DIR = lib
SRC_DIR = src
TEST_DIR = test
JNI_DIR = jni
LUA_DIR = Lua5.4.8

JLINE_JAR = $(LIB_DIR)/jline.jar
JUNIT_JAR = $(LIB_DIR)/junit-standalone.jar
SO_PATH = $(abspath $(BUILD_DIR)/luajava.so)

# Java 编译 classpath
CP = $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR)

all: $(BUILD_DIR)/luajava.so $(OUT_DIR)
	@echo "Building Java classes..."
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(SRC_DIR)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(SRC_DIR)/AsyncRunner.java
# C 编译（委托给 CMake）
$(BUILD_DIR)/luajava.so: $(JNI_DIR)/luajava.c $(LUA_DIR)/lualibjava.c $(LUA_DIR)/lualib_async.c
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

# 编译测试类
junit: all
	@echo "Compiling test classes..."
	javac -d $(OUT_DIR) -cp $(CP) $(TEST_DIR)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(SRC_DIR)/AsyncRunner.java
	jar cf luajava.jar -C $(OUT_DIR) .
	jar uf luajava.jar -C $(OUT_DIR) com/luajava/AsyncRunner.class
	@echo "Test classes compiled."

# 运行 JUnit 测试
test: junit
	@echo "Running JUnit tests..."
	java -Dluajava.library.path=$(SO_PATH) \
	     -cp $(CP):$(JUNIT_JAR) \
	     org.junit.platform.console.ConsoleLauncher \
	     --select-class=com.luajava.AllTests \
	     --select-class=com.luajava.PromiseTest --select-class=com.luajava.AsyncTest
	@echo ""
	@echo "All JUnit tests passed."

# REPL
repl: all
	@./luaj.sh

clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR)
	@echo "Cleaned."
