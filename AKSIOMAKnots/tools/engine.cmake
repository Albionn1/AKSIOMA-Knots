# AKSIOMA Engine for a tool built on its own (cmake -S tools/<name>): include()
# this, then link Aksioma::Core (geometry, rooms) or Aksioma::Engine (renderer).
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AksiomaEngine.cmake)
