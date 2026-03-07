.PHONY: clean configure all test benchmark

BUILD_DIR := build
DEPS_CACHE := local_deps/croft-deps.cmake

ifeq ($(wildcard $(DEPS_CACHE)),)
CONFIGURE_ARGS :=
else
CONFIGURE_ARGS := -C $(DEPS_CACHE)
endif

clean:
	rm -rf $(BUILD_DIR) build-* cmake-build-* generated

configure:
	cmake -S . -B $(BUILD_DIR) $(CONFIGURE_ARGS)

all: configure
	cmake --build $(BUILD_DIR)

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

benchmark:
	bash ./tools/benchmark_binary_size.sh
