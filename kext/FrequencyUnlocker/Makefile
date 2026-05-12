NAME := FrequencyUnlocker
KEXT_ID := local.kext.$(NAME)
KEXT_VERSION := 2026.05.11
MACOS_VERSION_MIN := 13.0
CODE_SIGN_IDENTITY := -

BUILD_DIR := build/Release
KEXT_DIR := $(BUILD_DIR)/$(NAME).kext
KEXT_BIN := $(KEXT_DIR)/Contents/MacOS/$(NAME)
SDK := $(shell xcrun --sdk macosx --show-sdk-path)

all: $(KEXT_BIN)

$(KEXT_BIN): $(NAME).cpp Info.plist entitlements.xml Makefile
	mkdir -p $(KEXT_DIR)/Contents/MacOS
	sed -e 's/$$(PRODUCT_BUNDLE_IDENTIFIER)/$(KEXT_ID)/g' \
	    -e 's/$$(MARKETING_VERSION)/$(KEXT_VERSION)/g' \
	    -e 's/$$(MACOSX_DEPLOYMENT_TARGET)/$(MACOS_VERSION_MIN)/g' \
	    < Info.plist > $(KEXT_DIR)/Contents/Info.plist
	$(CXX) -DXCODE_OFF -DKEXT_ID=$(KEXT_ID) -DKEXT_VERSION=$(KEXT_VERSION) \
	    -nostdinc -std=c++11 -stdlib=libc++ -Os \
	    -fno-builtin -fno-exceptions -fno-rtti -fno-common \
	    -mkernel -fapple-kext -fasm-blocks -fstrict-aliasing \
	    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	    -isystem "$(SDK)/System/Library/Frameworks/Kernel.framework/Headers" \
	    -mmacosx-version-min=$(MACOS_VERSION_MIN) -static $(NAME).cpp \
	    -o $(KEXT_BIN) -Xlinker -kext -nostdlib \
	    -lkmodc++ -lkmod -lcc_kext \
	    -Wall -Wextra -Wno-extra-semi -Wno-unused-parameter
	xcrun codesign --force --deep --sign "$(CODE_SIGN_IDENTITY)" \
	    --entitlements entitlements.xml --timestamp=none $(KEXT_DIR)

clean:
	rm -rf build

.PHONY: all clean
