# Copyright (C) 2026 Christian Mayer
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared warning flags for the project's own targets.
function(ampbeam_set_warnings target)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Woverloaded-virtual
      -Wold-style-cast
      -Wcast-align
      -Wunused
      -Wdouble-promotion>)
endfunction()

# Stricter flags for the plain-C++ core, which must stay warning-clean.
function(ampbeam_set_strict_warnings target)
  ampbeam_set_warnings(${target})
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
      -Wconversion
      -Wsign-conversion
      -Wuseless-cast>)
endfunction()
