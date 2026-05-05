// Global using directives for Axiom-ScriptCore.
//
// Several Native collection types (NativeArray, NativeDictionary, NativeBitArray, ...)
// reference System types (IDisposable, KeyValuePair, IComparer, ReadOnlySpan, ...) without
// per-file using directives. Rather than touch every file, the namespaces are pulled in
// once here.

global using System;
global using System.Collections.Generic;
