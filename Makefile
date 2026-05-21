# Makefile for Ultimate Hook (macOS)
CXX = clang++
CXXFLAGS = -stdlib=libc++ -O3 -Wall -fvisibility=hidden -fPIC
FRAMEWORKS = -framework Security -framework CoreFoundation -framework SystemConfiguration -framework ApplicationServices
LDFLAGS = -dynamiclib $(FRAMEWORKS)

TARGET = ultimate_hook.dylib
SRCDIR = src
BUILDDIR = build
SRC = $(SRCDIR)/hook.cpp
OBJ = $(BUILDDIR)/hook.o

.PHONY: all clean install test

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) $< -o $@
	@echo "[+] Built $(TARGET)"
	@otool -L $(TARGET)  # Show linked libraries

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: $(TARGET)
	@echo "To inject into a game: DYLD_INSERT_LIBRARIES=$(PWD)/$(TARGET) /path/to/Game.app/Contents/MacOS/Game"
	@echo "For testing, you can also run: make test"

test: $(TARGET)
	@echo "Testing library with a simple dlopen..."
	@echo '#include <dlfcn.h>\nint main() { void* h = dlopen("./$(TARGET)", RTLD_LAZY); return h ? 0 : 1; }' | $(CXX) -xc++ -o test_dlopen - -ldl && ./test_dlopen && echo "[✓] Library loads successfully" || echo "[✗] Failed to load"; rm -f test_dlopen

# Show symbols for debugging
symbols: $(TARGET)
	nm -gU $(TARGET) | grep -E " (T|D|B) "

# Generate a simple entitlements file (optional)
entitlements:
	@echo "Creating entitlements.plist for ad-hoc signing"
	@echo '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict>\n    <key>com.apple.security.cs.allow-unsigned-executable-memory</key>\n    <true/>\n    <key>com.apple.security.cs.disable-library-validation</key>\n    <true/>\n</dict>\n</plist>' > entitlements.plist

sign: $(TARGET) entitlements
	codesign -f -s - --entitlements entitlements.plist $(TARGET)
	codesign -vvv $(TARGET)
