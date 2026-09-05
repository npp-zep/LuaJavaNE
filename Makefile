# ============================================================
# LuaJavaNE Makefile - 支持 macOS (dylib) / Linux (so)
# ============================================================

# ---------- 变量定义 ----------
JAVA_HOME := $(strip $(shell ./select_jdk.sh))
JAVA_BIN := $(JAVA_HOME)/bin/java
JAVAC_BIN := $(JAVA_HOME)/bin/javac

.PHONY: all clean test repl ninja release deb deb-termux info

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

# ---------- 收集所有 C 源码（用于 .so 依赖检查） ----------
NATIVE_SRCS := $(shell find native -name "*.c")
LUA_SRCS := $(shell find lua -name "*.c" ! -name "luac.c")

# ---------- 构建 C 库 ----------
$(BUILD_DIR)/luajava.$(LIB_EXT): $(NATIVE_SRCS) $(LUA_SRCS) CMakeLists.txt
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
	$(JAVA_BIN) -Xshare:off -Dluajava.library.path=$(SO_PATH) \
	     -cp $(OUT_DIR):$(JLINE_JAR):$(JUNIT_JAR) \
	     org.junit.platform.console.ConsoleLauncher \
	     --select-class=com.luajava.AllTests \
	     --select-class=com.luajava.PromiseTest \
	     --select-class=com.luajava.AsyncTest \
	     --select-class=com.luajava.AgentTest \
	     --select-class=com.luajava.CallbackTest
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
	cp LICENSE release/
	cp version.properties release/
	cp lib/jline.jar release/lib/
	cp -r examples/* release/examples/
	cp -r docs/* release/docs/
	@echo "Release files are in ./release/"
	@echo "Version: $(PROJECT_VERSION)"

# ---------- Debian 包 ----------
PACKAGE_NAME := luajavane
DEB_ARCH := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)
DEB_FILE := $(PACKAGE_NAME)_$(PROJECT_VERSION)_$(DEB_ARCH).deb
DEB_DIR := pkg
# 归档根目录：Debian/Ubuntu 为 usr（对应系统 /）；Termux 自动识别
# （优先 PREFIX 环境变量，其次直接探测 /data/data/com.termux 文件系统，不依赖 shell 导出）
DEB_ARCHIVE_PREFIX ?= usr
ifneq ($(findstring /data/data/com.termux,$(PREFIX)),)
DEB_ARCHIVE_PREFIX = data/data/com.termux/files/usr
else ifeq ($(shell test -d /data/data/com.termux/files/usr && echo yes),yes)
DEB_ARCHIVE_PREFIX = data/data/com.termux/files/usr
endif
DEB_PREFIX := $(DEB_DIR)/$(DEB_ARCHIVE_PREFIX)/share/$(PACKAGE_NAME)

# 用 dpkg-deb 直接打包（无需 debhelper）
deb: all
	@echo "========================================"
	@echo "  Building Debian package: $(DEB_FILE)"
	@echo "  Archive prefix: /$(DEB_ARCHIVE_PREFIX)"
	@echo "========================================"
	@rm -rf $(DEB_DIR)
	@mkdir -p $(DEB_DIR)/DEBIAN $(DEB_PREFIX)/lib $(DEB_PREFIX)/docs $(DEB_PREFIX)/examples $(DEB_DIR)/$(DEB_ARCHIVE_PREFIX)/bin
	# Termux 等默认 umask=077，需显式修正权限（dpkg-deb 要求 DEBIAN 目录 0755~0775）
	@chmod -R u=rwX,go=rX $(DEB_DIR)
	# 启动脚本（与 release 布局一致，luaj.sh 按脚本目录查找依赖）
	install -m 0755 luaj.sh $(DEB_PREFIX)/luaj.sh
	install -m 0755 select_jdk.sh $(DEB_PREFIX)/select_jdk.sh
	# bin/luaj 包装脚本（luaj.sh 用 dirname $0 定位，不能直接符号链接）
	@printf '#!/bin/sh\nexec /$(DEB_ARCHIVE_PREFIX)/share/$(PACKAGE_NAME)/luaj.sh "$$@"\n' > $(DEB_DIR)/$(DEB_ARCHIVE_PREFIX)/bin/luaj
	@chmod 0755 $(DEB_DIR)/$(DEB_ARCHIVE_PREFIX)/bin/luaj
	# 运行时库与 jar
	install -m 0644 build/luajava.$(LIB_EXT) $(DEB_PREFIX)/luajava.$(LIB_EXT)
	install -m 0644 luajava.jar $(DEB_PREFIX)/luajava.jar
	install -m 0644 lib/jline.jar $(DEB_PREFIX)/lib/jline.jar
	# 配置与文档
	install -m 0644 version.properties $(DEB_PREFIX)/version.properties
	install -m 0644 LICENSE $(DEB_PREFIX)/LICENSE
	cp -r docs/* $(DEB_PREFIX)/docs/
	cp -r examples/* $(DEB_PREFIX)/examples/
	# DEBIAN/control
	@printf 'Package: %s\nVersion: %s\nSection: interpreters\nPriority: optional\nArchitecture: %s\nMaintainer: %s <%s>\nDepends: default-jre-headless (>= 17) | openjdk-17-jre-headless | openjdk-21-jre-headless | openjdk-17 | openjdk-21 | openjdk-25\nHomepage: %s\nDescription: Lua 5.4 <-> Java bidirectional interop engine (REPL + library)\n LuaJavaNE lets Lua call Java methods and Java call Lua functions directly,\n with async task support, dynamic proxies, a cross-state store and a\n SIMD-accelerated math library (clac). Ships the luaj REPL and the\n luajava library for embedding.\n' \
	    $(PACKAGE_NAME) $(PROJECT_VERSION) $(DEB_ARCH) "npp-zep" "264519049@qq.com" "https://github.com/npp-zep/LuaJavaNE" > $(DEB_DIR)/DEBIAN/control
	@chmod 0644 $(DEB_DIR)/DEBIAN/control
	# 打包（文件属主统一为 root:root）
	dpkg-deb --build --root-owner-group $(DEB_DIR) $(DEB_FILE)
	@rm -rf $(DEB_DIR)
	@echo "Debian package created: $(DEB_FILE)"
	@echo "Install with: sudo apt install ./$(DEB_FILE)   (or: sudo dpkg -i $(DEB_FILE))"

# ---------- Termux 包（归档根目录为完整 Termux prefix） ----------
deb-termux: DEB_ARCHIVE_PREFIX = data/data/com.termux/files/usr
deb-termux: deb

# ---------- 清理 ----------
clean:
	@rm -rf $(BUILD_DIR) $(OUT_DIR) build_ninja luajava.jar release $(DEB_DIR)
	@rm -f *.deb
	@echo "Cleaned."

# ---------- 额外信息 ----------
info:
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "JAVA_HOME: $(JAVA_HOME)"
	@echo "Library extension: $(LIB_EXT)"
	@echo "SO_PATH: $(SO_PATH)"