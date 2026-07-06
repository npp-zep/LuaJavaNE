JAVA_HOME := $(shell ./select_jdk.sh)
JAVA_BIN := $(JAVA_HOME)/bin/java
JAVAC_BIN := $(JAVA_HOME)/bin/javac

.PHONY: all clean test repl ninja release

BUILD_DIR = build
OUT_DIR = out
LIB_DIR = lib
JAVA_SRC = java/src/com/luajava
TEST_SRC = test

JLINE_JAR = $(LIB_DIR)/jline.jar
JUNIT_JAR = $(LIB_DIR)/junit-standalone.jar
SO_PATH = $(abspath $(BUILD_DIR)/luajava.so)

# ========================================
# 环境检测
# ========================================
ENV_INFO := $(shell echo "========================================"; \
    echo "  LuaJavaNE Build System"; \
    echo "  JDK: $$($(JAVA_BIN) -version 2>&1 | head -1)"; \
    echo "  CC:  $$(cc --version 2>&1 | head -1)"; \
    echo "  OS:  $$(uname -sm)"; \
    echo "========================================")

all: $(BUILD_DIR)/luajava.so $(OUT_DIR)
	@echo "$(ENV_INFO)"
	@echo "Building Java classes..."
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/compat/*.java
	jar uf luajava.jar -C $(OUT_DIR) com/luajava/compat/
	@echo "Build complete. Run './luaj.sh' to start."

$(BUILD_DIR)/luajava.so:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && $(MAKE)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

test: all
	@echo "Compiling test classes..."
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) $(TEST_SRC)/*.java
	@echo "Test classes compiled."
	@echo "Running JUnit tests..."
	$(JAVA_BIN) -Dluajava.library.path=$(SO_PATH) \
	     -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) \
	     org.junit.platform.console.ConsoleLauncher \
	     --select-class=com.luajava.AllTests \
	     --select-class=com.luajava.PromiseTest \
	     --select-class=com.luajava.AsyncTest \
	     --select-class=com.luajava.AgentTest
	@echo ""
	@echo "All JUnit tests passed."

repl: all
	@./luaj.sh

ninja:
	@mkdir -p $(OUT_DIR)
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/*.java
	jar cf luajava.jar -C $(OUT_DIR) .
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRC)/compat/*.java
	jar uf luajava.jar -C $(OUT_DIR) com/luajava/compat/
	@mkdir -p build_ninja
	@cd build_ninja && cmake -G Ninja .. && ninja
	@mkdir -p $(BUILD_DIR)
	@cp build_ninja/luajava.so $(BUILD_DIR)/luajava.so
	@echo "Ninja build + Java complete."

release: clean all
	@mkdir -p release/lib release/docs release/examples
	cp build/luajava.so release/luajava.so 2>/dev/null || cp build_ninja/luajava.so release/luajava.so
	cp luajava.jar release/
	cp select_jdk.sh release/
	cp luaj.sh release/
	cp lib/jline.jar release/lib/
	cp -r examples/* release/examples/
	cp -r docs/* release/docs/
	@echo "Release files are in ./release/"

clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR) build_ninja luajava.jar release
	@echo "Cleaned."
