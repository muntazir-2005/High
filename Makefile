# Makefile - Ultimate Hook for macOS (no subfolders)
CXX = clang++
CXXFLAGS = -stdlib=libc++ -O3 -Wall -fvisibility=hidden -fPIC
FRAMEWORKS = -framework Security -framework CoreFoundation -framework SystemConfiguration -framework ApplicationServices
LDFLAGS = -dynamiclib $(FRAMEWORKS)

TARGET = ultimate_hook.dylib
SOURCE = hook.cpp

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $(SOURCE) -o $(TARGET)
	@echo "[✓] $(TARGET) built successfully"
	@otool -L $(TARGET) | head -n 2

clean:
	rm -f $(TARGET) test_dlopen

test: $(TARGET)
	@echo '#include <dlfcn.h>\nint main() { void* h = dlopen("./$(TARGET)", RTLD_LAZY); return h ? 0 : 1; }' \
	 | $(CXX) -xc++ -o test_dlopen - -ldl && \
	 ./test_dlopen && echo "[✓] Library loads correctly" || echo "[✗] Load failed"; \
	 rm -f test_dlopen
