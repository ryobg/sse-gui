# SSE GUI

Skyrim SE, SKSE plugin providing basic framework for GUI overlays by hooking upon the rendering 
and input pipelines. 

Flow goes on as:
1. During SKSE's Post-Post Load message the render and input factories are wrapped
2. During SKSE's Input loaded message, the game window and rendering swaps are prepended
3. SSE GUI interface is broadcasted to willing subscribers
4. Each subscriber chooses to register a callback to the rendering and/or input pipelines

Refer to `include/sse-gui/sse-gui.h` file for API specification.

# Development

* All incoming or outgoing strings are UTF-8 compatible. Internally, SSEH converts these to the
  Windows Unicode when needed.

## Required tools

* Python 3.x for the build system (2.x may work too)
* C++14 compatible compiler available on the PATH
* Uses at run-time https://github.com/ryobg/sse-hooks

## License

LGPLv3, see the LICENSE.md file. Modules in `share/` have their own license.
