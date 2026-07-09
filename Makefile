# ============================================================
# LuaJavaNE Makefile - 支持 macOS (dylib) / Linux (so)
# ============================================================

# ---------- 变量定义 ----------
JAVA_HOME := $(strip $(shell ./select_jdk.sh))
JAVA_BIN := $(JAVA_HOME)/bin/java
JAVAC_BIN := $(JAVA_HOME)/bin/javac

.PHONY: all clean test repl ninja release info

BUILD_DIR = build
OUT_DIR = out
LIB_DIR = lib
JAVA_SRC = java/src/com/luajava
TEST_SRC = test

JLINE_JAR = $(LIB_DIR)/jline.jar
JUNIT_JAR = $(LIB_DIR)/junit-standalone.jar

# ---------- 从 version.properties 读取版本号 ----------
VERSION_FILE := version.properties
ifneq ($(wildcard $(VERSION_FILE)),)
    PROJECT_VERSION := $(shell grep '^PROJECT_VERSION=' $(VERSION_FILE) | cut -d'=' -f2 | tr -d '\r')
    PROJECT_NAME := $(shell grep '^PROJECT_NAME=' $(VERSION_FILE) | cut -d'=' -f2 | tr -d '\r')
endif
PROJECT_VERSION ?= 2.0.0
PROJECT_NAME ?= LuaJavaNE

# ---------- 平台检测 ----------
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LIB_EXT = dylib
else
    LIB_EXT = so
endif
SO_PATH = $(abspath $(BUILD_DIR)/luajava.$(LIB_EXT))

# 收集所有 Java 源码（递归）
JAVA_SRCS := $(shell find $(JAVA_SRC) -name "*.java")
TEST_SRCS := $(shell find $(TEST_SRC) -name "*.java")

# ---------- 默认目标 ----------
all: $(BUILD_DIR)/luajava.$(LIB_EXT) | $(OUT_DIR)
	@echo "========================================"
	@echo "  $(PROJECT_NAME) Build System v$(PROJECT_VERSION)"
	@echo "  JDK: $$($(JAVA_BIN) -version 2>&1 | head -1)"
	@echo "  CC:  $$(cc --version 2>&1 | head -1)"
	@echo "  OS:  $$(uname -sm)"
	@echo "  Library extension: $(LIB_EXT)"
	@echo "========================================"
	@echo "Building Java classes..."
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRCS)
	jar cf luajava.jar -C $(OUT_DIR) .
	@echo "Build complete. Run './luaj.sh' to start."

# ---------- 构建 C 库 ----------
$(BUILD_DIR)/luajava.$(LIB_EXT):
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DPROJECT_VERSION=$(PROJECT_VERSION) .. && $(MAKE)

# ---------- 输出目录 ----------
$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

# ---------- 测试 ----------
test: all
	@echo "Compiling test classes..."
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) $(TEST_SRCS)
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

# ---------- REPL ----------
repl: all
	@./luaj.sh

# ---------- Ninja 构建 ----------
ninja:
	@mkdir -p $(OUT_DIR)
	$(JAVAC_BIN) -d $(OUT_DIR) -cp $(JLINE_JAR) $(JAVA_SRCS)
	jar cf luajava.jar -C $(OUT_DIR) .
	@mkdir -p build_ninja
	@cd build_ninja && cmake -G Ninja -DPROJECT_VERSION=$(PROJECT_VERSION) .. && ninja
	@mkdir -p $(BUILD_DIR)
	@cp build_ninja/luajava.$(LIB_EXT) $(BUILD_DIR)/luajava.$(LIB_EXT)
	@echo "Ninja build + Java complete."

# ---------- 发布包 ----------
release: clean all
	@mkdir -p release/lib release/docs release/examples
	cp build/luajava.$(LIB_EXT) release/luajava.$(LIB_EXT) 2>/dev/null || cp build_ninja/luajava.$(LIB_EXT) release/luajava.$(LIB_EXT)
	cp luajava.jar release/
	cp select_jdk.sh release/
	cp luaj.sh release/
	cp version.properties release/
	cp lib/jline.jar release/lib/
	cp -r examples/* release/examples/
	cp -r docs/* release/docs/
	@echo "Release files are in ./release/"
	@echo "Version: $(PROJECT_VERSION)"

# ---------- 清理 ----------
clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR) build_ninja luajava.jar release
	@echo "Cleaned."

# ---------- 额外信息 ----------
info:
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "JAVA_HOME: $(JAVA_HOME)"
	@echo "Library extension: $(LIB_EXT)"
	@echo "SO_PATH: $(SO_PATH)"