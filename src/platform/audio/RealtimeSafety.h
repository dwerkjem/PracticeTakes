#pragma once

// Marks a function the real-time audio thread runs, so a tool can check the
// audio-thread contract in docs/development/performance/audio-thread-safety.md
// rather than leaving it to prose and code review.
//
// Under Clang 20 or newer this expands to [[clang::nonblocking]], and a build
// with -fsanitize=realtime aborts if the function allocates, takes a lock, or
// makes a blocking system call. Under any other compiler it expands to nothing,
// so the GCC build -- which is the build that ships -- is unaffected by an
// annotation only one toolchain reads.
//
// Two things about the syntax, both of which cost a CI round to discover:
//
//   * It is a function *type* attribute, so it goes after the parameter list
//     like noexcept, not in front of the declaration. Clang rejects the front
//     position outright.
//
//   * Clang requires noexcept alongside it and warns
//     -Wperf-constraint-implies-noexcept otherwise, which -Werror makes fatal.
//     That is not a formality: an exception escaping a function marked this way
//     terminates. For an audio callback that is the honest outcome, since
//     throwing on the audio thread is already forbidden.
//
// Written as:
//
//     void audioCallback(float* out, int n) noexcept PRACTICE_TAKES_NONBLOCKING;

#if defined(__clang__) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define PRACTICE_TAKES_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef PRACTICE_TAKES_NONBLOCKING
#define PRACTICE_TAKES_NONBLOCKING
#endif
