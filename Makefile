# hfsgraph — project task runner.
# `make help` lists targets. CI and humans run the same commands.

.DEFAULT_GOAL := help
CMD ?=
BUILD_DIR ?= build
SOURCES := $(shell find src -name '*.cpp' -o -name '*.h' 2>/dev/null)

.PHONY: help configure build run test lint format check clean adr docs docs-lint index

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

## --- Build / run -----------------------------------------------------------

configure: ## Configure the CMake build tree
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug

build: configure ## Compile
	cmake --build $(BUILD_DIR)

run: build ## Run the app
	$(BUILD_DIR)/hfsgraph

test: build ## Run the test suite (ctest)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

## --- Quality ---------------------------------------------------------------

lint: ## Check formatting (clang-format dry-run)
	@clang-format --dry-run --Werror $(SOURCES) && echo "format OK"

format: ## Auto-format C++ with clang-format
	clang-format -i $(SOURCES)

check: lint test docs-lint ## Run all quality gates (lint + test + docs lint)

clean: ## Remove build artifacts
	rm -rf $(BUILD_DIR)

## --- Docs / ADRs -----------------------------------------------------------

adr: ## ADR tool — pass a command, e.g. `make adr CMD="list --group"`
	docs/scripts/adr $(CMD)

index: ## Regenerate the ADR index
	docs/scripts/adr index -y

docs: ## Docs catalog tool — pass a command, e.g. `make docs CMD=coverage`
	docs/scripts/doc $(CMD)

docs-lint: ## Lint ADRs (and docs catalog if present)
	docs/scripts/adr lint --check
