#!/bin/bash
# Reconfigure to generate HTML files instead of JS
# This uses a custom toolchain file that wraps the Emscripten toolchain

set -e

echo "Cleaning build configuration..."
rm -f CMakeCache.txt

echo "Reconfiguring with HTML output..."
cmake -DCMAKE_TOOLCHAIN_FILE=./Emscripten-HTML.cmake ..

echo ""
echo "========================================"
echo "Configuration complete!"
echo "========================================"
echo ""
echo "Now run: make"
echo ""
echo "Output will be .html files in BIN/ directory along with .js and .wasm files."
echo "All three files are needed - open the .html file in a web browser."
echo ""
echo "To serve locally:"
echo "  cd ../BIN"
echo "  python3 -m http.server 8080"
echo "  # Then open http://localhost:8080/altair.html"
echo ""
