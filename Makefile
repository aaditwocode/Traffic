# TrafficGuru Makefile
# OS-inspired traffic management system with scheduling algorithms

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -O2 -D_XOPEN_SOURCE=500
DEBUG_CFLAGS = -Wall -Wextra -std=c99 -pthread -g -DDEBUG -fsanitize=thread
LIBS = -lncurses -lpthread -lm

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin
TESTDIR = tests
DOCDIR = docs

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
TARGET = $(BINDIR)/trafficguru

# Test files
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/test_%.o)
TEST_TARGET = $(BINDIR)/test_runner

# Phony targets
.PHONY: all clean debug run test install-dependents help docs benchmark

# Default target
all: $(TARGET)

# Main executable
$(TARGET): $(OBJECTS) | $(BINDIR)
	@echo "Linking TrafficGuru..."
	$(CC) $(OBJECTS) -o $@ $(LIBS)
	@echo "Build complete: $@"

# Object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# Test executable
test: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o,$(OBJECTS)) | $(BINDIR)
	@echo "Building test runner..."
	$(CC) $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o,$(OBJECTS)) -o $@ $(LIBS)
	@echo "Running tests..."
	./$(TEST_TARGET)

# Test object files
$(OBJDIR)/test_%.o: $(TESTDIR)/%.c | $(OBJDIR)
	@echo "Compiling test $<..."
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# Debug build
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: clean $(TARGET)
	@echo "Debug build complete"

# Run the application
run: $(TARGET)
	@echo "Starting TrafficGuru..."
	./$(TARGET)

# Performance benchmark
benchmark: $(TARGET)
	@echo "Running performance benchmark..."
	./$(TARGET) --duration 60 --benchmark

# Create directories
$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(OBJDIR) $(BINDIR)
	@echo "Clean complete"

# Install dependencies (Ubuntu/Debian)
install-dependencies:
	@echo "Installing dependencies for Ubuntu/Debian..."
	sudo apt-get update
	sudo apt-get install build-essential libncurses5-dev libncursesw5-dev
	@echo "Dependencies installed"

# Install dependencies (CentOS/RHEL)
install-dependencies-rhel:
	@echo "Installing dependencies for CentOS/RHEL..."
	sudo yum install gcc ncurses-devel
	@echo "Dependencies installed"

# Install dependencies (macOS)
install-dependencies-macos:
	@echo "Installing dependencies for macOS..."
	brew install ncurses
	@echo "Dependencies installed"

# Generate documentation
docs:
	@echo "Generating documentation..."
	@mkdir -p $(DOCDIR)
	doxygen Doxyfile 2>/dev/null || echo "Doxygen not configured, skipping API docs"
	@echo "Documentation generated in $(DOCDIR)"

# Static analysis
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c99 $(SRCDIR)/ 2>/dev/null || echo "Cppcheck not available"

# Memory check (requires valgrind)
memcheck: $(TARGET)
	@echo "Running memory check..."
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET) || echo "Valgrind not available"

# Thread safety check
threadcheck: $(TARGET)
	@echo "Running thread safety check..."
	valgrind --tool=helgrind ./$(TARGET) || echo "Helgrind not available"

# Code formatting
format:
	@echo "Formatting code..."
	clang-format -i $(SRCDIR)/*.c $(INCDIR)/*.h 2>/dev/null || echo "Clang-format not available"

# Profiling
profile: CFLAGS += -pg
profile: clean $(TARGET)
	@echo "Building with profiling support..."
	@echo "Run ./$(TARGET) and then use 'gprof $(TARGET) gmon.out' to analyze"

# Code coverage
coverage: CFLAGS += --coverage
coverage: LDFLAGS += --coverage
coverage: clean test
	@echo "Generating coverage report..."
	gcov $(SRCDIR)/*.c
	@echo "Coverage report generated"

# Check for required tools
check-tools:
	@echo "Checking for required tools..."
	@which gcc > /dev/null || (echo "ERROR: gcc not found" && exit 1)
	@which make > /dev/null || (echo "ERROR: make not found" && exit 1)
	@echo "All required tools found"

# Validate build environment
validate-env: check-tools
	@echo "Validating build environment..."
	@test -d $(SRCDIR) || (echo "ERROR: Source directory $(SRCDIR) not found" && exit 1)
	@test -d $(INCDIR) || (echo "ERROR: Include directory $(INCDIR) not found" && exit 1)
	@echo "Build environment validated"

# Quick build check (compile only)
check-compile: $(OBJECTS)
	@echo "Compilation check passed"

# Help target
help:
	@echo "TrafficGuru Build System"
	@echo "======================="
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Build the main executable (default)"
	@echo "  debug            - Build with debug symbols and checks"
	@echo "  run              - Build and run TrafficGuru"
	@echo "  test             - Build and run tests"
	@echo "  benchmark        - Run performance benchmark"
	@echo "  clean            - Remove build artifacts"
	@echo "  docs             - Generate documentation"
	@echo "  analyze          - Run static analysis"
	@echo "  memcheck         - Run memory leak check (requires valgrind)"
	@echo "  threadcheck      - Run thread safety check (requires valgrind-helgrind)"
	@echo "  format           - Format source code (requires clang-format)"
	@echo "  profile          - Build with profiling support"
	@echo "  coverage         - Build with code coverage"
	@echo "  check-tools      - Check for required build tools"
	@echo "  validate-env     - Validate build environment"
	@echo "  check-compile    - Quick compilation check"
	@echo ""
	@echo "Dependency installation:"
	@echo "  install-dependencies        - Ubuntu/Debian"
	@echo "  install-dependencies-rhel   - CentOS/RHEL"
	@echo "  install-dependencies-macos  - macOS"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build normally"
	@echo "  make debug              # Build debug version"
	@echo "  make run                # Build and run"
	@echo "  make test               # Run tests"
	@echo "  make clean run          # Clean build and run"
	@echo ""

# Project information
info:
	@echo "TrafficGuru - OS-inspired Traffic Management System"
	@echo "=================================================="
	@echo "Source files: $(words $(SOURCES))"
	@echo "Object files: $(words $(OBJECTS))"
	@echo "Test files: $(words $(TEST_SOURCES))"
	@echo "Target: $(TARGET)"
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Libraries: $(LIBS)"

# Continuous integration target
ci: validate-env check-compile test analyze
	@echo "Continuous integration checks passed"

# Installation target (optional)
install: $(TARGET)
	@echo "Installing TrafficGuru..."
	@mkdir -p /usr/local/bin
	@cp $(TARGET) /usr/local/bin/trafficguru
	@echo "TrafficGuru installed to /usr/local/bin/"

# Uninstallation target
uninstall:
	@echo "Uninstalling TrafficGuru..."
	@rm -f /usr/local/bin/trafficguru
	@echo "TrafficGuru uninstalled"

# Create distribution package
dist: clean docs
	@echo "Creating distribution package..."
	@mkdir -p dist
	@tar -czf dist/trafficguru-$(shell date +%Y%m%d).tar.gz \
		--exclude=dist --exclude=obj --exclude=bin \
		--exclude=.git --exclude='*.o' --exclude='trafficguru' \
		.
	@echo "Distribution package created in dist/"

# Development setup
dev-setup: install-dependency format
	@echo "Development environment setup complete"

# Quick rebuild during development
rebuild: clean all
	@echo "Rebuild complete"

# Show compilation database (for IDEs)
compdb:
	@echo "Generating compilation database..."
	@compile_commands_json=$(SRCDIR) compile_commands.json || echo "Bear not available"

# Dependency graph
deps:
	@echo "Generating dependency graph..."
	@$(CC) -MM -I$(INCDIR) $(SOURCES) | dot -Tpng -o deps.png || echo "Graphviz not available"

# Performance targets with different optimization levels
optimize-fast: CFLAGS += -O3 -march=native
optimize-fast: clean $(TARGET)

optimize-small: CFLAGS += -Os
optimize-small: clean $(TARGET)

# Debug with sanitizers
debug-sanitize: CFLAGS += -g -fsanitize=address -fsanitize=undefined
debug-sanitize: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug-sanitize: clean $(TARGET)

# Ensure important targets run first
.DEFAULT_GOAL := all