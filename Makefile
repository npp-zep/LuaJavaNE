.PHONY: all clean test junit repl ninja

BUILD_DIR = build
OUT_DIR = out
LIB_DIR = lib
JAVA_SRC = java/src/com/luajava
TEST_DIR = test/com/luajava

JLINE_JAR = $(LIB_DIR)/jline.jar
JUNIT_JAR = $(LIB_DIR)/junit-standalone.jar
SO_PATH = $(abspath $(BUILD_DIR)/luajava.so)

all: $(BUILD_DIR)/luajava.so $(OUT_DIR)
	@echo "Building Java classes..."
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	@echo "Build complete. Run './luaj.sh' to start."

$(BUILD_DIR)/luajava.so:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && $(MAKE)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

junit: all
	@echo "Compiling test classes..."
	javac -d $(OUT_DIR) -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) test/*.java
	@echo "Test classes compiled."

test:
	@echo "Running JUnit tests..."
	java -Dluajava.library.path=$(SO_PATH) \
	     -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) \
	     org.junit.platform.console.ConsoleLauncher \
	     --select-class=com.luajava.AllTests \
	     --select-class=com.luajava.PromiseTest \
	     --select-class=com.luajava.AsyncTest
	@echo "All JUnit tests passed."

repl: all
	@./luaj.sh

ninja:
	@mkdir -p $(BUILD_DIR) $(OUT_DIR)
	javac -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	@mkdir -p build_ninja
	@cd build_ninja && cmake -G Ninja .. && ninja
	@cp build_ninja/luajava.so $(BUILD_DIR)/luajava.so
	@echo "Ninja build + Java complete."

clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR) build_ninja luajava.jar
	@echo "Cleaned."
