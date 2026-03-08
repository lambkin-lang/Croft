.PHONY: clean configure all test test-core test-threaded test-runner test-wasi benchmark

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

test-core: all
	ctest --test-dir $(BUILD_DIR) -L sapling-core --output-on-failure

test-threaded: all
	ctest --test-dir $(BUILD_DIR) -L sapling-threaded --output-on-failure

test-runner: all
	ctest --test-dir $(BUILD_DIR) -L sapling-runner --output-on-failure

test-wasi: all
	ctest --test-dir $(BUILD_DIR) -L sapling-wasi --output-on-failure

benchmark:
	bash ./tools/benchmark_binary_size.sh
