.PHONY: all clean test junit repl jar

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

all: $(BUILD_DIR)/luajava.so $(OUT_DIR)
	@echo "Building Java classes..."
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(SRC_DIR)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	@echo "Build complete. Run './luaj.sh' to start."

# C 编译（委托给 CMake）
$(BUILD_DIR)/luajava.so: $(JNI_DIR)/luajava.c $(LUA_DIR)/lualibjava.c $(LUA_DIR)/lualib_async.c
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && $(MAKE)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

# 编译测试类
junit: all
	@echo "Compiling test classes..."
	javac -d $(OUT_DIR) -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) $(TEST_DIR)/*.java
	@echo "Test classes compiled."

# 运行 JUnit 测试
test: junit
	@echo "Running JUnit tests..."
	java -Dluajava.library.path=$(SO_PATH) \
	     -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) \
	     org.junit.platform.console.ConsoleLauncher \
	     --select-class=com.luajava.AllTests \
	     --select-class=com.luajava.PromiseTest \
	     --select-class=com.luajava.AsyncTest
	@echo ""
	@echo "All JUnit tests passed."

# REPL 启动
repl: all
	@./luaj.sh

clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR) luajava.jar
	@echo "Cleaned."