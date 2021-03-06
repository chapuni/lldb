//===-- MIDataTypes.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Overview:    Common global switches, macros, etc.
//
//              This file contains common data types required by applications
//              generally. If supported by the compiler, this file should be
//              #include'd as part of the project's PCH (precompiled header).

#pragma once

//--------------------------------------------------------------------------------------
// Windows headers:
#ifdef _WIN32

// Debugging:
#ifdef _DEBUG
#include <crtdbg.h> // C-runtime debugging library (defines _ASSERT).
#endif              // _DEBUG

#endif // _WIN32

//--------------------------------------------------------------------------------------
// Common definitions:

// Function return status
namespace MIstatus
{
const bool success = true;
const bool failure = false;
}

// Use to avoid "unused parameter" compiler warnings:
#define MIunused(x) (void) x;

#ifdef _WIN32
#define MI_NO_INITIALIZE_VTABLE __declspec(novtable)
#define MI_FORCE_INLINE __forceinline
#else
#define MI_NO_INITIALIZE_VTABLE
#define MI_FORCE_INLINE inline
// __attribute__( ( always_inline ) )
#endif // _WIN32

// Portability issues
#ifdef _WIN64
typedef unsigned __int64 size_t;
typedef unsigned __int64 PointerToInteger_t;
typedef __int64 MIint;
typedef unsigned __int64 MIuint;
#else
#ifdef _WIN32
typedef unsigned int size_t;
typedef unsigned int PointerToInteger_t;
typedef int MIint;
typedef unsigned int MIuint;
#else
//       typedef     long unsigned int size_t; // size_t already defined
typedef unsigned int PointerToInteger_t;
typedef int MIint;
typedef unsigned int MIuint;

#define MAX_PATH 4096
#endif // _WIN32
#endif // _WIN64

//--------------------------------------------------------------------------------------
// Common types:

// Fundamentals:
typedef float MIflt;
typedef double MIdbl;
typedef char MIchar;                 // Defaults to signed char, i.e. MIschar.
typedef signed char MIschar;         // Range: -128 to 127. More explicit than using MIchar.
typedef unsigned char MIuchar;       // Range: 0 to 255.
typedef long long MIint64;           // 64bit signed integer.
typedef unsigned long long MIuint64; // 64bit unsigned integer.

// using namespace std; // Better to put this or std:: at translation units scope.

//--------------------------------------------------------------------------------------
// Common routines:

//--------------------------------------------------------------------------------------
