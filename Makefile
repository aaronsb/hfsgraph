# hfsgraph — project task runner.
# `make help` lists targets. CI and humans run the same commands.

.DEFAULT_GOAL := help
CMD ?=

.PHONY: help build run test lint format check clean adr docs docs-lint index

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

## --- Build / run -----------------------------------------------------------

build: ## Compile (debug)
	cargo build

run: ## Run the app
	cargo run

test: ## Run the test suite
	cargo test

## --- Quality ---------------------------------------------------------------

lint: ## Run clippy (deny warnings)
	cargo clippy --all-targets -- -D warnings

format: ## Auto-format with rustfmt
	cargo fmt

check: lint test docs-lint ## Run all quality gates (lint + test + docs lint)

clean: ## Remove build artifacts
	cargo clean

## --- Docs / ADRs -----------------------------------------------------------

adr: ## ADR tool — pass a command, e.g. `make adr CMD="list --group"`
	docs/scripts/adr $(CMD)

index: ## Regenerate the ADR index
	docs/scripts/adr index -y

docs: ## Docs catalog tool — pass a command, e.g. `make docs CMD=coverage`
	docs/scripts/doc $(CMD)

docs-lint: ## Lint ADRs (and docs catalog if present)
	docs/scripts/adr lint --check
